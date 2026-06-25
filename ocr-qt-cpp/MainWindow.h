#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QSettings>
#include <QPixmap>
#include <QTimer>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QResizeEvent>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QCloseEvent>
#include "OCRWrapper.h"
#include "ScreenshotDialog.h"
#include "WidgetPicker.h"
#include "TbpuLayout.h"
#include "PdfHelper.h"

class OcrHttpServer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // skipOcr=true 时跳过 OCR 引擎初始化 (HTTP server 模式复用 server 的 OCR, 避免双实例)
    explicit MainWindow(QWidget* parent = nullptr, bool skipOcr = false);
    ~MainWindow();

private slots:
    void onScreenshotClicked();
    void onFileClicked();
    void onClearClicked();
    void onPinClicked();
    void onHotkeyChanged(int index);
    void onAutoSaveChanged(int state);
    void onAutoCopyChanged(int state);
    void onCoordModeChanged(int state);
    void onLayoutMenuTriggered(QAction* action);  // 排版策略菜单选择
    void onResultTextContextMenu(const QPoint& pos);  // 结果文本框自定义右键菜单

    void onScreenshotClosed(QPixmap pixmap);
    void onWidgetSelected(HWND hwnd, QString title, QRect rect);
    void onWidgetPickerCancelled();
    void onNetClicked();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
    bool eventFilter(QObject* watched, QEvent* event) override;  // 事件过滤器 (处理粘贴)
    void dragEnterEvent(QDragEnterEvent* event) override;  // 拖放进入事件
    void dropEvent(QDropEvent* event) override;  // 拖放放下事件

private:
    void initUI();
    void initOCR();
    void setupHotkey();
    bool parseHotkey(const QString& combo, UINT& mod, UINT& vk);
    void unregisterHotkey();
    void loadSettings();
    void saveSettings();
    void doScreenshot(const QString& mode);
    void processImage(const QPixmap& pixmap);
    void processPdfFile(const QString& filePath);  // 处理单个 PDF 文件
    void processFiles(const QStringList& files);    // 批量处理多个文件（图片+PDF混合）
    void showResult(const QString& text);
    void showResultHtml(const QString& html, const QString& plainText);
    void showResultWithCoords(const std::vector<OcrBlock>& blocks);
    void saveToHistory(const QPixmap& pixmap, const QString& text);
    void saveToHistoryHtml(const QPixmap& pixmap, const QString& html);
    void saveToHistoryWithPrefix(const QPixmap& pixmap, const QString& text, const QString& prefix);  // 带前缀保存
    void copyToClipboard(const QString& text);
    void copyHtmlToClipboard(const QString& html);
    void toggleAlwaysOnTop();
    void applyScaledStyle();
    void showNetSettingsDialog();
    void showAdvancedSettingsDialog();  // 高级设置弹窗
    void startHttpServer(quint16 port);
    void stopHttpServer();
    void setupTrayIcon();            // 启动时立即创建并显示托盘图标 (参考自动加密录音机)
    void minimizeToTray();          // 隐藏主窗口并显示托盘提示
    void resetOcrIdleTimer();       // 重置OCR空闲计时器 (每次OCR使用时调用)

    // UI 组件
    QPushButton* m_screenshotBtn;
    QPushButton* m_screenshotDropdown;
    QPushButton* m_fileBtn;
    QPushButton* m_clearBtn;
    QPushButton* m_pinBtn;
    QPushButton* m_netBtn;  // 网络服务图标
    QPushButton* m_autoSaveBtn;  // 自动保存图标按钮
    QPushButton* m_autoCopyBtn;  // 自动复制图标按钮
    QPushButton* m_coordBtn;     // 坐标模式图标按钮
    QPushButton* m_tableBtn;     // 表格模式开关按钮
    QPushButton* m_advancedBtn;  // 高级设置按钮
    QPushButton* m_layoutBtn;    // 排版模式状态切换按钮
    QTextEdit* m_resultText;
    QLabel* m_ocrStatusLabel;
    QComboBox* m_hotkeyCombo;
    QMenu* m_layoutMenu;       // 排版策略菜单

    QMenu* m_screenshotMenu;

    // OCR
    OCRWrapper* m_ocr;

    // HTTP 服务 (网络功能, 由右下角图标控制开关)
    OcrHttpServer* m_httpServer;

    // 截图对话框
    ScreenshotDialog* m_screenshotDialog;
    WidgetPicker* m_widgetPicker;

    // 系统托盘
    QSystemTrayIcon* m_trayIcon;
    bool m_forceQuit;
    bool m_minimizedToTray;  // 是否已最小化到托盘 (快捷键截图时保持隐藏)
    bool m_ocrRunning;  // OCR 是否正在运行 (用于超时和取消)
    QTimer* m_ocrTimeoutTimer;  // OCR超时定时器 (可取消)
    QTimer* m_ocrIdleTimer;     // OCR空闲卸载定时器 (2分钟无使用自动释放模型)

    // 状态
    QString m_screenshotMode;
    QPixmap m_currentPixmap;
    bool m_isAlwaysOnTop;
    QSettings* m_settings;

    // 设置
    QString m_hotkey;
    bool m_autoSave;
    QString m_autoSavePath;
    bool m_autoCopy;
    bool m_coordMode;
    bool m_doAngle;    // 自动旋转开关
    bool m_layoutMode;
    int m_tableMode;   // 表格模式: 0=关, 1=SLANet(ONNX), 2=img2table(纯OpenCV)
    std::string m_layoutStrategy;
    QString m_lastHtml;  // 最后一次表格HTML结果 (用于右键"另存为XLSX")


    // 全局热键 (RegisterHotKey)
    bool m_hotkeyRegistered;  // 当前是否已注册

    // 自适应缩放基线尺寸 (与初始窗口尺寸匹配)
    const int m_baseWidth = 400;
    const int m_baseHeight = 520;
    qreal m_scale = 1.0;
    int m_baseAppFontPx = 13;  // 应用默认字体像素大小基线
};

#endif // MAINWINDOW_H