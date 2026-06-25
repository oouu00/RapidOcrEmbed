#include "PdfTextLayer.h"
#include "PdfHelper.h"
#include <QImage>
#include <QBuffer>
#include <QPainter>
#include <QPageSize>
#include <QFont>
#include <QDebug>
#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <cstdio>

extern "C" {
#include "mupdf/fitz.h"
#include "mupdf/pdf.h"
#include "mupdf/pdf/clean.h"
}

#include <vector>
#include <algorithm>
#include <cstring>

// ========== 文件日志 (已禁用) ==========
static void pdfLog(const char *fmt, ...) {
    // 文件日志已禁用，仅保留qDebug
}

static void dbgLog(const char *, ...) {}

// ========== 投影分析：从图像区域分割字符边界 ==========
// 输入: 灰度图像区域 (row-major, 值0-255, 文字为深色)
// 输出: 字符左右边界列表 [(x_start, x_end), ...]
static std::vector<std::pair<int,int>> projectSegmentation(
    const unsigned char* region, int regionW, int regionH, int stride,
    int charHeight)
{
    std::vector<std::pair<int,int>> result;
    if (!region || regionW <= 0 || regionH <= 0) return result;

    // 自适应阈值
    int minGap = std::max(8, (int)(charHeight * 0.20));
    int minCharW = std::max(6, (int)(charHeight * 0.10));

    // 垂直投影：每列的深色像素数
    std::vector<int> projection(regionW, 0);
    for (int x = 0; x < regionW; x++) {
        int sum = 0;
        for (int y = 0; y < regionH; y++) {
            if (region[y * stride + x] < 200) sum++;
        }
        projection[x] = sum;
    }

    // 找文字区域（投影>0的连续区域）
    struct Seg { int start, end; };
    std::vector<Seg> segments;
    bool inText = false;
    int segStart = 0;

    for (int x = 0; x < regionW; x++) {
        if (!inText && projection[x] > 0) {
            segStart = x;
            inText = true;
        } else if (inText && projection[x] == 0) {
            segments.push_back({segStart, x});
            inText = false;
        }
    }
    if (inText) segments.push_back({segStart, regionW});

    if (segments.empty()) return result;

    // 合并间隙 < minGap 的相邻区域
    std::vector<Seg> merged;
    merged.push_back(segments[0]);
    for (size_t i = 1; i < segments.size(); i++) {
        int gap = segments[i].start - merged.back().end;
        if (gap < minGap) {
            merged.back().end = segments[i].end;
        } else {
            merged.push_back(segments[i]);
        }
    }

    // 过滤太小的区域
    for (const auto& seg : merged) {
        if (seg.end - seg.start >= minCharW) {
            result.push_back({seg.start, seg.end});
        }
    }

    return result;
}

// 渲染PDF页面为灰度像素（MuPDF）
static bool renderPageGrayscale(fz_context *ctx, fz_document *doc,
    int pageIndex, int dpi,
    std::vector<unsigned char>& outGray, int& outW, int& outH)
{
    fz_page *page = fz_load_page(ctx, doc, pageIndex);
    if (!page) return false;

    float zoom = dpi / 72.0f;
    fz_matrix ctm = fz_scale(zoom, zoom);
    fz_pixmap *pix = fz_new_pixmap_from_page(ctx, page, ctm, fz_device_rgb(ctx), 0);
    fz_drop_page(ctx, page);

    if (!pix) return false;

    outW = pix->w;
    outH = pix->h;
    outGray.resize(outW * outH);

    // RGB -> 灰度
    for (int y = 0; y < outH; y++) {
        const uchar *src = pix->samples + y * pix->stride;
        for (int x = 0; x < outW; x++) {
            uchar r = src[x * 3 + 0];
            uchar g = src[x * 3 + 1];
            uchar b = src[x * 3 + 2];
            outGray[y * outW + x] = (uchar)((r * 77 + g * 150 + b * 29) >> 8);
        }
    }

    fz_drop_pixmap(ctx, pix);
    return true;
}

