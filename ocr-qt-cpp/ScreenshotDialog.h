#ifndef SCREENSHOTDIALOG_H
#define SCREENSHOTDIALOG_H

#include <QWidget>
#include <QPixmap>
#include <QRect>
#include <QPoint>
#include <QPainterPath>
#include <QMouseEvent>
#include <QPainter>

class ScreenshotDialog : public QWidget {
    Q_OBJECT

public:
    explicit ScreenshotDialog(const QPixmap& pixmap, bool freeForm = false);
    ~ScreenshotDialog();

signals:
    void closed(QPixmap croppedPixmap);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void completeSelection();

    QPixmap m_originalPixmap;
    QPoint m_startPos;
    QPoint m_endPos;
    QRect m_selectedRect;
    QPainterPath m_freeFormPath;
    QList<QPoint> m_pathPoints;
    bool m_freeForm;
    bool m_explicitlyClosed;
};

#endif // SCREENSHOTDIALOG_H