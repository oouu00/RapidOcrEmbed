#ifndef PDFHELPER_H
#define PDFHELPER_H

#include <QString>
#include <QPixmap>
#include <QVector>
#include <QByteArray>

extern "C" {
#include "mupdf/fitz.h"
}

class PdfHelper {
public:
    static bool initLibrary();
    static void destroyLibrary();

    static bool load(const QString& filePath);
    static int pageCount();
    static QPixmap renderPage(int pageIndex, int dpi = 150);
    static QVector<QPixmap> renderAllPages(int dpi = 150);
    static void close();

    static bool isPdfFile(const QString& filePath);
    static bool isInitialized();

private:
    static fz_context* m_ctx;
    static fz_document* m_doc;
    static bool m_initialized;
};

#endif // PDFHELPER_H
