// TextClick 引擎: 程序化截图 + OCR + 精确坐标定位 + 鼠标/剪贴板
// 移植自 TextClick/src/TextClick.cpp 与 TextClick/src/main.cpp 的核心逻辑,
// 改为复用主程序的 OCRWrapper (C API) 与 Qt 截图能力。
#define NOMINMAX
#include "TextClickEngine.h"

#include <Windows.h>
#include <QGuiApplication>
#include <QScreen>
#include <QPixmap>
#include <QImage>
#include <QBuffer>
#include <QPainter>
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <cstring>
#include <iostream>

// ============================================================
// 构造 / 状态
// ============================================================
TextClickEngine::TextClickEngine(OCRWrapper* ocr)
    : m_ocr(ocr), m_lastImageWidth(0), m_lastImageHeight(0)
{
}

void TextClickEngine::setBlocks(const std::vector<OcrBlock>& blocks) {
    m_blocks = blocks;
}

// ============================================================
// 静态工具: UTF-8 与坐标计算
// (逻辑完全移植自 TextClick/src/main.cpp, 保持行为一致)
// ============================================================
int TextClickEngine::utf8CharCount(const std::string& str) {
    int count = 0;
    for (size_t i = 0; i < str.length(); i++) {
        if ((str[i] & 0xC0) != 0x80) count++;
    }
    return count;
}

int TextClickEngine::utf8CharToByteOffset(const std::string& str, int charIndex) {
    int count = 0;
    for (size_t i = 0; i < str.length(); i++) {
        if ((str[i] & 0xC0) != 0x80) {
            if (count == charIndex) return (int)i;
            count++;
        }
    }
    return (int)str.length();
}

TextClickEngine::Location TextClickEngine::parseLocation(const std::string& pos) {
    // 用 QString 比较, 避免 UTF-8 字节字面量硬编码出错 (源码已 /utf-8 编译)
    QString s = QString::fromUtf8(pos.c_str());
    if (s == "center" || s == QString::fromUtf8("中心")) return Location::Center;
    if (s == "topleft" || s == QString::fromUtf8("左上")) return Location::TopLeft;
    if (s == "topright" || s == QString::fromUtf8("右上")) return Location::TopRight;
    if (s == "bottomleft" || s == QString::fromUtf8("左下")) return Location::BottomLeft;
    if (s == "bottomright" || s == QString::fromUtf8("右下")) return Location::BottomRight;
    return Location::Center;
}

// 整块 box 取四角/中心 (TextClick: box[0,1]=左上 [2,3]=右上 [4,5]=右下 [6,7]=左下)
void TextClickEngine::getPointFromBox(const int box[8], Location loc, int& x, int& y) {
    switch (loc) {
        case Location::TopLeft:      x = box[0]; y = box[1]; break;
        case Location::TopRight:     x = box[2]; y = box[3]; break;
        case Location::BottomLeft:   x = box[6]; y = box[7]; break;
        case Location::BottomRight:  x = box[4]; y = box[5]; break;
        case Location::Center:
        default:
            x = (box[0] + box[2] + box[4] + box[6]) / 4;
            y = (box[1] + box[3] + box[5] + box[7]) / 4;
            break;
    }
}

// 子串局部坐标: 按加权字符比例在文字框内偏移 (UTF-8)
// charStart/charEnd 为子串在 fullText 中的字符索引 (0-based, end 闭区间)
// CJK 字符宽度约 1.0, ASCII 约 0.55, 其他约 0.75
void TextClickEngine::getPointFromBoxPartial(const int box[8], Location loc,
                                             const std::string& fullText,
                                             int charStart, int charEnd,
                                             int& x, int& y) {
    int totalChars = utf8CharCount(fullText);
    if (totalChars == 0) {
        x = (box[0] + box[2] + box[4] + box[6]) / 4;
        y = (box[1] + box[3] + box[5] + box[7]) / 4;
        return;
    }

    float startRatio = getWeightedCharRatio(fullText, charStart);
    float endRatio = getWeightedCharRatio(fullText, charEnd + 1);

    int p1x = box[0], p1y = box[1];
    int p2x = box[2], p2y = box[3];
    int p3x = box[4], p3y = box[5];
    int p4x = box[6], p4y = box[7];

    float midRatio = (startRatio + endRatio) / 2.0f;

    switch (loc) {
        case Location::TopLeft:
            x = p1x + (int)(startRatio * (p2x - p1x));
            y = p1y;
            break;
        case Location::TopRight:
            x = p1x + (int)(endRatio * (p2x - p1x));
            y = p2y;
            break;
        case Location::BottomLeft:
            x = p4x + (int)(startRatio * (p3x - p4x));
            y = p4y;
            break;
        case Location::BottomRight:
            x = p4x + (int)(endRatio * (p3x - p4x));
            y = p3y;
            break;
        case Location::Center:
        default:
            x = p1x + (int)(midRatio * (p2x - p1x));
            y = (p1y + p4y) / 2;
            break;
    }
}

