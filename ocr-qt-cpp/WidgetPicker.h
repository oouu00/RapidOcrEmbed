#ifndef WIDGETPICKER_H
#define WIDGETPICKER_H

#include <QDialog>
#include <QLabel>
#include <QTimer>
#include <windows.h>

class WidgetPicker : public QDialog {
    Q_OBJECT

public:
    explicit WidgetPicker(const QString& mode);
    ~WidgetPicker();

signals:
    void widgetSelected(HWND hwnd, QString title, QRect rect);
    void cancelled();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void checkMousePosition();

private:
    void onMouseClick(int x, int y);
    HWND getChildHwnd(HWND hwnd, int x, int y);
    HWND getTopWindow(HWND hwnd);
    void updateHighlight(HWND hwnd);

    QString m_mode;
    QLabel* m_highlightLabel;
    QLabel* m_infoLabel;

    HWND m_currentHwnd;
    HWND m_lastHwnd;
    RECT m_currentRect;
    qint64 m_lastStableTime;
};

#endif // WIDGETPICKER_H