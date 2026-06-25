#ifndef PDFTEXTLAYER_H
#define PDFTEXTLAYER_H

#include <QByteArray>
#include <QVector>
#include <QString>
#include <utility>
#include <vector>
#include "OCRWrapper.h"

namespace PdfTextLayer {

bool buildSearchablePdf(QByteArray& out,
    const QVector<QPair<QByteArray, std::vector<OcrBlock>>>& pages);

bool modifyPdfWithOcrText(const QString& srcPdfPath,
    const QString& dstPdfPath,
    const QVector<QPair<int, std::vector<OcrBlock>>>& pageBlocks,
    int dpi = 150);

}

#endif
