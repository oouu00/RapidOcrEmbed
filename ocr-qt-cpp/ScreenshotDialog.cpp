#include "ScreenshotDialog.h"
#include <QApplication>
#include <QScreen>
#include <QMessageBox>
#include <QCloseEvent>

ScreenshotDialog::ScreenshotDialog(const QPixmap& pixmap, bool freeForm)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_originalPixmap(pixmap)
    , m_selectedRect(0, 0, 0, 0)
    , m_freeForm(freeForm)
    , m_explicitlyClosed(false)
{
    setWindowTitle("截图识别 - 拖拽选择区域");
    setCursor(Qt::CrossCursor);

    // 确保覆盖整个屏幕
    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        setGeometry(screen->geometry());
    } else {
        resize(pixmap.size());
        move(0, 0);
    }
}

ScreenshotDialog::~ScreenshotDialog() {
}

void ScreenshotDialog::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.drawPixmap(0, 0, m_originalPixmap);

    if (m_freeForm) {
        // 任意形状模式
        if (!m_pathPoints.isEmpty()) {
            painter.setPen(QPen(QColor(129, 182, 230), 2));
            painter.setBrush(QColor(129, 182, 230, 50));

            QPainterPath path;
            if (m_pathPoints.size() > 0) {
                path.moveTo(m_pathPoints[0]);
                for (int i = 1; i < m_pathPoints.size(); i++) {
                    path.lineTo(m_pathPoints[i]);
                }
                path.closeSubpath();
                painter.drawPath(path);
            }
        }
    } else {
        // 矩形模式
        if (m_selectedRect.isValid()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 120, 215, 100));
            painter.drawRect(m_selectedRect);

            painter.setPen(QColor(0, 120, 215));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(m_selectedRect);
        }
    }

    painter.end();
}

void ScreenshotDialog::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_startPos = event->pos();
        m_endPos = event->pos();
        m_selectedRect = QRect();

        if (m_freeForm) {
            m_pathPoints.clear();
            m_pathPoints.append(event->pos());
        }

        update();
    } else if (event->button() == Qt::RightButton && m_freeForm) {
        completeSelection();
    }
}

void ScreenshotDialog::mouseMoveEvent(QMouseEvent* event) {
    if (m_freeForm) {
        m_pathPoints.append(event->pos());
        update();
    } else {
        if (!m_startPos.isNull()) {
            m_endPos = event->pos();
            int x1 = std::min(m_startPos.x(), m_endPos.x());
            int y1 = std::min(m_startPos.y(), m_endPos.y());
            int x2 = std::max(m_startPos.x(), m_endPos.x());
            int y2 = std::max(m_startPos.y(), m_endPos.y());
            m_selectedRect = QRect(x1, y1, x2 - x1, y2 - y1);
            update();
        }
    }
}

void ScreenshotDialog::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_freeForm) {
            if (m_pathPoints.size() >= 3) {
                completeSelection();
            }
        } else {
            if (m_selectedRect.width() > 10 && m_selectedRect.height() > 10) {
                QPixmap cropped = m_originalPixmap.copy(m_selectedRect);
                m_explicitlyClosed = true;
                emit closed(cropped);
                close();
            } else {
                m_startPos = QPoint();
                m_endPos = QPoint();
                m_selectedRect = QRect();
                update();
            }
        }
    }
}

void ScreenshotDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        m_explicitlyClosed = true;
        emit closed(QPixmap());
        close();
    } else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_freeForm) {
        completeSelection();
    }
}

void ScreenshotDialog::closeEvent(QCloseEvent* event) {
    if (!m_explicitlyClosed) {
        emit closed(QPixmap());
    }
    QWidget::closeEvent(event);
}

void ScreenshotDialog::completeSelection() {
    if (m_freeForm && m_pathPoints.size() >= 3) {
        QPainterPath path;
        path.moveTo(m_pathPoints[0]);
        for (int i = 1; i < m_pathPoints.size(); i++) {
            path.lineTo(m_pathPoints[i]);
        }
        path.closeSubpath();

        QRect boundingRect = path.boundingRect().toAlignedRect();
        if (!boundingRect.isValid() || boundingRect.width() < 10 || boundingRect.height() < 10) {
            QMessageBox::warning(this, "警告", "选择区域太小");
            return;
        }

        QImage resultImg(boundingRect.size(), QImage::Format_ARGB32);
        resultImg.fill(Qt::white);

        QPainter painter(&resultImg);
        painter.setClipPath(path.translated(-boundingRect.left(), -boundingRect.top()));
        painter.drawImage(-boundingRect.left(), -boundingRect.top(), m_originalPixmap.toImage());
        painter.end();

        QPixmap cropped = QPixmap::fromImage(resultImg);
        m_explicitlyClosed = true;
        emit closed(cropped);
        close();
    } else if (!m_freeForm && m_selectedRect.isValid()) {
        QPixmap cropped = m_originalPixmap.copy(m_selectedRect);
        m_explicitlyClosed = true;
        emit closed(cropped);
        close();
    } else {
        m_explicitlyClosed = true;
        emit closed(QPixmap());
        close();
    }
}