// ========== 工具函数 ==========

static QImage ensureWhiteBackground(const QImage& img) {
    if (!img.hasAlphaChannel()) return img;
    QImage whiteImg(img.size(), QImage::Format_RGB32);
    whiteImg.fill(Qt::white);
    QPainter p(&whiteImg);
    p.drawImage(0, 0, img);
    p.end();
    return whiteImg;
}

// 根据 box (8 点, 顺时针左上->右下) 估算字号 (像素)
// 参照老版本的 estimateFontSize
static int estimateFontSize(const OcrBlock& b) {
    if (b.box.size() < 8) return 12;
    int leftH  = b.box[7] - b.box[1];   // 左边: 左下.y - 左上.y
    int rightH = b.box[5] - b.box[3];   // 右边: 右下.y - 右上.y
    if (leftH < 0)  leftH  = -leftH;
    if (rightH < 0) rightH = -rightH;
    int h = (leftH + rightH) / 2;
    if (h < 6) h = 6;
    return h;
}

// ========== buildSearchablePdf (MuPDF 方案) ==========

// 前向声明 (定义在 modifyPdfWithOcrText 区域)
static fz_font* loadCjkFont(fz_context *ctx);
static void drawInvisibleText(fz_context *ctx, fz_device *dev,
    fz_font *font, const std::vector<OcrBlock>& blocks, float pageH,
    fz_colorspace *cs, float dpiScale,
    const unsigned char* pageGray, int pageW, int pageGrayH,
    float cropX0 = 0, float cropY1 = 0);

static void injectRenderMode3(fz_context *ctx, fz_buffer *buf) {
    unsigned char *data = NULL;
    size_t len = fz_buffer_storage(ctx, buf, &data);
    if (!data || len == 0) return;

    unsigned char *copy = (unsigned char *)fz_malloc(ctx, len);
    memcpy(copy, data, len);

    fz_clear_buffer(ctx, buf);

    size_t i = 0;
    while (i < len) {
        if (i + 1 < len && copy[i] == 'B' && copy[i + 1] == 'T') {
            fz_append_data(ctx, buf, copy + i, 2);
            fz_append_data(ctx, buf, (const unsigned char *)"\n3 Tr\n", 6);
            i += 2;
        } else {
            fz_append_data(ctx, buf, copy + i, 1);
            i++;
        }
    }

    fz_free(ctx, copy);
    pdfLog("[PdfTextLayer] injectRenderMode3: modified %zu bytes", len);
}

static fz_image* loadImageFromBytes(fz_context *ctx, const QByteArray& imgBytes) {
    fz_buffer *buf = fz_new_buffer_from_shared_data(ctx,
        (const unsigned char*)imgBytes.constData(), imgBytes.size());
    if (!buf) return NULL;

    fz_image *image = NULL;
    fz_try(ctx) {
        image = fz_new_image_from_buffer(ctx, buf);
    } fz_catch(ctx) {
        image = NULL;
    }
    fz_drop_buffer(ctx, buf);
    return image;
}

// 用 MuPDF device 画图片 (全尺寸铺满页面)
// 注意: 图像数据在内存中是 top-to-bottom 存储 (row 0 = 顶部),
// 但 PDF 坐标 y 向上。fz_scale(sx, sy) 会把图像 row 0 映射到
// device y=0 (页面底部), 导致图像上下颠倒。
// 用负 Y 缩放 + 向上平移 pageH 来翻转图像:
//   图像 (0, 0) → device (0, pageH)   [左上 → 页面左上]
//   图像 (w, h) → device (pageW, 0)   [右下 → 页面右下]
static void drawPageImage(fz_context *ctx, fz_device *dev,
    fz_image *image, float pageW, float pageH) {
    if (!image) return;
    float sx = pageW / (float)image->w;
    float sy = pageH / (float)image->h;
    fz_matrix ctm = fz_scale(sx, -sy);
    ctm.f = pageH;
    fz_fill_image(ctx, dev, image, ctm, 1.0f, fz_default_color_params);
}