// ============================================================
// 垂直投影切分字符
// ============================================================
std::vector<std::pair<int,int>> TextClickEngine::projectCharSegments(
    const unsigned char* gray, int imgW, int imgH,
    const int box[8])
{
    std::vector<std::pair<int,int>> result;
    if (!gray || imgW <= 0 || imgH <= 0) return result;

    int xs[4] = {box[0], box[2], box[4], box[6]};
    int ys[4] = {box[1], box[3], box[5], box[7]};
    int left = std::max(0, *std::min_element(xs, xs + 4));
    int right = std::min(imgW - 1, *std::max_element(xs, xs + 4));
    int top = std::max(0, *std::min_element(ys, ys + 4));
    int bottom = std::min(imgH - 1, *std::max_element(ys, ys + 4));

    int regionW = right - left + 1;
    int regionH = bottom - top + 1;
    if (regionW <= 0 || regionH <= 0) return result;

    int charHeight = regionH;
    int minGap = std::max(4, (int)(charHeight * 0.15));
    int minCharW = std::max(3, (int)(charHeight * 0.08));

    std::vector<int> projection(regionW, 0);
    for (int x = 0; x < regionW; x++) {
        int sum = 0;
        for (int y = 0; y < regionH; y++) {
            if (gray[(top + y) * imgW + (left + x)] < 200) sum++;
        }
        projection[x] = sum;
    }

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

    for (const auto& seg : merged) {
        if (seg.end - seg.start >= minCharW) {
            result.push_back({left + seg.start, left + seg.end});
        }
    }
    return result;
}

// ============================================================
// 加权字符比例 (按字符类型估算宽度)
// ============================================================
float TextClickEngine::getWeightedCharRatio(const std::string& text, int charIndex) {
    float totalWeight = 0;
    float prefixWeight = 0;
    int charIdx = 0;
    for (size_t i = 0; i < text.length(); ) {
        unsigned char fb = (unsigned char)text[i];
        int cbl = 1;
        if ((fb & 0x80) == 0x00) cbl = 1;
        else if ((fb & 0xE0) == 0xC0) cbl = 2;
        else if ((fb & 0xF0) == 0xE0) cbl = 3;
        else if ((fb & 0xF8) == 0xF0) cbl = 4;

        uint32_t cp = 0;
        if (cbl == 1) cp = fb;
        else if (cbl == 2) cp = ((fb & 0x1F) << 6) | ((unsigned char)text[i+1] & 0x3F);
        else if (cbl == 3) cp = ((fb & 0x0F) << 12) | (((unsigned char)text[i+1] & 0x3F) << 6) | ((unsigned char)text[i+2] & 0x3F);
        else if (cbl == 4) cp = ((fb & 0x07) << 18) | (((unsigned char)text[i+1] & 0x3F) << 12) | (((unsigned char)text[i+2] & 0x3F) << 6) | ((unsigned char)text[i+3] & 0x3F);

        float w = 0.75f;
        if (cp >= 0x4E00 && cp <= 0x9FFF) w = 1.0f;
        else if (cp >= 0x3400 && cp <= 0x4DBF) w = 1.0f;
        else if (cp >= 0x3000 && cp <= 0x303F) w = 1.0f;
        else if (cp >= 0xFF00 && cp <= 0xFFEF) w = 1.0f;
        else if (cp >= 0x0020 && cp <= 0x007E) w = 0.55f;
        else if (cp >= 0xAC00 && cp <= 0xD7AF) w = 1.0f;
        else if (cp >= 0x3040 && cp <= 0x309F) w = 1.0f;
        else if (cp >= 0x30A0 && cp <= 0x30FF) w = 1.0f;

        totalWeight += w;
        if (charIdx < charIndex) prefixWeight += w;
        charIdx++;
        i += cbl;
    }
    if (totalWeight == 0) return 0;
    return prefixWeight / totalWeight;
}

