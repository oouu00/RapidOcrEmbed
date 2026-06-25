#include "WidgetPicker.h"
#include <QApplication>
#include <QScreen>
#include <QDebug>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDateTime>
#include <windows.h>

WidgetPicker::WidgetPicker(const QString& mode)
    : QDialog(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_mode(mode)
    , m_currentHwnd(nullptr)
    , m_lastHwnd(nullptr)
    , m_lastStableTime(0)
{
    QString title = (mode == "window") ? "窗口截图 - 点击选择窗口，ESC退出" : "控件截图 - 点击选择控件，ESC退出";
    setWindowTitle(title);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    // 关键：整个对话框对鼠标透明，点击事件通过 eventFilter 全局拦截
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setCursor(Qt::CrossCursor);

    // 覆盖整个屏幕
    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        setGeometry(screen->geometry());
    } else {
        setGeometry(0, 0, 1920, 1080);
    }

    QString infoText = (mode == "window") ? "点击选择窗口" : "点击选择控件";

    m_infoLabel = new QLabel(infoText, this);
    m_infoLabel->setStyleSheet(
        "QLabel { background-color: #E6EDFC; color: #333; padding: 10px 20px; border-radius: 5px; font-size: 16px; }"
    );
    m_infoLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_infoLabel->move(20, 20);
    m_infoLabel->show();

    m_highlightLabel = new QLabel(this);
    m_highlightLabel->setStyleSheet(
        "QLabel { border: 3px solid #81B6E6; background-color: rgba(129, 182, 230, 100); }"
    );
    m_highlightLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_highlightLabel->hide();

    // 安装全局事件过滤器，与 Python 版本一致
    QApplication::instance()->installEventFilter(this);

    show();
    raise();
    activateWindow();

    // 延迟 50ms 开始检测鼠标位置
    QTimer::singleShot(50, this, &WidgetPicker::checkMousePosition);
}

WidgetPicker::~WidgetPicker() {
    QApplication::instance()->removeEventFilter(this);
}

void WidgetPicker::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        emit cancelled();
        close();
    }
    QDialog::keyPressEvent(event);
}

bool WidgetPicker::eventFilter(QObject* obj, QEvent* event) {
    // 全局拦截鼠标点击事件，与 Python 版本一致
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            QPoint globalPos = mouseEvent->globalPos();
            onMouseClick(globalPos.x(), globalPos.y());
            return true;  // 拦截事件
        }
    }
    return QDialog::eventFilter(obj, event);
}

void WidgetPicker::onMouseClick(int x, int y) {
    // 先关闭自身，退出截图状态（与 Python pick_widget 返回后 dialog 已关闭一致）
    close();

    if (m_lastHwnd && m_lastHwnd != (HWND)winId()) {
        HWND finalHwnd = m_lastHwnd;

        // 获取窗口矩形
        RECT rect;
        GetWindowRect(finalHwnd, &rect);

        int left = rect.left;
        int top = rect.top;
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        // 获取窗口标题
        wchar_t titleBuf[256] = {0};
        GetWindowTextW(finalHwnd, titleBuf, 256);
        QString title = QString::fromWCharArray(titleBuf);

        emit widgetSelected(finalHwnd, title, QRect(left, top, width, height));
    } else {
        // 没有选中有效窗口，发送空信号
        emit widgetSelected(nullptr, QString(), QRect());
    }
}

void WidgetPicker::checkMousePosition() {
    POINT pt;
    GetCursorPos(&pt);

    int x = pt.x;
    int y = pt.y;

    HWND hwnd = WindowFromPoint(pt);

    if (hwnd && hwnd != (HWND)winId()) {
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

        if (hwnd != m_currentHwnd) {
            m_currentHwnd = hwnd;
            m_lastStableTime = currentTime;
        }

        // 鼠标在同一个窗口上停留超过 200ms 才更新高亮（防抖）
        if (currentTime - m_lastStableTime >= 200) {
            if (hwnd != m_lastHwnd) {
                m_lastHwnd = hwnd;

                HWND finalHwnd = hwnd;
                if (m_mode == "window") {
                    finalHwnd = getTopWindow(hwnd);
                } else {
                    finalHwnd = getChildHwnd(hwnd, x, y);
                }

                RECT rect;
                GetWindowRect(finalHwnd, &rect);

                int left = rect.left;
                int top = rect.top;
                int width = rect.right - rect.left;
                int height = rect.bottom - rect.top;

                m_currentRect = rect;

                if (width > 0 && height > 0) {
                    m_highlightLabel->setGeometry(left, top, width, height);
                    m_highlightLabel->show();
                    m_highlightLabel->repaint();

                    // 获取窗口标题
                    wchar_t titleBuf[256] = {0};
                    GetWindowTextW(finalHwnd, titleBuf, 256);
                    QString title = QString::fromWCharArray(titleBuf);

                    QString info = (m_mode == "window") ?
                        QString("窗口: %1 | 句柄: 0x%2").arg(title).arg((uintptr_t)hwnd, 0, 16) :
                        QString("控件: %1 | 句柄: 0x%2").arg(title).arg((uintptr_t)hwnd, 0, 16);
                    m_infoLabel->setText(info);
                }
            }
        }
    }

    // 每 16ms 检测一次（约 60fps），与 Python 版本一致
    QTimer::singleShot(16, this, &WidgetPicker::checkMousePosition);
}

HWND WidgetPicker::getChildHwnd(HWND hwnd, int x, int y) {
    QList<QPair<HWND, int>> candidates;

    // 遍历所有子窗口
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        RECT rect;
        if (GetWindowRect(child, &rect)) {
            if (rect.left <= x && x <= rect.right && rect.top <= y && y <= rect.bottom) {
                int area = (rect.right - rect.left) * (rect.bottom - rect.top);
                candidates.append({child, area});
                // 递归遍历子窗口的子窗口（与 Python 版本一致）
                HWND subChild = getChildHwnd(child, x, y);
                if (subChild != child) {
                    RECT subRect;
                    if (GetWindowRect(subChild, &subRect)) {
                        int subArea = (subRect.right - subRect.left) * (subRect.bottom - subRect.top);
                        candidates.append({subChild, subArea});
                    }
                }
            }
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }

    if (!candidates.isEmpty()) {
        // 按面积排序，选择最小的
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
        return candidates.first().first;
    }

    return hwnd;
}

HWND WidgetPicker::getTopWindow(HWND hwnd) {
    HWND parent = GetParent(hwnd);
    while (parent) {
        hwnd = parent;
        parent = GetParent(hwnd);
    }
    return hwnd;
}

void WidgetPicker::updateHighlight(HWND hwnd) {
    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
        int left = rect.left;
        int top = rect.top;
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        m_currentRect = rect;

        if (width > 0 && height > 0) {
            m_highlightLabel->setGeometry(left, top, width, height);
            m_highlightLabel->show();
            m_highlightLabel->repaint();

            // 获取窗口标题
            wchar_t titleBuf[256] = {0};
            GetWindowTextW(hwnd, titleBuf, 256);
            QString title = QString::fromWCharArray(titleBuf);

            QString info = (m_mode == "window") ?
                QString("窗口: %1 | 句柄: 0x%2").arg(title).arg((uintptr_t)hwnd, 0, 16) :
                QString("控件: %1 | 句柄: 0x%2").arg(title).arg((uintptr_t)hwnd, 0, 16);
            m_infoLabel->setText(info);
        }
    }
}