bool PdfTextLayer::buildSearchablePdf(QByteArray& out,
    const QVector<QPair<QByteArray, std::vector<OcrBlock>>>& pages) {

    pdfLog("[PdfTextLayer] buildSearchablePdf START, pages=%d", pages.size());
    if (pages.isEmpty()) return false;

    fz_context *ctx = fz_new_context(NULL, NULL, 256 << 20);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);

    bool ok = true;
    fz_try(ctx) {
        pdf_document *pdf = pdf_create_document(ctx);
        pdfLog("[PdfTextLayer] pdf_create_document OK");

        fz_font *font = loadCjkFont(ctx);
        fz_colorspace *cs = fz_device_rgb(ctx);

        for (int i = 0; i < pages.size(); i++) {
            const QByteArray& imgBytes = pages[i].first;
            const std::vector<OcrBlock>& blocks = pages[i].second;

            float pageW = 800, pageH = 600;
            fz_image *image = loadImageFromBytes(ctx, imgBytes);
            if (image) {
                pageW = (float)image->w;
                pageH = (float)image->h;
                pdfLog("[PdfTextLayer] page %d: image %dx%.0f", i, image->w, pageH);
            } else {
                pdfLog("[PdfTextLayer] page %d: no image, default 800x600", i);
            }

            fz_rect mediabox = {0, 0, pageW, pageH};
            pdf_obj *resources = NULL;
            fz_buffer *contents = NULL;
            fz_device *dev = pdf_page_write(ctx, pdf, mediabox, &resources, &contents);

            if (image) {
                drawPageImage(ctx, dev, image, pageW, pageH);
            } else {
                float white[3] = {1.0f, 1.0f, 1.0f};
                fz_fill_path(ctx, dev, fz_new_path(ctx), 0, fz_identity,
                    cs, white, 1.0f, fz_default_color_params);
            }

            if (!blocks.empty()) {
                drawInvisibleText(ctx, dev, font, blocks, pageH, cs, 1.0f, nullptr, 0, 0, 0.0f, pageH);
            }

            fz_close_device(ctx, dev);
            fz_drop_device(ctx, dev);

            if (!blocks.empty()) {
                injectRenderMode3(ctx, contents);
            }

            if (image) {
                fz_drop_image(ctx, image);
            }

            pdf_obj *page = pdf_add_page(ctx, pdf, mediabox, 0, resources, contents);
            pdf_insert_page(ctx, pdf, -1, page);
            fz_drop_buffer(ctx, contents);

            pdfLog("[PdfTextLayer] page %d: done, blocks=%zu", i, blocks.size());
        }

        fz_drop_font(ctx, font);

        fz_buffer *foutbuf = fz_new_buffer(ctx, 64 << 20);
        fz_output *fout = fz_new_output_with_buffer(ctx, foutbuf);
        pdf_write_options opts;
        memset(&opts, 0, sizeof(opts));
        pdf_write_document(ctx, pdf, fout, &opts);
        fz_close_output(ctx, fout);
        fz_drop_output(ctx, fout);

        unsigned char *data = NULL;
        size_t len = fz_buffer_storage(ctx, foutbuf, &data);
        out = QByteArray((const char*)data, (int)len);
        pdfLog("[PdfTextLayer] buildSearchablePdf OK, out.size=%d", out.size());

        fz_drop_buffer(ctx, foutbuf);
        pdf_drop_document(ctx, pdf);
    } fz_catch(ctx) {
        pdfLog("[PdfTextLayer] EXCEPTION: %s", fz_caught_message(ctx));
        ok = false;
    }

    fz_drop_context(ctx);
    pdfLog("[PdfTextLayer] buildSearchablePdf END, ok=%d", ok);
    return ok;
}