std::string TextClickEngine::getCoordFromBox(const int box[8], Location loc) {
    int x, y;
    getPointFromBox(box, loc, x, y);
    return std::to_string(x) + "," + std::to_string(y);
}

std::string TextClickEngine::getCoordFromBoxPartial(const int box[8], Location loc,
                                                    const std::string& fullText,
                                                    int charStart, int charEnd) {
    int x, y;
    getPointFromBoxPartial(box, loc, fullText, charStart, charEnd, x, y);
    return std::to_string(x) + "," + std::to_string(y);
}

// ============================================================
// 鼠标 / 剪贴板 (移植自 TextClick, Win32 API)
// ============================================================
void TextClickEngine::moveTo(int x, int y) {
    SetCursorPos(x, y);
}

void TextClickEngine::click() {
    INPUT inputs[2];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, inputs, sizeof(INPUT));
}

void TextClickEngine::doubleClick() {
    INPUT inputs[4];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].type = INPUT_MOUSE; inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].type = INPUT_MOUSE; inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    inputs[2].type = INPUT_MOUSE; inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[3].type = INPUT_MOUSE; inputs[3].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(4, inputs, sizeof(INPUT));
}

void TextClickEngine::rightClick() {
    INPUT inputs[2];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    SendInput(2, inputs, sizeof(INPUT));
}

bool TextClickEngine::performMouseAction(MouseAction act, int x, int y) {
    moveTo(x, y);
    switch (act) {
        case MouseAction::Click:       click(); break;
        case MouseAction::DoubleClick: doubleClick(); break;
        case MouseAction::RightClick:  rightClick(); break;
        case MouseAction::Move:        break;  // 已 moveTo
    }
    return true;
}

bool TextClickEngine::setClipboardText(const std::string& text) {
    if (!OpenClipboard(NULL)) return false;
    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.length() + 1);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    memcpy(GlobalLock(hMem), text.c_str(), text.length() + 1);
    GlobalUnlock(hMem);
    SetClipboardData(CF_TEXT, hMem);
    CloseClipboard();
    return true;
}

std::string TextClickEngine::getClipboardText() {
    if (!OpenClipboard(NULL)) return "";
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) {
        CloseClipboard();
        return "";
    }
    char* pszText = (char*)GlobalLock(hData);
    std::string text(pszText ? pszText : "");
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}

// ============================================================
// 截图 / OCR
// ============================================================

