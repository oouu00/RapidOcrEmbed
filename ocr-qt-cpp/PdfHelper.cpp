#include "PdfHelper.h"
#include <QFileInfo>
#include <QFile>
#include <QImage>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>

extern "C" {
#include "mupdf/fitz.h"
#include "mupdf/pdf.h"
}

fz_context* PdfHelper::m_ctx = nullptr;
fz_document* PdfHelper::m_doc = nullptr;
bool PdfHelper::m_initialized = false;

bool PdfHelper::initLibrary() {
    if (m_initialized) return true;

    m_ctx = fz_new_context(NULL, NULL, 256 << 20);
    if (!m_ctx) {
        qWarning() << "[PdfHelper] failed to create fz_context";
        return false;
    }

    fz_register_document_handlers(m_ctx);
    m_initialized = true;
    qDebug() << "[PdfHelper] MuPDF initialized";
    return true;
}

void PdfHelper::destroyLibrary() {
    if (!m_initialized) return;
    close();
    if (m_ctx) {
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
    }
    m_initialized = false;
    qDebug() << "[PdfHelper] MuPDF destroyed";
}

bool PdfHelper::load(const QString& filePath) {
    qDebug() << "[PdfHelper] load:" << filePath;

    close();

    if (!initLibrary()) return false;

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        qWarning() << "[PdfHelper] file not found:" << filePath;
        return false;
    }

    QByteArray utf8 = filePath.toUtf8();
    fz_try(m_ctx) {
        m_doc = fz_open_document(m_ctx, utf8.constData());
    }
    fz_catch(m_ctx) {
        qWarning() << "[PdfHelper] failed to open:" << fz_caught_message(m_ctx);
        m_doc = nullptr;
        return false;
    }

    int pages = fz_count_pages(m_ctx, m_doc);
    qDebug() << "[PdfHelper] loaded, pages:" << pages;
    if (pages <= 0) {
        qWarning() << "[PdfHelper] PDF has no pages";
        fz_drop_document(m_ctx, m_doc);
        m_doc = nullptr;
        return false;
    }
    return true;
}

int PdfHelper::pageCount() {
    if (!m_doc) return 0;
    return fz_count_pages(m_ctx, m_doc);
}

QPixmap PdfHelper::renderPage(int pageIndex, int dpi) {
    if (!m_doc || !m_ctx) return QPixmap();

    int totalPages = fz_count_pages(m_ctx, m_doc);
    if (pageIndex < 0 || pageIndex >= totalPages) {
        qWarning() << "[PdfHelper] invalid page:" << pageIndex;
        return QPixmap();
    }

    fz_page *page = nullptr;
    fz_pixmap *pix = nullptr;
    QPixmap result;

    fz_try(m_ctx) {
        page = fz_load_page(m_ctx, m_doc, pageIndex);

        float zoom = dpi / 72.0f;
        fz_matrix ctm = fz_scale(zoom, zoom);

        pix = fz_new_pixmap_from_page(m_ctx, page, ctm, fz_device_rgb(m_ctx), 0);

        int w = pix->w;
        int h = pix->h;
        int n = pix->n;

        qDebug() << "[PdfHelper] pixmap: w=" << w << "h=" << h << "n=" << n << "stride=" << pix->stride;

        // MuPDF RGB (n=3) -> QImage Format_RGB32 (ARGB32, 0xffRRGGBB)
        QImage img(w, h, QImage::Format_RGB32);
        for (int y = 0; y < h; y++) {
            const uchar *src = pix->samples + y * pix->stride;
            QRgb *dst = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; x++) {
                uchar r = src[x * 3 + 0];
                uchar g = src[x * 3 + 1];
                uchar b = src[x * 3 + 2];
                dst[x] = qRgb(r, g, b);
            }
        }

        result = QPixmap::fromImage(img);
        qDebug() << "[PdfHelper] QPixmap:" << result.size() << "isNull=" << result.isNull();
    }
    fz_catch(m_ctx) {
        qWarning() << "[PdfHelper] render failed:" << fz_caught_message(m_ctx);
    }

    if (pix) fz_drop_pixmap(m_ctx, pix);
    if (page) fz_drop_page(m_ctx, page);

    return result;
}

QVector<QPixmap> PdfHelper::renderAllPages(int dpi) {
    QVector<QPixmap> pages;
    int count = pageCount();
    for (int i = 0; i < count; i++) {
        QPixmap pix = renderPage(i, dpi);
        if (!pix.isNull()) pages.append(pix);
    }
    return pages;
}

void PdfHelper::close() {
    if (m_doc && m_ctx) {
        fz_drop_document(m_ctx, m_doc);
        m_doc = nullptr;
    }
}

bool PdfHelper::isPdfFile(const QString& filePath) {
    return filePath.toLower().endsWith(".pdf");
}

bool PdfHelper::isInitialized() {
    return m_initialized;
}