// ========== modifyPdfWithOcrText (MuPDF 修改已有 PDF) ==========

static fz_font* loadCjkFont(fz_context *ctx) {
    QStringList searchPaths = {
        QCoreApplication::applicationDirPath() + "/fonts/simhei.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simsun.ttc",
    };
    for (const QString& path : searchPaths) {
        if (!QFile::exists(path)) continue;
        QByteArray utf8 = path.toUtf8();
        fz_font *font = NULL;
        fz_try(ctx) {
            font = fz_new_font_from_file(ctx, "CJK", utf8.constData(), 0, 0);
        } fz_catch(ctx) {
            font = NULL;
        }
        if (font) {
            pdfLog("[PdfTextLayer] loaded CJK font: %s", path.toUtf8().constData());
            return font;
        }
    }
    pdfLog("[PdfTextLayer] WARNING: no CJK font found");
    return fz_new_base14_font(ctx, "Helvetica");
}

// 在 MuPDF device 上画不可见文字
// 坐标约定:
//   - OCR box 坐标是屏幕坐标 (y 向下, 原点在图像左上角)
//   - MuPDF/PDF 设备坐标是 y 向上, 原点在页面左下角
//   - dpiScale: 像素到 PDF point 的缩放 (72.0/dpi)
//     buildSearchablePdf: pageW=image->w, pageH=image->h, 坐标1:1, dpiScale=1
//     modifyPdfWithOcrText: PDF页面单位是point, OCR坐标是像素, dpiScale=72.0/dpi
//   - cropX0/cropY1: 用于补偿 MediaBox 与 CropBox 原点差异
//     pdf_page_write 的 pagectm 从 textBox(0,0) 开始, 但页面内容在 MediaBox 坐标系中
//     修正: dev_x += cropX0, dev_y += (pageH - cropY1)
static void drawInvisibleText(fz_context *ctx, fz_device *dev,
    fz_font *font, const std::vector<OcrBlock>& blocks, float pageH,
    fz_colorspace *cs, float dpiScale,
    const unsigned char* pageGray, int pageW, int pageGrayH,
    float cropX0, float cropY1) {

    float black[3] = {0, 0, 0};
    fz_color_params cp = fz_default_color_params;
    int drawn = 0;

    // 补偿 MediaBox 与 CropBox 的偏移
    // 正确映射: dev_x = x*dpiScale + cropX0, dev_y = (pageH - cropY1) + (y0+fontPx)*dpiScale
    float yOffset = pageH - cropY1;

    for (size_t i = 0; i < blocks.size(); i++) {
        const auto& b = blocks[i];
        if (b.text.empty() || b.box.size() < 8) continue;

        int x0 = b.box[0], y0 = b.box[1];
        int x1 = b.box[2], y1 = b.box[3];

        int leftH  = b.box[7] - b.box[1];
        int rightH = b.box[5] - b.box[3];
        if (leftH < 0)  leftH  = -leftH;
        if (rightH < 0) rightH = -rightH;
        int fontPx = (leftH + rightH) / 2;
        if (fontPx < 6) fontPx = 6;

        int boxW = x1 - x0;
        if (boxW < 0) boxW = -boxW;

        float fontPt = fontPx * dpiScale;
        if (fontPt < 2.0f) fontPt = 2.0f;

        int byteCount = (int)b.text.size();
        if (byteCount <= 0) continue;

        int charCount = 0;
        for (int pos = 0; pos < byteCount; ) {
            unsigned char fb = (unsigned char)b.text[pos];
            int cbl = 1;
            if ((fb & 0x80) == 0) cbl = 1;
            else if ((fb & 0xE0) == 0xC0) cbl = 2;
            else if ((fb & 0xF0) == 0xE0) cbl = 3;
            else if ((fb & 0xF8) == 0xF0) cbl = 4;
            pos += cbl;
            charCount++;
        }
        if (charCount <= 0) continue;

        float startX = x0 * dpiScale + cropX0;
        float startY = yOffset + (float)(y0 + fontPx) * dpiScale;
        fz_matrix trm = fz_scale(fontPt, -fontPt);
        trm.e = startX;
        trm.f = startY;

        fz_text *text = fz_new_text(ctx);
        fz_show_string(ctx, text, font, trm, b.text.c_str(), 0, 0,
                       (fz_bidi_direction)0, FZ_LANG_UNSET);
        fz_fill_text(ctx, dev, text, fz_identity, cs, black, 1.0f, cp);
        fz_drop_text(ctx, text);
        drawn++;
    }
    pdfLog("[PdfTextLayer] drawInvisibleText: %d blocks drawn (of %zu), dpiScale=%.3f, hasImage=%d",
           drawn, blocks.size(), dpiScale, pageGray != nullptr);
}