// Win32: 截取当前活动窗口所在的屏幕 (支持多显示器)
static QImage captureCurrentMonitor() {
    // 设置 DPI 感知, 获取真实像素坐标
    DPI_AWARENESS_CONTEXT hDpiCtx = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!hDpiCtx) hDpiCtx = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

    HWND hForeground = GetForegroundWindow();
    HMONITOR hMonitor = MonitorFromWindow(hForeground, MONITOR_DEFAULTTONEAREST);

    MONITORINFOEX mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(hMonitor, &mi);

    int monW = mi.rcMonitor.right - mi.rcMonitor.left;
    int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;

    HDC hScreenDC = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, monW, monH);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBitmap);
    BitBlt(hMemDC, 0, 0, monW, monH, hScreenDC, mi.rcMonitor.left, mi.rcMonitor.top, SRCCOPY);
    SelectObject(hMemDC, hOldBmp);

    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = monW;
    bi.biHeight = -monH;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    QImage img(monW, monH, QImage::Format_ARGB32);
    GetDIBits(hMemDC, hBitmap, 0, monH, img.bits(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
    if (hDpiCtx) SetThreadDpiAwarenessContext(hDpiCtx);

    return img;
}

// 把 QPixmap 存为 BMP 字节流 (供 OCR 与标注用)
// 复用 CliRunner / OcrHttpServer 的转 BMP 做法 (绕过 libjpeg 符号冲突)
static bool pixmapToBmp(const QPixmap& pm, std::vector<unsigned char>& outBmp) {
    QPixmap processed = pm;
    if (pm.hasAlpha()) {
        processed = QPixmap(pm.size());
        processed.fill(Qt::white);
        QPainter p(&processed);
        p.drawPixmap(0, 0, pm);
        p.end();
    }
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    if (!processed.save(&buf, "BMP")) return false;
    buf.close();
    outBmp.assign(ba.begin(), ba.end());
    return true;
}

int TextClickEngine::detectScreen(int x, int y, int width, int height) {
    if (!m_ocr || !m_ocr->isInitialized()) return -1;

    QImage img;
    if (width <= 0 || height <= 0) {
        // 全屏: Win32 截取当前活动窗口所在的屏幕 (支持多显示器)
        img = captureCurrentMonitor();
    } else {
        // 区域: 先截全屏再裁剪
        QImage fullImg = captureCurrentMonitor();
        if (fullImg.isNull()) return -1;
        img = fullImg.copy(x, y, width, height);
    }
    if (img.isNull()) return -1;

    m_lastImageWidth = img.width();
    m_lastImageHeight = img.height();

    // 转灰度 (供垂直投影切分)
    QImage grayImg = img.convertToFormat(QImage::Format_Grayscale8);
    m_lastImageGrayW = grayImg.width();
    m_lastImageGrayH = grayImg.height();
    m_lastImageGray.assign(grayImg.bits(), grayImg.bits() + m_lastImageGrayW * m_lastImageGrayH);

    // 转 BMP (供 OCR)
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "BMP");
    buf.close();
    m_lastImageBmp.assign(ba.begin(), ba.end());

    m_blocks = m_ocr->detectWithCoords(m_lastImageBmp);
    return (int)m_blocks.size();
}

int TextClickEngine::detectImage(const std::string& imagePath) {
    if (!m_ocr || !m_ocr->isInitialized()) return -1;
    if (imagePath.empty()) return -1;

    // 复用 CliRunner 的 imageToBmpBytes 思路: 用 QImage 加载, 填白底, 存 BMP
    QString qpath = QString::fromStdString(imagePath);
    QImage img;
    if (!img.load(qpath)) return -1;
    if (img.hasAlphaChannel()) {
        QImage filled(img.size(), QImage::Format_RGB32);
        filled.fill(Qt::white);
        QPainter p(&filled);
        p.drawImage(0, 0, img);
        p.end();
        img = filled;
    }
    m_lastImageWidth = img.width();
    m_lastImageHeight = img.height();

    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "BMP");
    buf.close();
    m_lastImageBmp.assign(ba.begin(), ba.end());

    // 转灰度 (供垂直投影切分)
    QImage grayImg = img.convertToFormat(QImage::Format_Grayscale8);
    m_lastImageGrayW = grayImg.width();
    m_lastImageGrayH = grayImg.height();
    m_lastImageGray.assign(grayImg.bits(), grayImg.bits() + m_lastImageGrayW * m_lastImageGrayH);

    m_blocks = m_ocr->detectWithCoords(m_lastImageBmp);
    return (int)m_blocks.size();
}

bool TextClickEngine::saveResultImage(const std::string& outputPath) const {
    if (m_lastImageBmp.empty()) return false;
    QImage baseImg;
    if (!baseImg.loadFromData(m_lastImageBmp.data(), (int)m_lastImageBmp.size())) return false;

    QImage out = baseImg.convertToFormat(QImage::Format_RGB32);
    QPainter p(&out);
    QPen pen(QColor(0, 255, 0));
    pen.setWidth(2);
    p.setPen(pen);

    for (const auto& b : m_blocks) {
        if (b.box.size() < 8) continue;
        QPolygon poly;
        poly << QPoint(b.box[0], b.box[1]) << QPoint(b.box[2], b.box[3])
             << QPoint(b.box[4], b.box[5]) << QPoint(b.box[6], b.box[7]);
        p.drawPolygon(poly);
        // 文字标注
        int tx = b.box[0];
        int ty = b.box[1] - 10;
        if (b.box[1] < 20) ty = b.box[3] + 20;
        p.setPen(QPen(QColor(0, 0, 255)));
        p.drawText(tx, ty, QString::fromStdString(b.text));
        p.setPen(pen);
    }
    p.end();

    return out.save(QString::fromStdString(outputPath));
}

// ============================================================
// 结果访问
// ============================================================
std::string TextClickEngine::getBlockText(int index) const {
    if (index < 0 || index >= (int)m_blocks.size()) return "";
    return m_blocks[index].text;
}

void TextClickEngine::getBlockBox(int index, int box[8]) const {
    for (int i = 0; i < 8; i++) box[i] = 0;
    if (index < 0 || index >= (int)m_blocks.size()) return;
    const auto& b = m_blocks[index].box;
    for (int i = 0; i < 8 && i < (int)b.size(); i++) box[i] = b[i];
}

float TextClickEngine::getBlockScore(int index) const {
    if (index < 0 || index >= (int)m_blocks.size()) return 0.0f;
    return m_blocks[index].score;
}

std::string TextClickEngine::getAllText() const {
    std::string result;
    for (size_t i = 0; i < m_blocks.size(); i++) {
        if (i > 0) result += "\n";
        result += m_blocks[i].text;
    }
    return result;
}

// 查找第 occurrence 个包含 text 的文字块, 算出局部精确坐标
// 优先使用垂直投影切分, 字符数不匹配时回退加权插值
bool TextClickEngine::findPoint(const std::string& text, int occurrence,
                                Location loc, int& outX, int& outY) const {
    if (occurrence < 1) occurrence = 1;

    std::string textLower = text;
    for (size_t j = 0; j < textLower.length(); j++) textLower[j] = (char)tolower((unsigned char)textLower[j]);

    for (const auto& b : m_blocks) {
        std::string blockLower = b.text;
        for (size_t j = 0; j < blockLower.length(); j++) blockLower[j] = (char)tolower((unsigned char)blockLower[j]);

        size_t foundPos = blockLower.find(textLower);
        if (foundPos == std::string::npos) continue;

        if (--occurrence == 0) {
            if (b.box.size() < 8) return false;
            int box[8];
            for (int i = 0; i < 8; i++) box[i] = b.box[i];

            int charStart = utf8CharCount(b.text.substr(0, foundPos));
            int charEnd = charStart + utf8CharCount(text) - 1;

            // 优先: 垂直投影切分
            bool usedProjection = false;
            if (!m_lastImageGray.empty() && m_lastImageGrayW > 0 && m_lastImageGrayH > 0) {
                auto segments = projectCharSegments(
                    m_lastImageGray.data(), m_lastImageGrayW, m_lastImageGrayH, box);
                int totalChars = utf8CharCount(b.text);
                if (!segments.empty() && (int)segments.size() == totalChars) {
                    int segIdx = (charStart + charEnd) / 2;
                    if (segIdx >= 0 && segIdx < (int)segments.size()) {
                        int cx = (segments[segIdx].first + segments[segIdx].second) / 2;
                        float xRatio = (box[2] != box[0])
                            ? (float)(cx - box[0]) / (float)(box[2] - box[0]) : 0.5f;
                        xRatio = std::max(0.0f, std::min(1.0f, xRatio));

                        switch (loc) {
                            case Location::TopLeft:
                                outX = box[0] + (int)(xRatio * (box[2] - box[0]));
                                outY = box[1];
                                break;
                            case Location::TopRight:
                                outX = box[0] + (int)(xRatio * (box[2] - box[0]));
                                outY = box[3];
                                break;
                            case Location::BottomLeft:
                                outX = box[6] + (int)(xRatio * (box[4] - box[6]));
                                outY = box[7];
                                break;
                            case Location::BottomRight:
                                outX = box[6] + (int)(xRatio * (box[4] - box[6]));
                                outY = box[5];
                                break;
                            case Location::Center:
                            default:
                                outX = box[0] + (int)(xRatio * (box[2] - box[0]));
                                outY = (box[1] + box[7]) / 2;
                                break;
                        }
                        usedProjection = true;
                    }
                }
            }

            // 回退: 加权插值
            if (!usedProjection) {
                getPointFromBoxPartial(box, loc, b.text, charStart, charEnd, outX, outY);
            }
            return true;
        }
    }
    return false;
}