bool PdfTextLayer::modifyPdfWithOcrText(const QString& srcPdfPath,
    const QString& dstPdfPath,
    const QVector<QPair<int, std::vector<OcrBlock>>>& pageBlocks,
    int dpi) {

    pdfLog("[PdfTextLayer] modifyPdfWithOcrText START");
    pdfLog("[PdfTextLayer]   src: %s", srcPdfPath.toUtf8().constData());
    pdfLog("[PdfTextLayer]   dst: %s", dstPdfPath.toUtf8().constData());
    pdfLog("[PdfTextLayer]   pageBlocks: %d", pageBlocks.size());

    if (pageBlocks.isEmpty()) return false;

    // DPI 缩放: OCR 坐标是渲染时的像素坐标, PDF 页面单位是 point (1/72 英寸)
    // scale = 72.0 / dpi, 例如 dpi=150 时 scale=0.48
    int safeDpi = (dpi > 0) ? dpi : 150;
    float dpiScale = 72.0f / (float)safeDpi;
    pdfLog("[PdfTextLayer] dpi=%d, dpiScale=%.4f", safeDpi, dpiScale);

    fz_context *ctx = fz_new_context(NULL, NULL, 256 << 20);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);

    bool ok = true;
    fz_try(ctx) {
        QByteArray srcUtf8 = srcPdfPath.toUtf8();
        pdf_document *pdf = pdf_open_document(ctx, srcUtf8.constData());

        int totalPages = pdf_count_pages(ctx, pdf);
        pdfLog("[PdfTextLayer] totalPages: %d", totalPages);
        if (totalPages == 0) {
            pdf_drop_document(ctx, pdf);
            fz_drop_context(ctx);
            return false;
        }

        QMap<int, const std::vector<OcrBlock>*> ocrMap;
        for (const auto& pair : pageBlocks) {
            ocrMap[pair.first] = &pair.second;
        }

        fz_font *font = loadCjkFont(ctx);

        for (int pi = 0; pi < totalPages; pi++) {
            if (!ocrMap.contains(pi)) continue;
            const std::vector<OcrBlock>& blocks = *ocrMap[pi];
            if (blocks.empty()) continue;

            pdfLog("[PdfTextLayer] page %d: %zu blocks", pi, blocks.size());

            pdf_page *page = pdf_load_page(ctx, pdf, pi);
            fz_rect pageBox = pdf_bound_page(ctx, page, FZ_MEDIA_BOX);
            fz_rect cropBox = pdf_bound_page(ctx, page, FZ_CROP_BOX);
            float pageWpt = pageBox.x1 - pageBox.x0;
            float pageH = pageBox.y1 - pageBox.y0;

            // 读取原始 MediaBox
            // 大坑：pdf_bound_page(FZ_MEDIA_BOX) 返回归一化坐标（原点移到0,0），
            // 不是 PDF 文件中的原始 MediaBox 值！例如原始 MediaBox(-176,-257,419,584)
            // 被归一化为 (0,0,595,841)。必须从 page->obj 字典直接读取原始值，
            // 否则 CropBox/MediaBox 偏移计算全部错误。
            fz_rect rawMediaBox = pageBox;
            {
                pdf_obj *mb_arr = pdf_dict_get(ctx, page->obj, PDF_NAME(MediaBox));
                if (mb_arr && pdf_is_array(ctx, mb_arr) && pdf_array_len(ctx, mb_arr) >= 4) {
                    rawMediaBox.x0 = pdf_to_real(ctx, pdf_array_get(ctx, mb_arr, 0));
                    rawMediaBox.y0 = pdf_to_real(ctx, pdf_array_get(ctx, mb_arr, 1));
                    rawMediaBox.x1 = pdf_to_real(ctx, pdf_array_get(ctx, mb_arr, 2));
                    rawMediaBox.y1 = pdf_to_real(ctx, pdf_array_get(ctx, mb_arr, 3));
                }
                dbgLog("[PdfTextLayer] raw MediaBox: (%.2f,%.2f)-(%.2f,%.2f)", rawMediaBox.x0, rawMediaBox.y0, rawMediaBox.x1, rawMediaBox.y1);
            }
            pdfLog("[PdfTextLayer] page %d: %.0fx%.0f, crop=(%.0f,%.0f)-(%.0f,%.0f)", pi, pageWpt, pageH,
                   cropBox.x0, cropBox.y0, cropBox.x1, cropBox.y1);

            // 渲染页面为灰度像素（用于投影分析）
            std::vector<unsigned char> pageGray;
            int grayW = 0, grayH = 0;
            renderPageGrayscale(ctx, (fz_document*)pdf, pi, safeDpi,
                                pageGray, grayW, grayH);
            pdfLog("[PdfTextLayer] page %d: rendered %dx%d for projection", pi, grayW, grayH);

            fz_rect textBox = {0, 0, pageWpt, pageH};
            pdf_obj *textRes = NULL;
            fz_buffer *textBuf = NULL;
            fz_device *dev = pdf_page_write(ctx, pdf, textBox, &textRes, &textBuf);

            // cropOffset: 使用原始 MediaBox 值来正确补偿坐标偏移
            float cropX0 = rawMediaBox.x0;
            float cropY1 = rawMediaBox.y1;
            drawInvisibleText(ctx, dev, font, blocks, pageH, fz_device_rgb(ctx), dpiScale,
                              pageGray.data(), grayW, grayH, cropX0, cropY1);

            fz_close_device(ctx, dev);
            fz_drop_device(ctx, dev);

            injectRenderMode3(ctx, textBuf);

            pdf_obj *textStream = pdf_add_stream(ctx, pdf, textBuf, NULL, 0);
            pdf_obj *pageObj = page->obj;
            pdf_obj *contents = pdf_dict_get(ctx, pageObj, pdf_new_name(ctx, "Contents"));

            if (pdf_is_array(ctx, contents)) {
                pdf_array_push_drop(ctx, contents, textStream);
            } else {
                pdf_obj *arr = pdf_new_array(ctx, pdf, 2);
                pdf_array_push_drop(ctx, arr, contents);
                pdf_array_push_drop(ctx, arr, textStream);
                pdf_dict_put(ctx, pageObj, pdf_new_name(ctx, "Contents"), arr);
            }

            // 合并字体资源
            if (textRes) {
                pdf_obj *pageRes = pdf_dict_get(ctx, pageObj, pdf_new_name(ctx, "Resources"));
                if (pageRes) {
                    pdf_obj *srcFontDict = pdf_dict_get(ctx, textRes, pdf_new_name(ctx, "Font"));
                    if (srcFontDict) {
                        pdf_obj *dstFontDict = pdf_dict_get(ctx, pageRes, pdf_new_name(ctx, "Font"));
                        if (!dstFontDict) {
                            pdf_dict_put(ctx, pageRes, pdf_new_name(ctx, "Font"), srcFontDict);
                        } else {
                            int n = pdf_dict_len(ctx, srcFontDict);
                            for (int fi = 0; fi < n; fi++) {
                                pdf_obj *key = pdf_dict_get_key(ctx, srcFontDict, fi);
                                pdf_obj *val = pdf_dict_get_val(ctx, srcFontDict, fi);
                                if (!pdf_dict_get(ctx, dstFontDict, key)) {
                                    pdf_dict_put(ctx, dstFontDict, key, val);
                                }
                            }
                        }
                    }
                }
            }

            fz_drop_buffer(ctx, textBuf);
            pdf_drop_page(ctx, page);
        }

        fz_drop_font(ctx, font);

        // 保存: 先写临时文件, 再用 pdf_clean_file 做字体子集化
        QString tmpPath = dstPdfPath + ".tmp.pdf";
        QString cleanPath = dstPdfPath + ".clean.pdf";
        QByteArray tmpUtf8 = tmpPath.toUtf8();
        QByteArray cleanUtf8 = cleanPath.toUtf8();

        // 第一步: 写入未优化的临时文件
        fz_buffer *foutbuf = fz_new_buffer(ctx, 64 << 20);
        fz_output *fout = fz_new_output_with_buffer(ctx, foutbuf);
        pdf_write_options opts;
        memset(&opts, 0, sizeof(opts));
        pdf_write_document(ctx, pdf, fout, &opts);
        fz_close_output(ctx, fout);
        fz_drop_output(ctx, fout);

        unsigned char *data = NULL;
        size_t len = fz_buffer_storage(ctx, foutbuf, &data);
        pdfLog("[PdfTextLayer] raw buffer: %zu bytes", len);

        fz_output *fileOut = fz_new_output_with_path(ctx, tmpUtf8.constData(), 0);
        if (fileOut) {
            fz_write_data(ctx, fileOut, data, len);
            fz_close_output(ctx, fileOut);
            fz_drop_output(ctx, fileOut);
        }
        fz_drop_buffer(ctx, foutbuf);

        // 第二步: 用 pdf_clean_file 做字体子集化 + 压缩
        fz_try(ctx) {
            pdf_clean_options cleanOpts;
            memset(&cleanOpts, 0, sizeof(cleanOpts));
            cleanOpts.write.do_compress = 1;
            cleanOpts.write.do_compress_fonts = 1;
            cleanOpts.subset_fonts = 1;

            pdf_clean_file(ctx, (char*)tmpUtf8.constData(), (char*)cleanUtf8.constData(),
                           NULL, &cleanOpts, 0, NULL);
            pdfLog("[PdfTextLayer] pdf_clean_file done (subset_fonts=1)");
        } fz_catch(ctx) {
            pdfLog("[PdfTextLayer] pdf_clean_file failed: %s, using raw file",
                   fz_caught_message(ctx));
            // fallback: use the raw file
            cleanPath = tmpPath;
            cleanUtf8 = tmpUtf8;
        }

        // 第三步: rename 到最终路径
        QFile::remove(dstPdfPath);
        QFile cleanFile(cleanPath);
        if (cleanFile.rename(dstPdfPath)) {
            pdfLog("[PdfTextLayer] saved OK (subset fonts)");
        } else {
            pdfLog("[PdfTextLayer] rename failed, file at: %s", cleanUtf8.constData());
        }
        // 清理临时文件
        if (cleanPath != tmpPath) {
            QFile::remove(tmpPath);
        }

        pdf_drop_document(ctx, pdf);
    } fz_catch(ctx) {
        pdfLog("[PdfTextLayer] EXCEPTION: %s", fz_caught_message(ctx));
        ok = false;
    }

    fz_drop_context(ctx);
    pdfLog("[PdfTextLayer] modifyPdfWithOcrText END, ok=%d", ok);
    return ok;
}
