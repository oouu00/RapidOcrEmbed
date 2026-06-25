#include "MainWindow.h"
#include "Icons.h"
#include "OcrHttpServer.h"
#include "PdfTextLayer.h"
#include "HtmlToXlsx.h"
#include "HtmlToXlsx.h"
#include "HtmlToXlsx.h"
#include <Windows.h>
#include <cstdio>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QBuffer>
#include <QPainter>
#include <QScreen>
#include <QAction>
#include <QStyle>
#include <QClipboard>
#include <QKeyEvent>
#include <QDir>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QFutureWatcher>
#include <QTimer>
#include <QMap>
#include <QDragEnterEvent>
#include <algorithm>
#include <QMimeData>
#include <QDropEvent>
#include <QDragMoveEvent>
#include <QTextDocument>
#include <QRegularExpression>
#include <iostream>

// GUI 调试日志 (已禁用)
// static FILE* g_guiLog = nullptr;
static void GL(const char* msg) {
    (void)msg;
}

// 排版策略简写名称映射
static QString getLayoutStrategyShortName(const QString& key) {
    static QMap<QString, QString> shortNames = {
        {"none", "关闭"},
        {"multi_para", "多栏段落"},
        {"multi_line", "多栏换行"},
        {"multi_none", "多栏连续"},
        {"single_para", "单栏段落"},
        {"single_line", "单栏换行"},
        {"single_none", "单栏连续"},
        {"single_code", "单栏缩进"},
        {"single_vertical", "古籍竖排"}
    };
    return shortNames.value(key, "关闭");
}

MainWindow::MainWindow(QWidget* parent, bool skipOcr)
    : QMainWindow(parent)
    , m_ocr(nullptr)
    , m_httpServer(nullptr)
    , m_screenshotDialog(nullptr)
    , m_widgetPicker(nullptr)
    , m_screenshotMode("rect")
    , m_isAlwaysOnTop(false)
    , m_autoSave(false)
    , m_autoSavePath("")
    , m_autoCopy(true)
    , m_coordMode(false)
    , m_doAngle(false)
    , m_layoutMode(false)
    , m_tableMode(0)
    , m_layoutStrategy("multi_para")
    , m_trayIcon(nullptr)
    , m_forceQuit(false)
    , m_minimizedToTray(false)
    , m_ocrRunning(false)
    , m_ocrTimeoutTimer(nullptr)
    , m_hotkeyRegistered(false)
{
    GL("MainWindow: enter constructor");
    m_settings = new QSettings("OCR工具", "ScreenshotOCR_CPP", this);
    GL("MainWindow: QSettings created");

    // 捕获应用默认字体的像素大小, 作为标签/下拉框/复选框的缩放基线
    // (这些控件不写 stylesheet 字号, 通过 QFont 继承)
    QFont appFont = qApp->font();
    m_baseAppFontPx = (appFont.pixelSize() > 0)
                      ? appFont.pixelSize()
                      : qRound(appFont.pointSizeF() * 96.0 / 72.0);
    GL("MainWindow: font baseline OK");

    GL("MainWindow: before initUI");
    initUI();
    GL("MainWindow: initUI done, before loadSettings");
    loadSettings();
    GL("MainWindow: loadSettings done");
    if (!skipOcr) {
        initOCR();
        GL("MainWindow: initOCR done");
        // initOCR 后应用表格模式 (loadSettings 时 m_ocr 尚未创建)
        if (m_ocr && m_tableMode != 0) {
            m_ocr->setTableMode(m_tableMode);
        }
    } else {
        // server 模式: GUI 不创建 OCR, 状态标签提示
        m_ocrStatusLabel->setText("HTTP 服务模式 (OCR 由服务端提供)");
        m_ocrStatusLabel->setStyleSheet("color: #2e7d32; font-size: 12px;");
        m_netBtn->setChecked(true);  // 网络按钮显示已开启
        GL("MainWindow: skipOcr branch done");
    }
    setupHotkey();   // 注册全局热键 (loadSettings 已把 m_hotkey 设为默认 alt+q)
    GL("MainWindow: setupHotkey done");
    setupTrayIcon(); // 初始化托盘图标 (参考自动加密录音机: 启动时立即创建, 不延迟)
    GL("MainWindow: setupTrayIcon done");

    setWindowTitle("截图识别工具v6.31");
    GL("MainWindow: window flags done");

    // 初始尺寸 = 基线尺寸, scale=1.0, 按钮区域完整可见
    resize(m_baseWidth, m_baseHeight);

    // 应用初始缩放样式 (resizeEvent 会兜底再算一次)
    applyScaledStyle();
    GL("MainWindow: constructor done");
}

MainWindow::~MainWindow() {
    unregisterHotkey();   // 退出前注销全局热键
    stopHttpServer();     // 停止 HTTP 服务
    if (m_ocr) {
        m_ocr->destroy();
        delete m_ocr;
    }
    saveSettings();
}

// 解析快捷键字符串 (如 "alt+q"、"ctrl+shift+a") 为 Windows mod+vk
bool MainWindow::parseHotkey(const QString& combo, UINT& mod, UINT& vk) {
    mod = MOD_NOREPEAT;
    vk = 0;

    if (combo.isEmpty() || combo == "无") {
        return false;
    }

    QStringList parts = combo.toLower().split('+', Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        QString t = p.trimmed();
        if (t == "ctrl" || t == "control") {
            mod |= MOD_CONTROL;
        } else if (t == "shift") {
            mod |= MOD_SHIFT;
        } else if (t == "alt") {
            mod |= MOD_ALT;
        } else if (t == "win") {
            mod |= MOD_WIN;
        } else if (t.length() == 1) {
            // 单个字符 (字母/数字) → 直接转虚拟键码
            vk = toupper(t.toLatin1().at(0));
        } else {
            // 未知 token, 解析失败
            return false;
        }
    }
    return vk != 0;
}

void MainWindow::unregisterHotkey() {
    if (m_hotkeyRegistered) {
        RegisterHotKey(reinterpret_cast<HWND>(winId()), 1, 0, 0);  // 占位, 确保无残留
        UnregisterHotKey(reinterpret_cast<HWND>(winId()), 1);
        m_hotkeyRegistered = false;
    }
}

void MainWindow::setupHotkey() {
    // 先注销旧热键
    unregisterHotkey();

    if (m_hotkey.isEmpty() || m_hotkey == "无") {
        return;
    }

    UINT mod = 0, vk = 0;
    if (!parseHotkey(m_hotkey, mod, vk)) {
        std::cerr << "Failed to parse hotkey: "
                  << m_hotkey.toLocal8Bit().constData() << std::endl;
        return;
    }

    // RegisterHotKey 需要 HWND, 此时窗口已 show 之前但 winId() 会强制创建
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!RegisterHotKey(hwnd, 1, mod, vk)) {
        DWORD err = GetLastError();
        std::cerr << "RegisterHotKey failed for " << m_hotkey.toLocal8Bit().constData()
                  << ", error=" << err << " (可能被其他程序占用)" << std::endl;
        return;
    }
    m_hotkeyRegistered = true;
    std::cout << "Hotkey registered: " << m_hotkey.toLocal8Bit().constData() << std::endl;
}

// 接收 Windows 消息, 处理 WM_HOTKEY
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, long* result) {
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG* msg = reinterpret_cast<MSG*>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == 1) {
            // 全局热键触发 → 执行截图
            onScreenshotClicked();
            return true;
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // 处理文本框的粘贴事件（Ctrl+V / Shift+Insert）
    if (watched == m_resultText && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_V && keyEvent->modifiers() == Qt::ControlModifier) ||
            (keyEvent->key() == Qt::Key_Insert && keyEvent->modifiers() == Qt::ShiftModifier)) {
            QClipboard* clipboard = QApplication::clipboard();
            const QMimeData* mimeData = clipboard->mimeData();

            // 优先检查剪贴板是否有图片数据
            if (mimeData->hasImage()) {
                QPixmap pixmap = clipboard->pixmap();
                if (!pixmap.isNull()) {
                    processImage(pixmap);
                    return true;
                }
            }

            // 检查剪贴板文本是否是图片文件路径或 PDF 文件路径
            if (mimeData->hasText()) {
                QString filePath = clipboard->text();
                if (filePath.startsWith("file:///")) {
                    filePath = filePath.mid(8);
                }
                QFileInfo fi(filePath);
                if (fi.exists() && fi.isFile()) {
                    QString lower = filePath.toLower();
                    // 支持 PDF 文件
                    if (lower.endsWith(".pdf")) {
                        processPdfFile(filePath);
                        return true;
                    }
                    // 支持图片文件
                    if (lower.endsWith(".png") || lower.endsWith(".jpg") ||
                        lower.endsWith(".jpeg") || lower.endsWith(".bmp") ||
                        lower.endsWith(".webp") || lower.endsWith(".gif") ||
                        lower.endsWith(".tiff") || lower.endsWith(".tif")) {
                        QPixmap pixmap(filePath);
                        if (!pixmap.isNull()) {
                            processImage(pixmap);
                            return true;
                        }
                    }
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::initUI() {
    GL("initUI: enter");
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    // 启用拖放功能
    setAcceptDrops(true);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);

    // 按钮区域
    QWidget* buttonGroup = new QWidget();
    QHBoxLayout* buttonLayout = new QHBoxLayout(buttonGroup);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(3);   // 缩小间距, 给 emoji 按钮留更多横向空间

    // 截图按钮
    m_screenshotBtn = new QPushButton("截图");
    m_screenshotBtn->setIcon(AppIcons::fromSvg(AppIcons::screenshot()));
    m_screenshotBtn->setIconSize(QSize(18, 18));
    m_screenshotBtn->setMinimumSize(80, 30);
    m_screenshotBtn->setStyleSheet(
        "QPushButton { background-color: #EDEFF2; color: #333; border: none; border-radius: 5px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #E5E8EE; }"
        "QPushButton:pressed { background-color: #DFE7F2; }"
    );
    connect(m_screenshotBtn, &QPushButton::clicked, this, &MainWindow::onScreenshotClicked);

    // 截图下拉菜单
    m_screenshotMenu = new QMenu(this);
    m_screenshotMenu->setStyleSheet(
        "QMenu { background-color: white; border: 1px solid #ccc; border-radius: 5px; padding: 5px; }"
        "QMenu::item { padding: 8px 20px; border-radius: 3px; }"
        "QMenu::item:selected { background-color: #E6EDFC; }"
    );

    QAction* actionRect = new QAction("矩形截图", this);
    actionRect->setIcon(AppIcons::fromSvg(AppIcons::rect(), 18));
    connect(actionRect, &QAction::triggered, [this]() { m_screenshotMode = "rect"; });
    m_screenshotMenu->addAction(actionRect);

    QAction* actionFree = new QAction("任意形状截图", this);
    actionFree->setIcon(AppIcons::fromSvg(AppIcons::free(), 18));
    connect(actionFree, &QAction::triggered, [this]() { m_screenshotMode = "free"; });
    m_screenshotMenu->addAction(actionFree);

    QAction* actionWidget = new QAction("控件截图", this);
    actionWidget->setIcon(AppIcons::fromSvg(AppIcons::widget(), 18));
    connect(actionWidget, &QAction::triggered, [this]() { m_screenshotMode = "widget"; });
    m_screenshotMenu->addAction(actionWidget);

    QAction* actionWindow = new QAction("窗口截图", this);
    actionWindow->setIcon(AppIcons::fromSvg(AppIcons::window(), 18));
    connect(actionWindow, &QAction::triggered, [this]() { m_screenshotMode = "window"; });
    m_screenshotMenu->addAction(actionWindow);

    QAction* actionFull = new QAction("全屏截图", this);
    actionFull->setIcon(AppIcons::fromSvg(AppIcons::fullscreen(), 18));
    connect(actionFull, &QAction::triggered, [this]() { m_screenshotMode = "full"; });
    m_screenshotMenu->addAction(actionFull);

    // 下拉按钮 (用 minimumSize 而非 fixedSize, 以便随缩放撑开)
    m_screenshotDropdown = new QPushButton();
    m_screenshotDropdown->setIcon(AppIcons::fromSvg(AppIcons::dropdown(), 14));
    m_screenshotDropdown->setIconSize(QSize(12, 12));
    m_screenshotDropdown->setMinimumSize(25, 30);
    m_screenshotDropdown->setStyleSheet(
        "QPushButton { background-color: #EDEFF2; color: #333; border: none; border-radius: 5px; font-size: 12px; }"
        "QPushButton:hover { background-color: #E5E8EE; }"
        "QPushButton:pressed { background-color: #DFE7F2; }"
    );
    connect(m_screenshotDropdown, &QPushButton::clicked, [this]() {
        m_screenshotMenu->popup(m_screenshotBtn->mapToGlobal(m_screenshotBtn->rect().bottomLeft()));
    });

    buttonLayout->addWidget(m_screenshotBtn);
    buttonLayout->addWidget(m_screenshotDropdown);

    // 选择文件按钮
    m_fileBtn = new QPushButton("选择");
    m_fileBtn->setIcon(AppIcons::fromSvg(AppIcons::file()));
    m_fileBtn->setIconSize(QSize(18, 18));
    m_fileBtn->setMinimumSize(60, 30);
    m_fileBtn->setStyleSheet(
        "QPushButton { background-color: #EDEFF2; color: #333; border: none; border-radius: 5px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #E5E8EE; }"
        "QPushButton:pressed { background-color: #DFE7F2; }"
    );
    connect(m_fileBtn, &QPushButton::clicked, this, &MainWindow::onFileClicked);
    buttonLayout->addWidget(m_fileBtn);

    // 清空按钮
    m_clearBtn = new QPushButton("清空");
    m_clearBtn->setIcon(AppIcons::fromSvg(AppIcons::trash()));
    m_clearBtn->setIconSize(QSize(18, 18));
    m_clearBtn->setMinimumSize(60, 30);
    m_clearBtn->setStyleSheet(
        "QPushButton { background-color: #EDEFF2; color: #333; border: none; border-radius: 5px; font-size: 14px; }"
        "QPushButton:hover { background-color: #E5E8EE; }"
        "QPushButton:pressed { background-color: #DFE7F2; }"
    );
    connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::onClearClicked);
    buttonLayout->addWidget(m_clearBtn);

    // 置顶按钮
    m_pinBtn = new QPushButton("置顶");
    m_pinBtn->setIcon(AppIcons::fromSvg(AppIcons::pin()));
    m_pinBtn->setIconSize(QSize(18, 18));
    m_pinBtn->setMinimumSize(60, 30);
    m_pinBtn->setCheckable(true);
    m_pinBtn->setStyleSheet(
        "QPushButton { background-color: #EDEFF2; color: #333; border: none; border-radius: 5px; font-size: 14px; }"
        "QPushButton:hover { background-color: #E5E8EE; }"
        "QPushButton:pressed { background-color: #DFE7F2; }"
        "QPushButton:checked { background-color: #E6EDFC; color: #333; }"
    );
    connect(m_pinBtn, &QPushButton::clicked, this, &MainWindow::onPinClicked);
    buttonLayout->addWidget(m_pinBtn);

    buttonLayout->addStretch();
    mainLayout->addWidget(buttonGroup);

    // 结果文本区域
    m_resultText = new QTextEdit();
    m_resultText->setPlaceholderText("识别结果将显示在这里...");
    m_resultText->setContextMenuPolicy(Qt::CustomContextMenu);  // 自定义右键菜单
    m_resultText->setAcceptDrops(false);  // 禁用文本框拖放，由窗口级 dragEnterEvent/dropEvent 统一处理
    m_resultText->installEventFilter(this);  // 安装事件过滤器，处理粘贴事件
    connect(m_resultText, &QTextEdit::customContextMenuRequested, this, &MainWindow::onResultTextContextMenu);
    m_resultText->setStyleSheet(
        "QTextEdit { font-size: 14px; line-height: 1.5; padding: 10px; border: 1px solid #ccc; border-radius: 5px; background-color: #fafafa; }"
    );
    mainLayout->addWidget(m_resultText);

    // 设置区域
    QWidget* settingsGroup = new QWidget();
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsGroup);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(4);

    // 快捷键 + 排版 横行
    QHBoxLayout* topRow = new QHBoxLayout();

    QLabel* hotkeyLabel = new QLabel("快捷键:");
    topRow->addWidget(hotkeyLabel);

    m_hotkeyCombo = new QComboBox();
    m_hotkeyCombo->addItems({"无", "alt+q", "alt+e", "ctrl+shift+a"});
    connect(m_hotkeyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onHotkeyChanged);
    topRow->addWidget(m_hotkeyCombo);

    topRow->addStretch();

    // 排版模式状态切换按钮
    m_layoutBtn = new QPushButton("关闭");
    m_layoutBtn->setMinimumSize(90, 30);
    m_layoutBtn->setToolTip("TBPU 排版算法: 按人类阅读顺序排序文本块, 智能合并段落");
    connect(m_layoutBtn, &QPushButton::clicked, [this]() {
        m_layoutMenu->popup(m_layoutBtn->mapToGlobal(m_layoutBtn->rect().bottomLeft()));
    });
    topRow->addWidget(m_layoutBtn);

    settingsLayout->addLayout(topRow);

    // 状态提示行 (快捷键下面一行)
    QHBoxLayout* statusRow = new QHBoxLayout();
    m_ocrStatusLabel = new QLabel("OCR引擎就绪");
    m_ocrStatusLabel->setStyleSheet("color: #666; font-size: 12px;");
    m_ocrStatusLabel->setMinimumWidth(80);
    m_ocrStatusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    statusRow->addWidget(m_ocrStatusLabel);
    statusRow->addStretch();
    settingsLayout->addLayout(statusRow);

    // 图标按钮行: 表格 | 自动保存 | 自动复制 | 坐标 | 网络
    QHBoxLayout* iconRow = new QHBoxLayout();
    iconRow->setSpacing(6);

    auto makeIconBtn = [this](const QString& tip, bool checked, std::function<void(bool)> onClick,
                               const QString& svgChecked, const QString& svgNormal,
                               const QString& label = QString()) -> QPushButton* {
        QPushButton* btn = new QPushButton(label);
        QString initSvg = checked
            ? AppIcons::wrapWithColor(svgChecked, "#FFD700")
            : svgNormal;
        btn->setIcon(AppIcons::fromSvg(initSvg, 14));
        btn->setIconSize(QSize(14, 14));
        btn->setMinimumSize(68, 28);
        btn->setToolTip(tip);
        btn->setCheckable(true);
        btn->setChecked(checked);
        btn->setStyleSheet("QPushButton { border: none; border-radius: 4px; padding: 2px 4px;"
                           " font-size: 11px; color: #555; }"
                           "QPushButton:hover { background: #e6efff; }");
        connect(btn, &QPushButton::toggled, this, [btn, svgChecked, svgNormal](bool checked) {
            QString svg = checked
                ? AppIcons::wrapWithColor(svgChecked, "#FFD700")
                : svgNormal;
            btn->setIcon(AppIcons::fromSvg(svg, 14));
        });
        connect(btn, &QPushButton::clicked, this, [onClick](bool checked) {
            onClick(checked);
        });
        return btn;
    };

    // 表格模式 (三态切换: 关→SLANet→img2table→关)
    m_tableBtn = new QPushButton("表格(关)");
    m_tableBtn->setMinimumSize(68, 28);
    m_tableBtn->setToolTip("点击开启表格模式");
    m_tableBtn->setIcon(AppIcons::fromSvg(AppIcons::table(), 14));
    m_tableBtn->setIconSize(QSize(14, 14));
    m_tableBtn->setStyleSheet("QPushButton { border: none; border-radius: 4px; padding: 2px 4px;"
                               " font-size: 11px; color: #555; }"
                               "QPushButton:hover { background: #e6efff; }");
    connect(m_tableBtn, &QPushButton::clicked, this, [this]() {
        m_tableMode = (m_tableMode + 1) % 3;
        if (m_ocr) m_ocr->setTableMode(m_tableMode);
        if (m_tableMode == 0) {
            m_tableBtn->setText("表格(关)");
            m_tableBtn->setIcon(AppIcons::fromSvg(AppIcons::table(), 14));
            m_tableBtn->setToolTip("点击开启表格模式");
        } else if (m_tableMode == 1) {
            m_tableBtn->setText("SLANet");
            m_tableBtn->setIcon(AppIcons::fromSvg(AppIcons::wrapWithColor(AppIcons::tableBody(), "#FFD700"), 14));
            m_tableBtn->setToolTip("使用 SLANet-plus 模型识别表格");
        } else {
            m_tableBtn->setText("img2table");
            m_tableBtn->setIcon(AppIcons::fromSvg(AppIcons::wrapWithColor(AppIcons::tableBody(), "#FF4444"), 14));
            m_tableBtn->setToolTip("使用 OpenCV 识别有线表格");
        }
        // 表格模式启用时禁用排版, 反之允许排版
        if (m_tableMode != 0) {
            m_layoutMode = false;
            m_layoutBtn->setChecked(false);
        }
        m_layoutBtn->setEnabled(m_tableMode == 0);
        m_layoutBtn->setText(m_tableMode != 0 ? "不可排版" : getLayoutStrategyShortName(
            QString::fromStdString(m_layoutStrategy)));
        // 状态栏提示
        if (m_tableMode == 0) {
            m_ocrStatusLabel->setText("表格模式: 关");
        } else if (m_tableMode == 1) {
            m_ocrStatusLabel->setText("表格模式: SLANet");
        } else {
            m_ocrStatusLabel->setText("表格模式: img2table");
        }
    });
    // 自动保存
    QPushButton* autoSaveBtn = makeIconBtn("自动保存", m_autoSave,
        [this](bool checked) { onAutoSaveChanged(checked ? Qt::Checked : Qt::Unchecked); },
        AppIcons::autoSaveBody(), AppIcons::autoSave(), "自动保存");
    // 自动复制
    QPushButton* autoCopyBtn = makeIconBtn("自动复制", m_autoCopy,
        [this](bool checked) {
            m_autoCopy = checked;
            m_ocrStatusLabel->setText(checked ? "自动复制: 开" : "自动复制: 关");
        },
        AppIcons::autoCopyBody(), AppIcons::autoCopy(), "自动复制");
    // 坐标模式
    QPushButton* coordBtn = makeIconBtn("坐标模式", m_coordMode,
        [this](bool checked) {
            GL("coordBtn: clicked");
            m_coordMode = checked;
            m_ocrStatusLabel->setText(checked ? "坐标模式: 开" : "坐标模式: 关");
            GL("coordBtn: coordMode set");
        },
        AppIcons::coordBody(), AppIcons::coord(), "坐标模式");

    // 高级设置按钮
    QPushButton* advancedBtn = new QPushButton("高级设置");
    advancedBtn->setIcon(AppIcons::fromSvg(AppIcons::gear(), 14));
    advancedBtn->setIconSize(QSize(14, 14));
    advancedBtn->setMinimumSize(68, 28);
    advancedBtn->setToolTip("高级设置\n\n点击打开 OCR 参数设置");
    advancedBtn->setStyleSheet("QPushButton { border: none; border-radius: 4px; padding: 2px 4px;"
                               " font-size: 11px; color: #555; }"
                               "QPushButton:hover { background: #e6efff; }");
    connect(advancedBtn, &QPushButton::clicked, this, [this]() {
        showAdvancedSettingsDialog();
    });

    // 保存按钮引用以便在 makeIconBtn lambda 中访问
    m_autoSaveBtn = autoSaveBtn;
    m_autoCopyBtn = autoCopyBtn;
    m_coordBtn = coordBtn;
    m_advancedBtn = advancedBtn;

    iconRow->addWidget(m_tableBtn);
    iconRow->addWidget(autoSaveBtn);
    iconRow->addWidget(autoCopyBtn);
    iconRow->addWidget(coordBtn);
    iconRow->addWidget(advancedBtn);
    iconRow->addStretch();

    settingsLayout->addLayout(iconRow);

    // 网络服务 (放在快捷键行右侧)
    m_netBtn = makeIconBtn("网络服务", false,
        [this](bool) { onNetClicked(); },
        AppIcons::networkBody(), AppIcons::network(), "网络服务");
    topRow->addWidget(m_netBtn);

    // 排版策略菜单
    m_layoutMenu = new QMenu(this);
    // 样式由 applyScaledStyle 统一处理
    // 菜单项: 关闭排版 + 各策略
    QAction* actionNone = new QAction("关闭排版", this);
    actionNone->setData("none");
    m_layoutMenu->addAction(actionNone);
    m_layoutMenu->addSeparator();
    const auto& strategies = layoutStrategies();
    for (const auto& s : strategies) {
        QAction* action = new QAction(getLayoutStrategyShortName(QString::fromUtf8(s.key)), this);
        action->setData(s.key);
        m_layoutMenu->addAction(action);
    }
    connect(m_layoutMenu, &QMenu::triggered, this, &MainWindow::onLayoutMenuTriggered);

    mainLayout->addWidget(settingsGroup);
}

void MainWindow::applyScaledStyle() {
    // 基于"初始完美显示尺寸"的线性比例, 等比放大所有像素值
    const qreal s = m_scale;
    const int   pxBtn   = qRound(14 * s);  // 按钮字号基线
    const int   pxDrop  = qMax(8, qRound(12 * s));  // 下拉箭头字号基线
    const int   pxEdit  = qRound(14 * s);  // 结果文本字号基线
    const int   pxLabel = qMax(9, qRound(12 * s));  // 状态标签字号基线
    const int   radius  = qMax(2, qRound(5 * s));   // 圆角基线
    const int   padBtn  = qRound(6 * s);            // 按钮内边距
    const int   padEdit = qRound(10 * s);           // 文本框内边距

    // 1. 按钮最小尺寸按基线比例缩放, 让布局自动撑开控件
    // (emoji 按钮 + 文字横向占位较大, 基准宽度留足)
    m_screenshotBtn->setMinimumSize   (qRound(88 * s), qRound(30 * s));
    m_screenshotDropdown->setMinimumSize(qRound(25 * s), qRound(30 * s));
    m_fileBtn->setMinimumSize         (qRound(72 * s), qRound(30 * s));
    m_clearBtn->setMinimumSize        (qRound(72 * s), qRound(30 * s));
    m_pinBtn->setMinimumSize          (qRound(72 * s), qRound(30 * s));
    m_layoutBtn->setMinimumSize       (qRound(100 * s), qRound(30 * s));  // 排版按钮 (简写显示, 加宽)
    m_tableBtn->setMinimumSize        (qRound(28 * s), qRound(28 * s));  // 表格模式按钮

    // 2. 按钮样式 (统一颜色, 仅 font-size/padding/radius 随缩放)
    const QString btnStyle = QString(
        "QPushButton { background-color: #EDEFF2; color: #333; border: none;"
        " border-radius: %1px; font-size: %2px; padding: %3px %4px; }"
        "QPushButton:hover { background-color: #E5E8EE; }"
        "QPushButton:pressed { background-color: #DFE7F2; }"
    ).arg(radius).arg(pxBtn).arg(padBtn/2).arg(padBtn);

    const QString btnStyleActive = QString(
        "QPushButton { background-color: #E6EDFC; color: #333; border: none;"
        " border-radius: %1px; font-size: %2px; padding: %3px %4px; }"
        "QPushButton:hover { background-color: #D6E5F5; }"
        "QPushButton:pressed { background-color: #C6D5E5; }"
    ).arg(radius).arg(pxBtn).arg(padBtn/2).arg(padBtn);

    m_screenshotBtn->setStyleSheet(btnStyle + "QPushButton { font-weight: bold; }");
    m_fileBtn->setStyleSheet(btnStyle + "QPushButton { font-weight: bold; }");
    m_clearBtn->setStyleSheet(btnStyle);
    m_pinBtn->setStyleSheet(btnStyle +
        "QPushButton:checked { background-color: #E6EDFC; color: #333; }");
    // 排版按钮: 根据当前状态选择样式
    m_layoutBtn->setStyleSheet(m_layoutMode ? btnStyleActive : btnStyle);

    // 下拉箭头按钮 (字号略小)
    m_screenshotDropdown->setStyleSheet(QString(
        "QPushButton { background-color: #EDEFF2; color: #333; border: none;"
        " border-radius: %1px; font-size: %2px; }"
        "QPushButton:hover { background-color: #E5E8EE; }"
        "QPushButton:pressed { background-color: #DFE7F2; }"
    ).arg(radius).arg(pxDrop));

    // 3. 结果文本框
    m_resultText->setStyleSheet(QString(
        "QTextEdit { font-size: %1px; line-height: 1.5; padding: %2px;"
        " border: 1px solid #ccc; border-radius: %3px; background-color: #fafafa; }"
    ).arg(pxEdit).arg(padEdit).arg(radius));

    // 4. 状态标签 (颜色字号，不设最大宽度以免长文本被截断)
    m_ocrStatusLabel->setStyleSheet(QString(
        "color: #666; font-size: %1px;"
    ).arg(pxLabel));

    // 5. 菜单项内边距随缩放
    const int menuPadV = qMax(4, qRound(8 * s));
    const int menuPadH = qMax(8, qRound(20 * s));
    const QString menuStyle = QString(
        "QMenu { background-color: white; border: 1px solid #ccc; border-radius: %1px; padding: %2px; }"
        "QMenu::item { padding: %3px %4px; border-radius: %5px; }"
        "QMenu::item:selected { background-color: #E6EDFC; }"
    ).arg(radius).arg(qRound(5*s)).arg(menuPadV).arg(menuPadH).arg(qMax(2,qRound(3*s)));
    m_screenshotMenu->setStyleSheet(menuStyle);
    m_layoutMenu->setStyleSheet(menuStyle);  // 排版菜单也应用缩放样式

    // 6. 标签/下拉框/复选框继承缩放字体 (这些控件未在 stylesheet 写字号)
    QFont f = qApp->font();
    f.setPixelSize(qMax(9, qRound(m_baseAppFontPx * s)));
    centralWidget()->setFont(f);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);

    // 取较小比例, 保证宽/高任一方向都按基线比例缩放 (不变形)
    qreal newScale = qMin<qreal>(
        qreal(event->size().width())  / m_baseWidth,
        qreal(event->size().height()) / m_baseHeight
    );
    // 限定下限, 防止极端缩小时字看不清
    if (newScale < 0.5) newScale = 0.5;

    // 阈值过滤: 像素变化 <1 才刷新, 避免拖动时无谓的重绘抖动
    if (qAbs(newScale - m_scale) < 1.0 / qMax(m_baseWidth, m_baseHeight)) {
        return;
    }
    m_scale = newScale;
    applyScaledStyle();
}

void MainWindow::initOCR() {
    GL("initOCR: creating OCRWrapper...");
    m_ocr = new OCRWrapper();

    GL("initOCR: calling initEmbedded(4)...");
    if (!m_ocr->initEmbedded(4)) {
        GL("initOCR: FAILED");
        QMessageBox::critical(this, "错误", "初始化OCR引擎失败");
        m_ocrStatusLabel->setText("OCR引擎初始化失败");
        delete m_ocr;
        m_ocr = nullptr;
        return;
    }
    GL("initOCR: OK");
    m_ocrStatusLabel->setText("OCR引擎已初始化(嵌入式)");

    // 加载全局共享配置 (ocr_config.json)
    m_ocr->loadConfig();
    m_doAngle = (m_ocr->doAngle() != 0);

    // 空闲卸载定时器: 2分钟无OCR使用自动释放模型
    m_ocrIdleTimer = new QTimer(this);
    m_ocrIdleTimer->setSingleShot(true);
    connect(m_ocrIdleTimer, &QTimer::timeout, this, [this]() {
        if (m_ocr) {
            m_ocr->unload();
            m_ocrStatusLabel->setText("OCR引擎已释放(空闲)");
            std::cerr << "[idle] OCR model unloaded to save memory" << std::endl;
        }
    });
    m_ocrIdleTimer->start(120000);  // 2分钟
}

void MainWindow::resetOcrIdleTimer() {
    if (m_ocrIdleTimer) {
        m_ocrIdleTimer->start();  // 重置倒计时
        if (m_ocr && !m_ocr->isInitialized()) {
            m_ocr->ensureLoaded(4);
            if (m_ocr->tableMode()) m_ocr->setTableMode(m_ocr->tableMode());
            m_ocrStatusLabel->setText("OCR引擎已初始化(嵌入式)");
        }
    }
}

void MainWindow::loadSettings() {
    // 快捷键和自动保存状态每次启动都重置为默认值, 不读取上次记录
    m_hotkey = "alt+q";
    m_autoSave = false;
    m_autoSavePath = m_settings->value("auto_save_path", "").toString();
    m_autoCopy = true;
    m_coordMode = false;
    // m_doAngle 由 initOCR() 中 loadConfig() 从 ocr_config.json 加载
    m_layoutMode = m_settings->value("layout_mode", false).toBool();
    m_layoutStrategy = m_settings->value("layout_strategy", "multi_para").toString().toStdString();
    m_isAlwaysOnTop = m_settings->value("always_on_top", false).toBool();
    m_tableMode = 0; // 表格模式固定关闭，不保留状态

    // 清除历史遗留记录, 保持设置文件干净
    m_settings->remove("hotkey");
    m_settings->remove("auto_save");
    m_settings->remove("coord_mode");
    m_settings->remove("do_angle");
    m_settings->remove("do_angle_by_mode");

    // 更新 UI
    int hotkeyIndex = m_hotkeyCombo->findText(m_hotkey);
    if (hotkeyIndex >= 0) {
        m_hotkeyCombo->setCurrentIndex(hotkeyIndex);
    }

    m_autoSaveBtn->setChecked(m_autoSave);
    m_autoCopyBtn->setChecked(m_autoCopy);
    m_coordBtn->setChecked(m_coordMode);
    // m_ocr 此时可能还没创建, doAngle 由 initOCR() 中 loadConfig() 加载

    // 更新排版按钮显示状态
    QString strategyKey = QString::fromStdString(m_layoutStrategy);
    m_layoutBtn->setText(getLayoutStrategyShortName(strategyKey));
    if (m_layoutMode && strategyKey != "none") {
        // 查找策略标签显示在 tooltip
        const auto& strategies = layoutStrategies();
        for (const auto& s : strategies) {
            if (QString::fromUtf8(s.key) == strategyKey) {
                m_layoutBtn->setToolTip(QString("当前策略: %1").arg(QString::fromUtf8(s.label)));
                break;
            }
        }
    } else {
        m_layoutBtn->setToolTip("TBPU 排版算法: 按人类阅读顺序排序文本块, 智能合并段落");
    }
    // 样式由 applyScaledStyle 统一处理

    m_pinBtn->setChecked(m_isAlwaysOnTop);

    if (m_isAlwaysOnTop) {
        setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    }

    // 恢复表格模式按钮状态
    if (m_ocr) m_ocr->setTableMode(m_tableMode);
    if (m_tableMode == 0) {
        m_tableBtn->setText("表格(关)");
        m_tableBtn->setIcon(AppIcons::fromSvg(AppIcons::table(), 14));
        m_tableBtn->setToolTip("点击开启表格模式");
    } else if (m_tableMode == 1) {
        m_tableBtn->setText("SLANet");
        m_tableBtn->setIcon(AppIcons::fromSvg(AppIcons::wrapWithColor(AppIcons::tableBody(), "#FFD700"), 14));
        m_tableBtn->setToolTip("使用 SLANet-plus 模型识别表格");
    } else {
        m_tableBtn->setText("img2table");
        m_tableBtn->setIcon(AppIcons::fromSvg(AppIcons::wrapWithColor(AppIcons::tableBody(), "#FF4444"), 14));
        m_tableBtn->setToolTip("使用 OpenCV 识别有线表格");
    }
    m_layoutBtn->setEnabled(m_tableMode == 0);
    m_layoutBtn->setText(m_tableMode != 0 ? "不可排版" : getLayoutStrategyShortName(
        QString::fromStdString(m_layoutStrategy)));
}

void MainWindow::saveSettings() {
    // 不保存 hotkey、auto_save, 让它们每次启动都重置为默认值
    m_settings->setValue("auto_save_path", m_autoSavePath);
    m_settings->setValue("auto_copy", m_autoCopy);
    m_settings->setValue("layout_mode", m_layoutMode);
    m_settings->setValue("layout_strategy", QString::fromStdString(m_layoutStrategy));
    m_settings->setValue("always_on_top", m_isAlwaysOnTop);
    // 不保存 table_mode 状态

    // OCR 参数写入全局共享配置 (ocr_config.json), GUI/CLI/HTTP 共用
    if (m_ocr) {
        m_ocr->saveConfig();
    }
}

void MainWindow::onScreenshotClicked() {
    doScreenshot(m_screenshotMode);
}

void MainWindow::doScreenshot(const QString& mode) {
    GL("doScreenshot: enter");
    if (!m_ocr) {
        QMessageBox::warning(this, "警告", "OCR引擎未初始化");
        return;
    }

    hide();
    QTimer::singleShot(300, [this, mode]() {
        if (mode == "full") {
            QScreen* screen = QApplication::primaryScreen();
            QPixmap pixmap = screen->grabWindow(0);
            processImage(pixmap);
            if (!m_minimizedToTray) {
                show();
            }
        } else if (mode == "widget" || mode == "window") {
            m_widgetPicker = new WidgetPicker(mode);
            connect(m_widgetPicker, &WidgetPicker::widgetSelected, this, &MainWindow::onWidgetSelected);
            connect(m_widgetPicker, &WidgetPicker::cancelled, this, &MainWindow::onWidgetPickerCancelled);
            QTimer::singleShot(50, m_widgetPicker, &WidgetPicker::show);
        } else if (mode == "free") {
            QScreen* screen = QApplication::primaryScreen();
            QPixmap pixmap = screen->grabWindow(0);
            m_screenshotDialog = new ScreenshotDialog(pixmap, true);
            connect(m_screenshotDialog, &ScreenshotDialog::closed, this, &MainWindow::onScreenshotClosed);
            QTimer::singleShot(50, m_screenshotDialog, &QWidget::show);
        } else {
            QScreen* screen = QApplication::primaryScreen();
            QPixmap pixmap = screen->grabWindow(0);
            m_screenshotDialog = new ScreenshotDialog(pixmap, false);
            connect(m_screenshotDialog, &ScreenshotDialog::closed, this, &MainWindow::onScreenshotClosed);
            QTimer::singleShot(50, m_screenshotDialog, &QWidget::show);
        }
    });
}

void MainWindow::onScreenshotClosed(QPixmap pixmap) {
    GL("onScreenshotClosed: enter");

    if (!pixmap.isNull()) {
        processImage(pixmap);
    }

    if (m_screenshotDialog) {
        m_screenshotDialog->deleteLater();
        m_screenshotDialog = nullptr;
    }

    // 如果之前最小化到托盘，不弹出主窗口
    if (!m_minimizedToTray) {
        show();
        activateWindow();
    }
}

void MainWindow::onWidgetSelected(HWND hwnd, QString title, QRect rect) {
    // 1. 先截图（此时主窗口还在隐藏中，WidgetPicker已关闭，不会遮挡目标）
    QPixmap pixmap;
    if (rect.isValid()) {
        QScreen* screen = QApplication::primaryScreen();
        pixmap = screen->grabWindow(0, rect.x(), rect.y(), rect.width(), rect.height());
    }

    // 2. 如果之前最小化到托盘，不弹出主窗口
    if (!m_minimizedToTray) {
        show();
        activateWindow();
    }

    // 3. 最后才进行识别
    if (!pixmap.isNull()) {
        processImage(pixmap);
    }

    // 清理 WidgetPicker
    if (m_widgetPicker) {
        m_widgetPicker->deleteLater();
        m_widgetPicker = nullptr;
    }
}

void MainWindow::onWidgetPickerCancelled() {
    if (!m_minimizedToTray) {
        show();
        activateWindow();
    }

    if (m_widgetPicker) {
        m_widgetPicker->deleteLater();
        m_widgetPicker = nullptr;
    }
}

void MainWindow::onFileClicked() {
    GL("onFileClicked: enter");
    if (!m_ocr) {
        QMessageBox::warning(this, "警告", "OCR引擎未初始化");
        return;
    }

    QStringList filePaths = QFileDialog::getOpenFileNames(
        this, "选择图片或PDF", "", "图片和PDF文件 (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tiff *.tif *.pdf);;所有文件 (*.*)"
    );

    if (!filePaths.isEmpty()) {
        processFiles(filePaths);
    }
}

void MainWindow::onClearClicked() {
    m_resultText->clear();
    m_ocrStatusLabel->setText("已清空");
}

void MainWindow::onPinClicked() {
    m_isAlwaysOnTop = !m_isAlwaysOnTop;
    m_pinBtn->setChecked(m_isAlwaysOnTop);
    m_ocrStatusLabel->setText(m_isAlwaysOnTop ? "置顶: 开" : "置顶: 关");

    if (m_isAlwaysOnTop) {
        setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    } else {
        setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
    }

    show();
    saveSettings();
}

void MainWindow::onHotkeyChanged(int index) {
    m_hotkey = m_hotkeyCombo->currentText();
    // 切换热键选项时, 重新注册全局热键
    setupHotkey();
}

void MainWindow::onAutoCopyChanged(int) {
    m_autoCopy = m_autoCopyBtn->isChecked();
    m_ocrStatusLabel->setText(m_autoCopy ? "自动复制: 开" : "自动复制: 关");
    saveSettings();
}

void MainWindow::onAutoSaveChanged(int) {
    bool wasOn = m_autoSave;
    m_autoSave = m_autoSaveBtn->isChecked();

    if (m_autoSave && !wasOn) {
        QString path = QFileDialog::getExistingDirectory(
            this,
            "选择自动保存目录",
            m_autoSavePath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation) : m_autoSavePath
        );
        if (!path.isEmpty()) {
            m_autoSavePath = path;
            m_ocrStatusLabel->setText(QString("自动保存: 开 (%1)").arg(QDir(m_autoSavePath).dirName()));
        } else {
            // 用户取消选择, 恢复按钮状态
            m_autoSave = false;
            m_autoSaveBtn->blockSignals(true);
            m_autoSaveBtn->setChecked(false);
            m_autoSaveBtn->blockSignals(false);
            // blockSignals 跳过了 toggled, 手动重置图标
            m_autoSaveBtn->setIcon(AppIcons::fromSvg(AppIcons::autoSave(), 14));
            m_ocrStatusLabel->setText("自动保存: 已取消");
        }
    } else if (!m_autoSave && wasOn) {
        m_ocrStatusLabel->setText("自动保存: 关");
    }

    saveSettings();
}

void MainWindow::onCoordModeChanged(int) {
    m_coordMode = m_coordBtn->isChecked();
    m_ocrStatusLabel->setText(m_coordMode ? "坐标模式: 开" : "坐标模式: 关");
    saveSettings();
}

void MainWindow::onLayoutMenuTriggered(QAction* action) {
    QString key = action->data().toString();
    if (key == "none") {
        m_layoutMode = false;
        m_layoutStrategy = "none";
        m_layoutBtn->setText(getLayoutStrategyShortName("none"));
        m_layoutBtn->setToolTip("TBPU 排版算法: 按人类阅读顺序排序文本块, 智能合并段落");
    } else {
        m_layoutMode = true;
        m_layoutStrategy = key.toStdString();
        m_layoutBtn->setText(getLayoutStrategyShortName(key));
        // 查找策略标签显示在 tooltip
        const auto& strategies = layoutStrategies();
        for (const auto& s : strategies) {
            if (QString::fromUtf8(s.key) == key) {
                m_layoutBtn->setToolTip(QString("当前策略: %1").arg(QString::fromUtf8(s.label)));
                break;
            }
        }
    }
    // 更新按钮样式 (调用 applyScaledStyle 会统一处理)
    applyScaledStyle();
    m_ocrStatusLabel->setText(m_layoutMode ? QString("排版: %1").arg(getLayoutStrategyShortName(QString::fromStdString(m_layoutStrategy))) : "排版: 关");
    saveSettings();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
        m_minimizedToTray = false;
        showNormal();
        activateWindow();
        raise();
    }
}

// 从 HICON 转换为 QPixmap (纯 Win32 API, 不依赖 QtWinExtras)
static QPixmap pixmapFromHICON(HICON hIcon) {
    ICONINFO ii = {0};
    if (!GetIconInfo(hIcon, &ii)) return QPixmap();

    // 获取图标尺寸
    BITMAP bm = {0};
    GetObject(ii.hbmColor, sizeof(BITMAP), &bm);
    int w = bm.bmWidth, h = bm.bmHeight;

    // 创建兼容 DC 和 DIB Section
    HDC hdc = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(hdc);
    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = w;
    bi.biHeight = -h;  // 自顶向下
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memDC, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, dib);

    // 绘制图标到 DIB
    DrawIconEx(memDC, 0, 0, hIcon, w, h, 0, NULL, DI_NORMAL);
    SelectObject(memDC, oldBmp);

    // 取出 alpha 通道 (预乘 alpha)
    // Win32 图标的 32bpp DIB 通常 alpha=0 表示不透明像素, 需要反转
    unsigned char* p = (unsigned char*)bits;
    for (int i = 0; i < w * h; i++) {
        if (p[3] == 0 && (p[0] || p[1] || p[2])) {
            p[3] = 255;  // 有颜色但 alpha 为 0 → 设为不透明
        }
        p += 4;
    }

    // 转为 QPixmap
    QImage img((unsigned char*)bits, w, h, QImage::Format_ARGB32_Premultiplied);
    QPixmap pm = QPixmap::fromImage(img.copy());  // 深拷贝, bits 会被 DeleteObject 释放

    DeleteObject(dib);
    DeleteDC(memDC);
    ReleaseDC(NULL, hdc);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return pm;
}

void MainWindow::setupTrayIcon() {
    // 启动时立即创建并显示托盘图标 (参考自动加密录音机: InitTrayIcon 在 WinMain 中调用)
    m_trayIcon = new QSystemTrayIcon(this);

    // 获取托盘图标: 优先从 exe 资源加载原生 HICON (参考自动加密录音机 LoadIcon 方式)
    // 这是最可靠的方式, 不依赖 windowIcon() 是否设置, 也不用 SVG 渲染
    QIcon trayIcon;
#ifdef Q_OS_WIN
    // RC 文件: IDI_ICON1 ICON "1.ico" -- IDI_ICON1 被当作字符串名存储
    // 必须用 L"IDI_ICON1" 加载, 不能用 MAKEINTRESOURCE(101) (经 test_tray 验证)
    HICON hIcon = LoadIcon(GetModuleHandle(NULL), L"IDI_ICON1");
    if (hIcon) {
        QPixmap pm = pixmapFromHICON(hIcon);
        if (!pm.isNull()) {
            trayIcon = QIcon(pm);
        }
        DestroyIcon(hIcon);
    }
#endif
    // 兜底: exe 文件图标 → SVG
    if (trayIcon.isNull()) {
        trayIcon = QIcon(QCoreApplication::applicationFilePath());
    }
    if (trayIcon.isNull()) {
        trayIcon = AppIcons::fromSvg(AppIcons::screenshot(), 32);
    }
    m_trayIcon->setIcon(trayIcon);
    m_trayIcon->setToolTip("截图识别工具v6.31");

    QMenu* trayMenu = new QMenu();
    QAction* showAction = trayMenu->addAction("显示主窗口");
    connect(showAction, &QAction::triggered, this, [this]() {
        m_minimizedToTray = false;
        showNormal();
        activateWindow();
        raise();
    });
    QAction* quitAction = trayMenu->addAction("退出");
    connect(quitAction, &QAction::triggered, this, [this]() {
        m_forceQuit = true;
        // 强制退出: 直接杀进程, 避免 Qt 退出流程不稳定
        // 先隐藏托盘图标, 防止退出后残留
        if (m_trayIcon) {
            m_trayIcon->hide();
            m_trayIcon->deleteLater();
            m_trayIcon = nullptr;
        }
        // 强制终止进程 (不经过 Qt 退出流程)
        std::cerr << "[exit] 强制退出" << std::endl;
        TerminateProcess(GetCurrentProcess(), 0);
    });
    m_trayIcon->setContextMenu(trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);

    m_trayIcon->show();  // 立即显示托盘图标
}

void MainWindow::minimizeToTray() {
    // 阻止最后一个窗口隐藏时自动退出应用
    QApplication::setQuitOnLastWindowClosed(false);

    m_minimizedToTray = true;
    hide();                 // 彻底隐藏 (不留在任务栏)
    if (m_trayIcon) {
        m_trayIcon->showMessage("截图识别工具v6.31", "已最小化到托盘，双击图标恢复窗口",
            QSystemTrayIcon::Information, 2000);
    }
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!m_forceQuit) {
        // 右上角关闭按钮 → 最小化到托盘
        minimizeToTray();
        event->ignore();
    } else {
        // 托盘菜单"退出" → 真正退出
        if (m_trayIcon) {
            m_trayIcon->hide();
        }
        event->accept();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    // 检查拖入的内容是否包含图片文件或图片数据
    if (event->mimeData()->hasUrls()) {
        // 检查是否有图片文件或 PDF 文件
        const QList<QUrl>& urls = event->mimeData()->urls();
        for (const QUrl& url : urls) {
            if (url.isLocalFile()) {
                QString filePath = url.toLocalFile();
                QString lowerPath = filePath.toLower();
                if (lowerPath.endsWith(".png") || lowerPath.endsWith(".jpg") ||
                    lowerPath.endsWith(".jpeg") || lowerPath.endsWith(".bmp") ||
                    lowerPath.endsWith(".webp") || lowerPath.endsWith(".gif") ||
                    lowerPath.endsWith(".tiff") || lowerPath.endsWith(".tif") ||
                    lowerPath.endsWith(".pdf")) {  // 支持 PDF
                    event->acceptProposedAction();  // 接受拖放
                    return;
                }
            }
        }
    } else if (event->mimeData()->hasImage()) {
        // 直接拖入图片数据
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();

    // 1. 处理图片文件或 PDF 文件拖放
    if (mimeData->hasUrls()) {
        const QList<QUrl>& urls = mimeData->urls();
        QStringList files;
        
        for (const QUrl& url : urls) {
            if (url.isLocalFile()) {
                QString filePath = url.toLocalFile();
                QFileInfo info(filePath);
                
                // 如果是文件夹，收集其中的图片和 PDF 文件
                if (info.isDir()) {
                    QDir dir(filePath);
                    QStringList nameFilters = {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.gif", "*.webp", "*.tiff", "*.tif", "*.pdf"};
                    QStringList dirFiles = dir.entryList(nameFilters, QDir::Files);
                    for (const QString& f : dirFiles) {
                        files.append(filePath + "/" + f);
                    }
                } else {
                    // 单个文件
                    QString lowerPath = filePath.toLower();
                    if (lowerPath.endsWith(".pdf") ||
                        lowerPath.endsWith(".png") || lowerPath.endsWith(".jpg") ||
                        lowerPath.endsWith(".jpeg") || lowerPath.endsWith(".bmp") ||
                        lowerPath.endsWith(".webp") || lowerPath.endsWith(".gif") ||
                        lowerPath.endsWith(".tiff") || lowerPath.endsWith(".tif")) {
                        files.append(filePath);
                    }
                }
            }
        }
        
        if (!files.isEmpty()) {
            processFiles(files);
            event->acceptProposedAction();
            return;
        }
    }

    // 2. 处理图片数据拖放
    if (mimeData->hasImage()) {
        QPixmap pixmap = qvariant_cast<QPixmap>(mimeData->imageData());
        if (!pixmap.isNull()) {
            processImage(pixmap);
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::processPdfFile(const QString& filePath) {
    resetOcrIdleTimer();
    if (!m_ocr) {
        QMessageBox::warning(this, "警告", "OCR引擎未初始化");
        return;
    }

    // 加载 PDF 文件
    if (!PdfHelper::load(filePath)) {
        QMessageBox::warning(this, "警告", "无法加载PDF文件");
        return;
    }

    int pageCount = PdfHelper::pageCount();
    if (pageCount <= 0) {
        QMessageBox::warning(this, "警告", "PDF文件没有页面");
        PdfHelper::close();
        return;
    }

    bool tableMode = (m_tableMode != 0);

    // 单页 PDF
    if (pageCount == 1) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("PDF识别");
        if (tableMode) {
            msgBox.setText("PDF共 1 页，表格模式：是否识别并输出 HTML？");
        } else {
            msgBox.setText("PDF共 1 页，是否识别并输出可搜索 PDF？");
        }
        msgBox.setIcon(QMessageBox::Question);
        QCheckBox* outputCheckBox = new QCheckBox(tableMode ? "输出 HTML" : "输出可搜索 PDF");
        outputCheckBox->setToolTip(tableMode ? "生成带边框的 HTML 表格文件" : "生成带文字层的 PDF，可在阅读器中搜索/复制文字");
        msgBox.setCheckBox(outputCheckBox);
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);
        msgBox.button(QMessageBox::Yes)->setText("识别");
        msgBox.button(QMessageBox::No)->setText("取消");
        
        bool wantXlsx = false;
        if (tableMode) {
            QMessageBox xlsxMsg(this);
            xlsxMsg.setWindowTitle("XLSX输出");
            xlsxMsg.setText("是否同时输出 XLSX 表格文件？");
            xlsxMsg.setIcon(QMessageBox::Question);
            xlsxMsg.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            xlsxMsg.setDefaultButton(QMessageBox::Yes);
            xlsxMsg.button(QMessageBox::Yes)->setText("是");
            xlsxMsg.button(QMessageBox::No)->setText("否");
            wantXlsx = (xlsxMsg.exec() == QMessageBox::Yes);
        }
        
        if (msgBox.exec() == QMessageBox::Yes) {
            QPixmap pixmap = PdfHelper::renderPage(0);
            PdfHelper::close();
            if (!pixmap.isNull()) {
                bool wantOutput = outputCheckBox->isChecked();
                
                // 处理透明背景
                QPixmap processedPixmap = pixmap;
                if (pixmap.hasAlpha()) {
                    processedPixmap = QPixmap(pixmap.size());
                    processedPixmap.fill(Qt::white);
                    QPainter painter(&processedPixmap);
                    painter.drawPixmap(0, 0, pixmap);
                    painter.end();
                }
                
                // 转换为 BMP 进行识别
                QByteArray byteArray;
                QBuffer buffer(&byteArray);
                buffer.open(QIODevice::WriteOnly);
                processedPixmap.save(&buffer, "BMP");
                buffer.close();
                
                std::vector<unsigned char> imageData(byteArray.begin(), byteArray.end());

                if (tableMode) {
                    auto tableResult = m_ocr->detectTable(imageData);
                    QString pageHtml = QString::fromStdString(tableResult.htmlStructure);
                    QString pageText = QString::fromStdString(tableResult.ocrText);
                    // DLL 的 wrapFullHtml 已输出完整 HTML 文档, 直接使用
                    QString styledHtml = pageHtml;
                    showResultHtml(pageHtml, pageText);
                    if (m_autoSave && !styledHtml.isEmpty()) {
                        saveToHistoryHtml(processedPixmap, styledHtml);
                    }
                    if (wantOutput && !styledHtml.isEmpty()) {
                        QString htmlPath = QFileDialog::getSaveFileName(this, "保存 HTML 表格",
                            QFileInfo(filePath).absolutePath() + "/" + QFileInfo(filePath).completeBaseName() + "_table.html",
                            "HTML 文件 (*.html)");
                        if (!htmlPath.isEmpty()) {
                            QFile f(htmlPath);
                            if (f.open(QIODevice::WriteOnly)) {
                                f.write(styledHtml.toUtf8());
                                f.close();
                                QMessageBox::information(this, "成功", "HTML 表格已保存至:\n" + htmlPath);
                            }
                        }
                    }
                    if (wantXlsx && !styledHtml.isEmpty()) {
                        QString xlsxPath = QFileDialog::getSaveFileName(this, "保存 XLSX 表格",
                            QFileInfo(filePath).absolutePath() + "/" + QFileInfo(filePath).completeBaseName() + "_table.xlsx",
                            "Excel 文件 (*.xlsx)");
                        if (!xlsxPath.isEmpty()) {
                            if (htmlToXlsx(styledHtml.toStdString(), xlsxPath.toStdString())) {
                                QMessageBox::information(this, "成功", "XLSX 表格已保存至:\n" + xlsxPath);
                            } else {
                                QMessageBox::warning(this, "警告", "XLSX 生成失败");
                            }
                        }
                    }
                } else {
                    std::vector<OcrBlock> blocks = m_ocr->detectWithCoords(imageData);
                    if (m_layoutMode && !blocks.empty()) {
                        applyLayout(blocks, m_layoutStrategy);
                    }
                    // 坐标模式：显示带坐标的结果
                    if (m_coordMode) {
                        showResultWithCoords(blocks);
                    } else {
                        QString allText;
                        for (const OcrBlock& block : blocks) {
                            allText += QString::fromStdString(block.text);
                        }
                        if (!allText.isEmpty()) {
                            showResult(allText);
                        }
                    }
                    if (m_autoSave) {
                        QString allText;
                        for (const OcrBlock& block : blocks) {
                            allText += QString::fromStdString(block.text);
                        }
                        saveToHistoryWithPrefix(processedPixmap, allText, 
                            QFileInfo(filePath).completeBaseName() + "_page1");
                    }
                    if (wantOutput && !blocks.empty()) {
                        QString pdfPath = QFileDialog::getSaveFileName(this, "保存可搜索 PDF",
                            QFileInfo(filePath).absolutePath() + "/" + QFileInfo(filePath).completeBaseName() + "_ocr.pdf",
                            "PDF 文件 (*.pdf)");
                        if (!pdfPath.isEmpty()) {
                            QVector<QPair<int, std::vector<OcrBlock>>> pageBlocks;
                            pageBlocks.append(qMakePair(0, blocks));
                            if (PdfTextLayer::modifyPdfWithOcrText(filePath, pdfPath, pageBlocks)) {
                                QMessageBox::information(this, "成功", "可搜索 PDF 已保存至:\n" + pdfPath);
                            } else {
                                QMessageBox::warning(this, "警告", "生成 PDF 失败");
                            }
                        }
                    }
                }
            } else {
                QMessageBox::warning(this, "警告", "无法渲染PDF页面");
            }
        }
        return;
    }

    // 多页 PDF: 弹窗让用户选择页码
    QDialog dialog(this);
    dialog.setWindowTitle("选择PDF页码");
    dialog.setMinimumWidth(350);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* label = new QLabel(QString("PDF共 %1 页\n请输入要识别的页码（支持格式：1, 2-5, all）：").arg(pageCount));
    layout->addWidget(label);

    QLineEdit* inputEdit = new QLineEdit();
    inputEdit->setPlaceholderText("例如: 1, 2-5, 6-10 或 all");
    layout->addWidget(inputEdit);

    // 添加帮助提示
    QLabel* helpLabel = new QLabel("格式说明：单个页码用逗号分隔，范围用连字符，all 表示全部");
    helpLabel->setStyleSheet("color: gray; font-size: 10px;");
    layout->addWidget(helpLabel);

    // 添加输出选项复选框 (表格模式下改为输出 HTML)
    QCheckBox* outputCheckBox = new QCheckBox(tableMode ? "输出 HTML" : "输出可搜索 PDF");
    outputCheckBox->setToolTip(tableMode ? "生成带边框的 HTML 表格文件" : "生成带文字层的 PDF，可在阅读器中搜索/复制文字");
    layout->addWidget(outputCheckBox);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    bool wantXlsx = false;
    if (dialog.exec() == QDialog::Accepted) {
        QString input = inputEdit->text().trimmed();
        
        // 解析页码输入
        QVector<int> pageIndices;
        
        // 统一替换全角逗号为半角逗号
        input.replace(QString::fromUtf8("，"), ",");
        
        if (input.isEmpty() || input.toLower() == "all") {
            // 全部页面
            for (int i = 0; i < pageCount; i++) {
                pageIndices.append(i);
            }
        } else {
            // 解析页码列表
            QStringList parts = input.split(",", Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                QString trimmedPart = part.trimmed();
                if (trimmedPart.contains("-")) {
                    // 范围格式: 2-5
                    QStringList rangeParts = trimmedPart.split("-", Qt::SkipEmptyParts);
                    if (rangeParts.size() == 2) {
                        int start = rangeParts[0].trimmed().toInt();
                        int end = rangeParts[1].trimmed().toInt();
                        if (start > 0 && end > 0 && start <= end) {
                            for (int p = start; p <= end && p <= pageCount; p++) {
                                pageIndices.append(p - 1);  // 转为0-based
                            }
                        }
                    }
                } else {
                    // 单个页码
                    int pageNum = trimmedPart.toInt();
                    if (pageNum > 0 && pageNum <= pageCount) {
                        pageIndices.append(pageNum - 1);  // 转为0-based
                    }
                }
            }
        }
        
        // 去重并排序
        std::sort(pageIndices.begin(), pageIndices.end());
        pageIndices.erase(std::unique(pageIndices.begin(), pageIndices.end()), pageIndices.end());
        
        if (pageIndices.isEmpty()) {
            QMessageBox::warning(this, "警告", "无效的页码输入");
            PdfHelper::close();
            return;
        }
        
        // 是否输出 (PDF 或 HTML)
        bool wantOutput = outputCheckBox->isChecked();
        if (tableMode && wantOutput && !wantXlsx) {
            QMessageBox xlsxMsg(this);
            xlsxMsg.setWindowTitle("XLSX输出");
            xlsxMsg.setText("是否同时输出 XLSX 表格文件？");
            xlsxMsg.setIcon(QMessageBox::Question);
            xlsxMsg.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            xlsxMsg.setDefaultButton(QMessageBox::Yes);
            xlsxMsg.button(QMessageBox::Yes)->setText("是");
            xlsxMsg.button(QMessageBox::No)->setText("否");
            wantXlsx = (xlsxMsg.exec() == QMessageBox::Yes);
        }
        QVector<QPair<QByteArray, std::vector<OcrBlock>>> pdfPages;
        
        QVector<std::vector<OcrBlock>> ocrResults;
        
        // 渲染并识别选中的页面
        QString allText;
        QString allHtml;  // 表格模式 HTML 累积
        std::vector<OcrBlock> allBlocks;  // 坐标模式 blocks 累积
        bool coordMode = m_coordMode;  // 保存坐标模式状态
        for (int idx : pageIndices) {
            QPixmap pixmap = PdfHelper::renderPage(idx);
            if (!pixmap.isNull()) {
                // 临时保存当前 pixmap，调用 processImage
                // 由于 processImage 是异步的，我们需要同步处理
                QByteArray byteArray;
                QBuffer buffer(&byteArray);
                buffer.open(QIODevice::WriteOnly);
                
                QPixmap processedPixmap = pixmap;
                if (pixmap.hasAlpha()) {
                    processedPixmap = QPixmap(pixmap.size());
                    processedPixmap.fill(Qt::white);
                    QPainter painter(&processedPixmap);
                    painter.drawPixmap(0, 0, pixmap);
                    painter.end();
                }
                
                processedPixmap.save(&buffer, "BMP");
                buffer.close();
                
                std::vector<unsigned char> imageData(byteArray.begin(), byteArray.end());

                if (tableMode) {
                    auto tableResult = m_ocr->detectTable(imageData);
                    QString pageHtml = QString::fromStdString(tableResult.htmlStructure);
                    QString pageText = QString::fromStdString(tableResult.ocrText);
                    allText += pageText;
                    if (pageIndices.size() > 1) {
                        allText += "\n--- 第 " + QString::number(idx + 1) + " 页 ---\n";
                    }
                    // 累积 HTML
                    allHtml += QString("<p><b>第%1页</b></p>").arg(idx + 1) + pageHtml;
                    if (m_autoSave) {
                        saveToHistoryHtml(processedPixmap, pageHtml);
                    }
                } else {
                    std::vector<OcrBlock> blocks = m_ocr->detectWithCoords(imageData);
                    if (m_layoutMode && !blocks.empty()) {
                        applyLayout(blocks, m_layoutStrategy);
                    }
                    QString pageText;
                    for (const OcrBlock& block : blocks) {
                        pageText += QString::fromStdString(block.text);
                    }
                    allText += pageText;
                    if (pageIndices.size() > 1) {
                        allText += "\n--- 第 " + QString::number(idx + 1) + " 页 ---\n";
                    }
                    if (coordMode) {
                        // 坐标模式：累积所有 blocks
                        allBlocks.insert(allBlocks.end(), blocks.begin(), blocks.end());
                    }
                    if (m_autoSave) {
                        saveToHistoryWithPrefix(processedPixmap, pageText, 
                            QFileInfo(filePath).completeBaseName() + "_page" + QString::number(idx + 1));
                    }
                    if (wantOutput) {
                        ocrResults.append(blocks);
                    }
                }
            }
        }
        
        PdfHelper::close();
        
        // 输出结果
        if (tableMode && !allHtml.isEmpty()) {
            QString styledHtml;
            QString trimmed = allHtml.trimmed().toLower();
            if (trimmed.startsWith("<!doctype") || trimmed.startsWith("<html")) {
                styledHtml = allHtml;
            } else {
                styledHtml = QString(
                    "<html><head><meta charset=\"UTF-8\">"
                    "<style>table{border-collapse:collapse;width:100%%}"
                    "td,th{border:1px solid black;padding:8px;text-align:left}</style>"
                    "</head><body>%1</body></html>"
                ).arg(allHtml);
            }
            showResultHtml(allHtml, allText);
            if (wantOutput) {
                QString htmlPath = QFileDialog::getSaveFileName(this, "保存 HTML 表格",
                    QFileInfo(filePath).absolutePath() + "/" + QFileInfo(filePath).completeBaseName() + "_table.html",
                    "HTML 文件 (*.html)");
                if (!htmlPath.isEmpty()) {
                    QFile f(htmlPath);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(styledHtml.toUtf8());
                        f.close();
                        QMessageBox::information(this, "成功", "HTML 表格已保存至:\n" + htmlPath);
                    }
                }
            }
            if (wantXlsx && !styledHtml.isEmpty()) {
                QString xlsxPath = QFileDialog::getSaveFileName(this, "保存 XLSX 表格",
                    QFileInfo(filePath).absolutePath() + "/" + QFileInfo(filePath).completeBaseName() + "_table.xlsx",
                    "Excel 文件 (*.xlsx)");
                if (!xlsxPath.isEmpty()) {
                    if (htmlToXlsx(styledHtml.toStdString(), xlsxPath.toStdString())) {
                        QMessageBox::information(this, "成功", "XLSX 表格已保存至:\n" + xlsxPath);
                    } else {
                        QMessageBox::warning(this, "警告", "XLSX 生成失败");
                    }
                }
            }
        } else if (wantOutput && !ocrResults.isEmpty()) {
            QString pdfPath = QFileDialog::getSaveFileName(this, "保存可搜索 PDF",
                QFileInfo(filePath).absolutePath() + "/" + QFileInfo(filePath).completeBaseName() + "_ocr.pdf",
                "PDF 文件 (*.pdf)");
            if (!pdfPath.isEmpty()) {
                QVector<QPair<int, std::vector<OcrBlock>>> pageBlocks;
                for (int i = 0; i < pageIndices.size(); i++) {
                    if (i < ocrResults.size()) {
                        pageBlocks.append(qMakePair(pageIndices[i], ocrResults[i]));
                    }
                }
                if (PdfTextLayer::modifyPdfWithOcrText(filePath, pdfPath, pageBlocks)) {
                    QMessageBox::information(this, "成功", "可搜索 PDF 已保存至:\n" + pdfPath);
                } else {
                    QMessageBox::warning(this, "警告", "生成 PDF 失败");
                }
            }
        }
        
        // 显示识别结果 (表格模式已在上面 showResultHtml 中显示, 不要重复)
        if (!tableMode) {
            if (coordMode && !allBlocks.empty()) {
                // 坐标模式：显示带坐标的结果
                showResultWithCoords(allBlocks);
            } else if (!allText.isEmpty()) {
                showResult(allText);
            } else {
                QMessageBox::warning(this, "警告", "无法渲染PDF页面");
            }
        }
    }
}

// 批量处理多个文件（图片+PDF混合）
void MainWindow::processFiles(const QStringList& files) {
    GL("processFiles: enter");
    resetOcrIdleTimer();
    if (!m_ocr) {
        QMessageBox::warning(this, "警告", "OCR引擎未初始化");
        return;
    }

    if (files.isEmpty()) {
        return;
    }

    // 表格模式: 静态库已包含表格 API, 无需 DLL

    // 单个文件：直接处理
    if (files.size() == 1) {
        QString filePath = files[0];
        GL("processFiles: single file");
        if (PdfHelper::isPdfFile(filePath)) {
            GL("processFiles: PDF");
            processPdfFile(filePath);
        } else {
            GL("processFiles: loading pixmap...");
            QPixmap pixmap(filePath);
            GL("processFiles: pixmap loaded");
            if (!pixmap.isNull()) {
                GL("processFiles: calling processImage...");
                processImage(pixmap);
            } else {
                QMessageBox::warning(this, "警告", "无法加载图片: " + filePath);
            }
        }
        return;
    }

    // 多文件：收集 PDF 文件信息，弹窗让用户设置
    QStringList pdfFiles;
    QStringList imageFiles;
    for (const QString& f : files) {
        if (PdfHelper::isPdfFile(f)) {
            pdfFiles.append(f);
        } else {
            imageFiles.append(f);
        }
    }

    // 如果有 PDF 文件，弹窗让用户选择页码
    QMap<QString, QVector<int>> pdfPageSelections;  // PDF 文件 -> 选中的页码
    bool outputPdf = false;
    bool tableMode = (m_tableMode != 0);

    if (!pdfFiles.isEmpty()) {
        // 创建批量 PDF 页码选择对话框
        QDialog dialog(this);
        dialog.setWindowTitle("批量PDF页码选择");
        dialog.setMinimumWidth(500);
        QVBoxLayout* mainLayout = new QVBoxLayout(&dialog);

        QLabel* titleLabel = new QLabel(QString("共 %1 个PDF文件，请为每个PDF设置要识别的页码：").arg(pdfFiles.size()));
        mainLayout->addWidget(titleLabel);

        // 为每个 PDF 创建页码输入行
        QMap<QString, QLineEdit*> pdfInputs;
        for (const QString& pdfPath : pdfFiles) {
            // 加载 PDF 获取页数
            if (!PdfHelper::load(pdfPath)) {
                continue;
            }
            int pageCount = PdfHelper::pageCount();
            PdfHelper::close();

            QHBoxLayout* rowLayout = new QHBoxLayout();
            QLabel* nameLabel = new QLabel(QFileInfo(pdfPath).fileName() + QString(" (%1页):").arg(pageCount));
            nameLabel->setMinimumWidth(150);
            rowLayout->addWidget(nameLabel);

            QLineEdit* inputEdit = new QLineEdit();
            inputEdit->setPlaceholderText("all 或 1,2-5");
            inputEdit->setText("all");  // 默认全部
            inputEdit->setToolTip(pdfPath);
            rowLayout->addWidget(inputEdit);

            pdfInputs[pdfPath] = inputEdit;
            mainLayout->addLayout(rowLayout);
        }

        // 添加输出选项复选框 (表格模式下改为输出 HTML)
        QCheckBox* outputCheckBox2 = new QCheckBox(tableMode ? "输出 HTML" : "输出可搜索 PDF");
        outputCheckBox2->setToolTip(tableMode ? "生成带边框的 HTML 表格文件" : "生成带文字层的 PDF，可在阅读器中搜索/复制文字");
        mainLayout->addWidget(outputCheckBox2);

        // 添加帮助提示
        QLabel* helpLabel = new QLabel("格式说明：单个页码用逗号分隔，范围用连字符，all 表示全部");
        helpLabel->setStyleSheet("color: gray; font-size: 10px;");
        mainLayout->addWidget(helpLabel);

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        mainLayout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        outputPdf = outputCheckBox2->isChecked();

        // 解析每个 PDF 的页码输入
        for (const QString& pdfPath : pdfFiles) {
            if (!pdfInputs.contains(pdfPath)) continue;
            QString input = pdfInputs[pdfPath]->text().trimmed();
            input.replace(QString::fromUtf8("，"), ",");

            // 加载 PDF 获取页数
            if (!PdfHelper::load(pdfPath)) continue;
            int pageCount = PdfHelper::pageCount();
            PdfHelper::close();

            QVector<int> pageIndices;
            if (input.isEmpty() || input.toLower() == "all") {
                for (int i = 0; i < pageCount; i++) {
                    pageIndices.append(i);
                }
            } else {
                QStringList parts = input.split(",", Qt::SkipEmptyParts);
                for (const QString& part : parts) {
                    QString trimmedPart = part.trimmed();
                    if (trimmedPart.contains("-")) {
                        QStringList rangeParts = trimmedPart.split("-", Qt::SkipEmptyParts);
                        if (rangeParts.size() == 2) {
                            int start = rangeParts[0].trimmed().toInt();
                            int end = rangeParts[1].trimmed().toInt();
                            if (start > 0 && end > 0 && start <= end) {
                                for (int p = start; p <= end && p <= pageCount; p++) {
                                    pageIndices.append(p - 1);
                                }
                            }
                        }
                    } else {
                        int pageNum = trimmedPart.toInt();
                        if (pageNum > 0 && pageNum <= pageCount) {
                            pageIndices.append(pageNum - 1);
                        }
                    }
                }
            }
            std::sort(pageIndices.begin(), pageIndices.end());
            pageIndices.erase(std::unique(pageIndices.begin(), pageIndices.end()), pageIndices.end());
            pdfPageSelections[pdfPath] = pageIndices;
        }
    }

    // 开始批量处理
    QString allText;
    int totalFiles = imageFiles.size();
    for (const QString& pdfPath : pdfFiles) {
        if (pdfPageSelections.contains(pdfPath)) {
            totalFiles += pdfPageSelections[pdfPath].size();
        }
    }

    int processedCount = 0;
    QString allHtml;  // 表格模式 HTML 累积
    std::vector<OcrBlock> allBlocks;  // 坐标模式 blocks 累积
    bool coordMode = m_coordMode;  // 保存坐标模式状态

    // 处理图片文件
    for (const QString& imgPath : imageFiles) {
        processedCount++;
        m_ocrStatusLabel->setText(QString("正在处理 %1/%2: %3").arg(processedCount).arg(totalFiles).arg(QFileInfo(imgPath).fileName()));
        QApplication::processEvents();

        QPixmap pixmap(imgPath);
        if (pixmap.isNull()) {
            allText += QString("\n[错误] 无法加载图片: %1\n").arg(QFileInfo(imgPath).fileName());
            continue;
        }

        QPixmap processedPixmap = pixmap;
        if (pixmap.hasAlpha()) {
            processedPixmap = QPixmap(pixmap.size());
            processedPixmap.fill(Qt::white);
            QPainter painter(&processedPixmap);
            painter.drawPixmap(0, 0, pixmap);
            painter.end();
        }

        QByteArray byteArray;
        QBuffer buffer(&byteArray);
        buffer.open(QIODevice::WriteOnly);
        processedPixmap.save(&buffer, "BMP");
        buffer.close();

        std::vector<unsigned char> imageData(byteArray.begin(), byteArray.end());

        if (tableMode) {
            GL("processFiles: calling detectTable...");
            auto tableResult = m_ocr->detectTable(imageData);
            char buf2[256];
            snprintf(buf2, sizeof(buf2), "processFiles: detectTable done, html.size=%d, text.size=%d",
                     (int)tableResult.htmlStructure.size(), (int)tableResult.ocrText.size());
            GL(buf2);
            QString pageHtml = QString::fromStdString(tableResult.htmlStructure);
            QString pageText = QString::fromStdString(tableResult.ocrText);
            allHtml += QString("<p><b>%1</b></p>").arg(QFileInfo(imgPath).fileName());
            allHtml += pageHtml;
            allText += QString("\n=== %1 ===\n").arg(QFileInfo(imgPath).fileName());
            allText += pageText;
            if (m_autoSave) {
                saveToHistoryHtml(processedPixmap, allHtml);
            }
        } else {
            std::vector<OcrBlock> blocks = m_ocr->detectWithCoords(imageData);
            if (m_layoutMode && !blocks.empty()) {
                applyLayout(blocks, m_layoutStrategy);
            }
            QString imgText;
            for (const OcrBlock& block : blocks) {
                imgText += QString::fromStdString(block.text);
            }
            allText += QString("\n=== %1 ===\n").arg(QFileInfo(imgPath).fileName());
            allText += imgText;
            if (coordMode) {
                // 坐标模式：累积所有 blocks
                allBlocks.insert(allBlocks.end(), blocks.begin(), blocks.end());
            }
            if (m_autoSave) {
                saveToHistoryWithPrefix(processedPixmap, imgText, QFileInfo(imgPath).completeBaseName());
            }
        }
    }

    // 处理 PDF 文件（每个 PDF 单独输出）
    for (const QString& pdfPath : pdfFiles) {
        if (!pdfPageSelections.contains(pdfPath)) continue;
        QVector<int> pageIndices = pdfPageSelections[pdfPath];
        if (pageIndices.isEmpty()) continue;

        if (!PdfHelper::load(pdfPath)) {
            allText += QString("\n[错误] 无法加载PDF: %1\n").arg(QFileInfo(pdfPath).fileName());
            continue;
        }

        allText += QString("\n=== %1 ===\n").arg(QFileInfo(pdfPath).fileName());

        QVector<std::vector<OcrBlock>> ocrResults;

        for (int idx : pageIndices) {
            processedCount++;
            m_ocrStatusLabel->setText(QString("正在处理 %1/%2: %3 第%4页").arg(processedCount).arg(totalFiles).arg(QFileInfo(pdfPath).fileName()).arg(idx + 1));
            QApplication::processEvents();

            QPixmap pixmap = PdfHelper::renderPage(idx);
            if (pixmap.isNull()) {
                allText += QString("\n[错误] 无法渲染第 %1 页\n").arg(idx + 1);
                continue;
            }

            QPixmap processedPixmap = pixmap;
            if (pixmap.hasAlpha()) {
                processedPixmap = QPixmap(pixmap.size());
                processedPixmap.fill(Qt::white);
                QPainter painter(&processedPixmap);
                painter.drawPixmap(0, 0, pixmap);
                painter.end();
            }

            QByteArray byteArray;
            QBuffer buffer(&byteArray);
            buffer.open(QIODevice::WriteOnly);
            processedPixmap.save(&buffer, "BMP");
            buffer.close();

            std::vector<unsigned char> imageData(byteArray.begin(), byteArray.end());

            if (tableMode) {
                GL("processFiles: PDF page detectTable...");
                auto tableResult = m_ocr->detectTable(imageData);
                GL("processFiles: PDF page detectTable done");
                QString pageHtml = QString::fromStdString(tableResult.htmlStructure);
                QString pageText = QString::fromStdString(tableResult.ocrText);
                allHtml += QString("<p><b>%1 第%2页</b></p>").arg(QFileInfo(pdfPath).fileName()).arg(idx + 1);
                allHtml += pageHtml;
                allText += QString("\n--- 第 %1 页 ---\n").arg(idx + 1);
                allText += pageText;
                if (m_autoSave) {
                    saveToHistoryHtml(processedPixmap, allHtml);
                }
            } else {
                std::vector<OcrBlock> blocks = m_ocr->detectWithCoords(imageData);
                if (m_layoutMode && !blocks.empty()) {
                    applyLayout(blocks, m_layoutStrategy);
                }
                QString pageText;
                for (const OcrBlock& block : blocks) {
                    pageText += QString::fromStdString(block.text);
                }
                allText += QString("\n--- 第 %1 页 ---\n").arg(idx + 1);
                allText += pageText;
                if (coordMode) {
                    // 坐标模式：累积所有 blocks
                    allBlocks.insert(allBlocks.end(), blocks.begin(), blocks.end());
                }
                if (m_autoSave) {
                    saveToHistoryWithPrefix(processedPixmap, pageText, 
                        QFileInfo(pdfPath).completeBaseName() + "_page" + QString::number(idx + 1));
                }
                if (outputPdf) {
                    // 收集 blocks 用于 modifyPdfWithOcrText (和单文件逻辑一致)
                    ocrResults.append(blocks);
                }
            }
        }

        PdfHelper::close();

        // 当前 PDF 单独输出可搜索 PDF（弹窗选择路径）
        if (outputPdf && !ocrResults.isEmpty()) {
            QString defaultPath = QFileInfo(pdfPath).absolutePath() + "/" + 
                QFileInfo(pdfPath).completeBaseName() + "_ocr.pdf";
            QString pdfOutPath = QFileDialog::getSaveFileName(this, "保存可搜索 PDF - " + QFileInfo(pdfPath).fileName(),
                defaultPath, "PDF 文件 (*.pdf)");
            
            if (!pdfOutPath.isEmpty()) {
                QVector<QPair<int, std::vector<OcrBlock>>> pageBlocks;
                for (int i = 0; i < pageIndices.size(); i++) {
                    if (i < ocrResults.size()) {
                        pageBlocks.append(qMakePair(pageIndices[i], ocrResults[i]));
                    }
                }
                if (PdfTextLayer::modifyPdfWithOcrText(pdfPath, pdfOutPath, pageBlocks)) {
                    allText += QString("\n[PDF] 已保存: %1 (%2页)\n").arg(QFileInfo(pdfOutPath).fileName()).arg(pageIndices.size());
                } else {
                    allText += QString("\n[错误] 生成 PDF 失败: %1\n").arg(QFileInfo(pdfPath).fileName());
                }
            } else {
                allText += QString("\n[PDF] 用户取消保存: %1\n").arg(QFileInfo(pdfPath).fileName());
            }
        }
    }

    // 显示合并结果
    if (tableMode && !allHtml.isEmpty()) {
        // 表格模式: 显示 HTML
        showResultHtml(allHtml, allText);
    } else if (coordMode && !allBlocks.empty()) {
        // 坐标模式：显示带坐标的结果
        showResultWithCoords(allBlocks);
    } else if (!allText.isEmpty()) {
        showResult(allText);
    }

    m_ocrStatusLabel->setText(QString("批量处理完成，共 %1 个文件").arg(totalFiles));
}

// ========================================================================
// ⚠️ processImage 中 QPixmap 加载 JPEG 依赖 Qt 内置 JPEG 支持
//   如果链接了 OpenCV libjpeg-turbo, 此处会闪退 (符号冲突)
//   解决方案: gen_libs_pri.py 已排除 libjpeg-turbo.lib
//   参见: gen_libs_pri.py, rename_jpeg_symbols.py
// ========================================================================
void MainWindow::processImage(const QPixmap& pixmap) {
    GL("processImage: ===ENTER===");
    resetOcrIdleTimer();
    if (!m_ocr) {
        GL("processImage: no OCR engine");
        return;
    }

    if (m_ocrRunning) {
        m_ocrStatusLabel->setText("上一次识别尚未完成，请稍候...");
        return;
    }

    m_currentPixmap = pixmap;
    m_ocrRunning = true;
    m_screenshotBtn->setEnabled(false);
    m_fileBtn->setEnabled(false);
    m_ocrStatusLabel->setText("正在识别...");

    // 转换为 BMP 格式 (主线程完成，避免工作线程操作 QPixmap)
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);

    QPixmap processedPixmap = pixmap;
    if (pixmap.hasAlpha()) {
        processedPixmap = QPixmap(pixmap.size());
        processedPixmap.fill(Qt::white);
        QPainter painter(&processedPixmap);
        painter.drawPixmap(0, 0, pixmap);
        painter.end();
    }

    processedPixmap.save(&buffer, "BMP");
    buffer.close();

    std::vector<unsigned char> imageData(byteArray.begin(), byteArray.end());
    bool coordMode = m_coordMode;
    bool layoutMode = m_layoutMode;
    bool tableMode = (m_tableMode != 0);
    std::string layoutStrategy = m_layoutStrategy;

    // 设置 OCR 旋转矫正状态
    if (m_ocr) {
        m_ocr->setDoAngle(m_doAngle ? 1 : 0);
    }

    // 表格模式: 静态库已包含表格 API

    char buf[256];
    snprintf(buf, sizeof(buf), "processImage: launching worker, tableMode=%d coordMode=%d layoutMode=%d", tableMode, coordMode, layoutMode);
    GL(buf);

    // 用 QFuture 在工作线程执行 OCR，避免主线程卡死
    QFuture<std::pair<bool, std::vector<OcrBlock>>> future = QtConcurrent::run(
        [this, imageData, coordMode, layoutMode, layoutStrategy, tableMode]() -> std::pair<bool, std::vector<OcrBlock>> {
            GL("[worker] enter");
            if (!m_ocr) {
                GL("[worker] no OCR engine");
                return {false, {}};
            }
            if (tableMode) {
                GL("[worker] calling detectTable...");
                auto tableResult = m_ocr->detectTable(imageData);
                snprintf(nullptr, 0, "");
                GL("[worker] detectTable done");
                OcrBlock block;
                block.text = tableResult.htmlStructure;
                return {true, {block}};
            }
            GL("[worker] calling detectWithCoords or detect...");
            if (coordMode || layoutMode) {
                auto blocks = m_ocr->detectWithCoords(imageData);
                GL("[worker] detectWithCoords done");
                if (layoutMode && !blocks.empty()) {
                    applyLayout(blocks, layoutStrategy);
                }
                return {true, blocks};
            } else {
                std::string result = m_ocr->detect(imageData);
                GL("[worker] detect done");
                OcrBlock block;
                block.text = result;
                return {true, {block}};
            }
        });

    auto* watcher = new QFutureWatcher<std::pair<bool, std::vector<OcrBlock>>>(this);
    connect(watcher, &QFutureWatcher<std::pair<bool, std::vector<OcrBlock>>>::finished, this,
        [this, watcher, future, coordMode, layoutMode, tableMode]() {
            GL("[callback] enter");
            watcher->deleteLater();
            if (m_ocrTimeoutTimer) {
                m_ocrTimeoutTimer->stop();
                m_ocrTimeoutTimer->deleteLater();
                m_ocrTimeoutTimer = nullptr;
            }
            if (!m_ocrRunning) { GL("[callback] not running, skip"); return; }
            m_ocrRunning = false;
            m_screenshotBtn->setEnabled(true);
            m_fileBtn->setEnabled(true);

            auto result = future.result();
            GL("[callback] got result");
            if (result.first) {
                if (!result.second.empty()) {
                    if (tableMode) {
                        GL("[callback] tableMode, displaying HTML");
                        QString html = QString::fromUtf8(result.second[0].text.c_str());
                        if (!html.isEmpty()) {
                            showResultHtml(html, html);
                        } else {
                            m_resultText->setText("");
                            m_ocrStatusLabel->setText("识别完成(空)");
                        }
                    } else if (coordMode) {
                        GL("[callback] coordMode, showing with coords");
                        showResultWithCoords(result.second);
                    } else {
                        GL("[callback] plain text mode");
                        QString text;
                        for (const auto& block : result.second) {
                            if (!block.text.empty()) {
                                text += QString::fromUtf8(block.text.c_str());
                                if (!block.end.empty()) {
                                    text += QString::fromUtf8(block.end.c_str());
                                }
                            }
                        }
                        showResult(text);
                    }
                } else {
                    m_resultText->setText("");
                    m_ocrStatusLabel->setText("识别完成(空)");
                }
            } else {
                m_ocrStatusLabel->setText("识别失败");
            }
        });
    watcher->setFuture(future);

    // 超时守卫: 30 秒后强制终止 (使用可取消的定时器)
    if (m_ocrTimeoutTimer) {
        m_ocrTimeoutTimer->stop();
        m_ocrTimeoutTimer->deleteLater();
    }
    m_ocrTimeoutTimer = new QTimer(this);
    m_ocrTimeoutTimer->setSingleShot(true);
    connect(m_ocrTimeoutTimer, &QTimer::timeout, this, [this, watcher]() {
        if (m_ocrRunning) {
            m_ocrRunning = false;
            m_screenshotBtn->setEnabled(true);
            m_fileBtn->setEnabled(true);
            m_ocrStatusLabel->setText("识别超时(30秒)，请重试");
        }
    });
    m_ocrTimeoutTimer->start(30000);  // 30秒超时
}

void MainWindow::showResult(const QString& text) {
    m_lastHtml.clear();  // 非表格模式，清除HTML缓存
    if (m_layoutMode && m_layoutStrategy == "single_vertical" && !text.isEmpty()) {
        QString html = QString(
            "<html><head><style>"
            "body { writing-mode: vertical-rl; font-size: 14px; line-height: 1.8; "
            "font-family: 'SimSun', '宋体', serif; }"
            "</style></head><body>%1</body></html>"
        ).arg(text.toHtmlEscaped().replace("\n", "<br>").replace(" ", "&nbsp;"));
        m_resultText->setHtml(html);
    } else {
        m_resultText->setText(text);
    }

    if (!text.isEmpty()) {
        m_ocrStatusLabel->setText("识别完成");

        if (m_autoCopy) {
            copyToClipboard(text);
            m_ocrStatusLabel->setText("识别完成 (已复制到剪贴板)");
        }

        if (!m_currentPixmap.isNull()) {
            saveToHistory(m_currentPixmap, text);
        }
    } else {
        m_ocrStatusLabel->setText("识别完成(空)");
    }
}

void MainWindow::showResultHtml(const QString& html, const QString& plainText) {
    GL("showResultHtml: enter");
    m_resultText->setText(plainText);
    GL("showResultHtml: setText done");

    if (!html.isEmpty()) {
        m_ocrStatusLabel->setText("识别完成 (表格)");

        // 如果 html 已经是完整 HTML 文档 (wrapFullHtml 输出), 直接使用, 避免双重包装
        QString styledHtml;
        QString trimmed = html.trimmed().toLower();
        if (trimmed.startsWith("<!doctype") || trimmed.startsWith("<html")) {
            styledHtml = html;
        } else {
            styledHtml = QString(
                "<html><head><meta charset=\"UTF-8\">"
                "<style>table{border-collapse:collapse;width:100%%}"
                "td,th{border:1px solid black;padding:8px;text-align:left}</style>"
                "</head><body>%1</body></html>"
            ).arg(html);
        }

        if (m_autoCopy) {
            GL("showResultHtml: copying to clipboard...");
            copyHtmlToClipboard(styledHtml);
            m_ocrStatusLabel->setText("识别完成 (表格, Excel全选粘贴可保留格式)");
            GL("showResultHtml: clipboard done");
        }

        if (!m_currentPixmap.isNull()) {
            saveToHistoryHtml(m_currentPixmap, styledHtml);
        }

        GL("showResultHtml: calling setHtml...");
        try {
            m_resultText->setHtml(styledHtml);
            GL("showResultHtml: setHtml done");
        // 保存最近的HTML用于右键"另存为XLSX"
        m_lastHtml = styledHtml;
        } catch (...) {
            GL("showResultHtml: setHtml FAILED");
        }
    } else {
        m_ocrStatusLabel->setText("识别完成(空)");
    }
}

void MainWindow::showResultWithCoords(const std::vector<OcrBlock>& blocks) {
    GL("showResultWithCoords: enter, blocks.size=" + QByteArray::number(blocks.size()));
    QStringList outputParts;
    QStringList coordParts;

    QStringList angleNames = {"0°", "90°", "180°", "270°"};

    for (size_t i = 0; i < blocks.size(); i++) {
        const OcrBlock& block = blocks[i];

        if (!block.text.empty()) {
            outputParts.append(QString::fromUtf8(block.text.c_str()));
        }

        if (block.box.size() >= 8) {
            QString boxStr = QString("(%1,%2) - (%3,%4) - (%5,%6) - (%7,%8)")
                .arg(block.box[0]).arg(block.box[1])
                .arg(block.box[2]).arg(block.box[3])
                .arg(block.box[4]).arg(block.box[5])
                .arg(block.box[6]).arg(block.box[7]);

            QString angleStr = (block.angle >= 0 && block.angle < 4) ? angleNames[block.angle] : "未知";

            coordParts.append(QString("文本块%1: %2\n  置信度: %3\n  坐标: %4\n  角度: %5 (置信度: %6)")
                .arg(i + 1)
                .arg(QString::fromUtf8(block.text.c_str()))
                .arg(block.score, 0, 'f', 4)
                .arg(boxStr)
                .arg(angleStr)
                .arg(block.angleScore, 0, 'f', 4));
        }
    }

    QString resultText = outputParts.join('\n');
    QString coordText = coordParts.join("\n\n");

    QString fullResult;
    if (!coordText.isEmpty()) {
        fullResult = QString("识别结果:\n%1\n\n详细信息:\n%2").arg(resultText, coordText);
    } else {
        fullResult = resultText;
    }

    m_resultText->setText(fullResult);
    m_ocrStatusLabel->setText(QString("识别完成 (%1个文本块)").arg(blocks.size()));

    if (m_autoCopy && !resultText.isEmpty()) {
        copyToClipboard(resultText);
        m_ocrStatusLabel->setText("识别完成 (已复制到剪贴板)");
    }

    // 自动保存
    if (!m_currentPixmap.isNull() && !resultText.isEmpty()) {
        saveToHistory(m_currentPixmap, fullResult);
    }
}

void MainWindow::saveToHistory(const QPixmap& pixmap, const QString& text) {
    if (!m_autoSave) {
        return;
    }

    QString savePath = m_autoSavePath;
    if (savePath.isEmpty()) {
        savePath = QDir::currentPath() + "/history";
    }

    // 确保目录存在
    QDir dir(savePath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");

    // 保存图片
    QString imagePath = savePath + "/screenshot_" + timestamp + ".png";
    if (pixmap.save(imagePath, "PNG")) {
        std::cout << "图片已保存: " << imagePath.toStdString() << std::endl;
    }

    // 保存文本
    QString textPath = savePath + "/screenshot_" + timestamp + ".txt";
    QFile textFile(textPath);
    if (textFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&textFile);
        stream << text;
        textFile.close();
        std::cout << "文本已保存: " << textPath.toStdString() << std::endl;
    }

    m_ocrStatusLabel->setText("已保存");
}

void MainWindow::saveToHistoryHtml(const QPixmap& pixmap, const QString& html) {
    if (!m_autoSave) return;

    QString savePath = m_autoSavePath;
    if (savePath.isEmpty()) savePath = QDir::currentPath() + "/history";

    QDir dir(savePath);
    if (!dir.exists()) dir.mkpath(".");

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");

    pixmap.save(savePath + "/screenshot_" + timestamp + ".png", "PNG");

    QFile htmlFile(savePath + "/screenshot_" + timestamp + ".html");
    if (htmlFile.open(QIODevice::WriteOnly)) {
        // 直接写 UTF-8 字节, 不经过 QTextStream 编码转换
        htmlFile.write(html.toUtf8());
        htmlFile.close();
    }
    // 同时生成 XLSX
    QString xlsxPath = savePath + "/screenshot_" + timestamp + ".xlsx";
    htmlToXlsx(html.toStdString(), xlsxPath.toStdString());
    m_ocrStatusLabel->setText("已保存");
}

// 带前缀保存（用于批量处理）
void MainWindow::saveToHistoryWithPrefix(const QPixmap& pixmap, const QString& text, const QString& prefix) {
    if (!m_autoSave) {
        return;
    }

    QString savePath = m_autoSavePath;
    if (savePath.isEmpty()) {
        savePath = QDir::currentPath() + "/history";
    }

    // 确保目录存在
    QDir dir(savePath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");

    // 保存图片（带前缀）
    QString imagePath = savePath + "/" + prefix + "_" + timestamp + ".png";
    if (pixmap.save(imagePath, "PNG")) {
        std::cout << "图片已保存: " << imagePath.toStdString() << std::endl;
    }

    // 保存文本（带前缀）
    QString textPath = savePath + "/" + prefix + "_" + timestamp + ".txt";
    QFile textFile(textPath);
    if (textFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&textFile);
        stream << text;
        textFile.close();
        std::cout << "文本已保存: " << textPath.toStdString() << std::endl;
    }
}

void MainWindow::copyToClipboard(const QString& text) {
    QApplication::clipboard()->setText(text);
}

static QString toExcelClipboardHtml(const QString& html) {
    // 从完整 HTML 中提取所有 <table>，合并为单个 Excel 兼容表格:
    // 1) 去掉 DOCTYPE/head/style/body/div 包裹
    // 2) 百分比列宽行高 → 像素值
    // 3) 每个 <td> 加内联样式 (border/padding/text-align)
    // 4) 多表格合并到一个 <tbody> 中
    // 5) 表格标题 (<p><b>...</b></p>) 转为加粗分隔行

    // 提取所有 <table>...</table>
    QRegularExpression reTable("<table[\\s\\S]*?</table>", QRegularExpression::CaseInsensitiveOption);
    auto allTables = reTable.globalMatch(html);
    if (!allTables.hasNext()) return html;

    // 用第一个表格确定列数和列宽
    auto firstTable = allTables.peekNext().captured(0);
    allTables.next();

    int cols = 0;
    QRegularExpression reFirstTr("<tr[^>]*>([\\s\\S]*?)</tr>", QRegularExpression::CaseInsensitiveOption);
    auto firstTrMatch = reFirstTr.match(firstTable);
    if (firstTrMatch.hasMatch()) {
        QRegularExpression reTd("<td[^>]*>", QRegularExpression::CaseInsensitiveOption);
        QRegularExpression reCs("colspan\\s*=\\s*\"?(\\d+)\"?", QRegularExpression::CaseInsensitiveOption);
        auto it = reTd.globalMatch(firstTrMatch.captured(1));
        while (it.hasNext()) {
            auto m = reCs.match(it.next().captured(0));
            cols += m.hasMatch() ? m.captured(1).toInt() : 1;
        }
    }
    if (cols <= 0) cols = 8;

    const double tblW = 690.891;
    const double tblH = 928.984;

    // 解析第一个表格的 <col> 百分比 → 像素
    QList<double> colWidths;
    QRegularExpression reColPct("<col\\s+[^>]*style\\s*=\\s*[\"'][^\"']*width\\s*:\\s*([\\d.]+)%",
                                QRegularExpression::CaseInsensitiveOption);
    auto colIt = reColPct.globalMatch(firstTable);
    while (colIt.hasNext())
        colWidths.append(colIt.next().captured(1).toDouble() / 100.0 * tblW);
    while (colWidths.size() < cols)
        colWidths.append(tblW / cols);

    QString colgroup = "<colgroup>";
    for (double w : colWidths)
        colgroup += QString("<col style=\"width: %1px;\">").arg(w, 0, 'f', 4);
    colgroup += "</colgroup>";

    // 提取所有表格标题 <p><b>...</b></p>，记录每个标题在 html 中的位置
    struct TitleEntry { int pos; QString text; };
    QList<TitleEntry> titles;
    QRegularExpression reTitle("<p[^>]*>\\s*<b[^>]*>([\\s\\S]*?)</b>", QRegularExpression::CaseInsensitiveOption);
    auto titleIt = reTitle.globalMatch(html);
    while (titleIt.hasNext()) {
        auto tm = titleIt.next();
        titles.append({ tm.capturedStart(), tm.captured(1).trimmed() });
    }

    // 处理每个表格的所有 <tr>
    QRegularExpression reTr("<tr[^>]*>([\\s\\S]*?)</tr>", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression reTdOpen("<td([^>]*)>", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression reTdClose("</td>", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression reCsAttr("(colspan|rowspan)\\s*=\\s*(\"[^\"]*\"|'[^']*'|\\S+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression reTrPct("<tr\\s+[^>]*style\\s*=\\s*[\"'][^\"']*height\\s*:\\s*([\\d.]+)%",
                               QRegularExpression::CaseInsensitiveOption);

    auto processRow = [&](const QString& rowContent, double rh) -> QString {
        QString newTr;
        int lastEnd = 0;
        auto tdIt = reTdOpen.globalMatch(rowContent);
        while (tdIt.hasNext()) {
            auto tdMatch = tdIt.next();
            // 提取所有 colspan 和 rowspan 属性
            QString csAttrs;
            auto csIt = reCsAttr.globalMatch(tdMatch.captured(0));
            while (csIt.hasNext())
                csAttrs += " " + csIt.next().captured(0);

            int tdContentStart = tdMatch.capturedEnd();
            int tdEndPos = reTdClose.match(rowContent, tdContentStart).capturedStart();
            QString cellContent = (tdEndPos >= 0)
                ? rowContent.mid(tdContentStart, tdEndPos - tdContentStart).trimmed()
                : rowContent.mid(tdContentStart).trimmed();

            newTr += rowContent.mid(lastEnd, tdMatch.capturedStart() - lastEnd);
            newTr += QString("<td%1 style=\"border: 1px solid black; padding: 8px; text-align: left;\">%2</td>")
                         .arg(csAttrs).arg(cellContent.isEmpty() ? " " : cellContent);
            lastEnd = (tdEndPos >= 0) ? tdEndPos + 5 : rowContent.size();
        }
        newTr += rowContent.mid(lastEnd);
        return QString("<tr style=\"height: %1px;\">%2</tr>").arg(rh, 0, 'f', 4).arg(newTr);
    };

    QString tbody;
    int globalTrIdx = 0;
    int lastTableEnd = -1;

    // 重建表格匹配结果列表（用于位置查找）
    QRegularExpression reTablePos("<table[\\s\\S]*?</table>", QRegularExpression::CaseInsensitiveOption);
    auto tableMatches = reTablePos.globalMatch(html);

    while (tableMatches.hasNext()) {
        auto tableMatch = tableMatches.next();
        QString tableHtml = tableMatch.captured(0);
        int tablePos = tableMatch.capturedStart();

        // 检查这个表格前面是否有标题需要插入
        for (const auto& t : titles) {
            if (t.pos > lastTableEnd && t.pos < tablePos) {
                // 插入标题行 (跨所有列)
                tbody += QString("<tr style=\"height: 30px;\"><td colspan=\"%1\" "
                    "style=\"border: 1px solid black; padding: 8px; text-align: left; "
                    "font-weight: bold; font-size: 14px;\">%2</td></tr>")
                    .arg(cols).arg(t.text);
            }
        }

        // 解析此表格的行高
        QList<double> rowHeights;
        auto trIt = reTrPct.globalMatch(tableHtml);
        while (trIt.hasNext())
            rowHeights.append(trIt.next().captured(1).toDouble() / 100.0 * tblH);

        // 处理每个 <tr>
        auto trIt2 = reTr.globalMatch(tableHtml);
        int localTrIdx = 0;
        while (trIt2.hasNext()) {
            auto trMatch = trIt2.next();
            double rh = (localTrIdx < rowHeights.size())
                ? rowHeights[localTrIdx]
                : tblH / qMax(1, (int)rowHeights.size());
            tbody += processRow(trMatch.captured(1), rh);
            localTrIdx++;
            globalTrIdx++;
        }
        lastTableEnd = tableMatch.capturedEnd();
    }

    // 处理最后一个表格后面的标题（如有）
    for (const auto& t : titles) {
        if (t.pos > lastTableEnd) {
            tbody += QString("<tr style=\"height: 30px;\"><td colspan=\"%1\" "
                "style=\"border: 1px solid black; padding: 8px; text-align: left; "
                "font-weight: bold; font-size: 14px;\">%2</td></tr>")
                .arg(cols).arg(t.text);
        }
    }

    return QString(
        "<table style=\"border-collapse: collapse; table-layout: fixed; width: %1px; height: %2px; "
        "color: rgb(0, 0, 0); font-family: &quot;Noto Sans SC&quot;; font-size: medium; "
        "font-style: normal; font-variant-ligatures: normal; font-variant-caps: normal; "
        "font-weight: 400; letter-spacing: normal; orphans: 2; text-align: start; "
        "text-transform: none; widows: 2; word-spacing: 0px; -webkit-text-stroke-width: 0px; "
        "white-space: normal; text-decoration-thickness: initial; text-decoration-style: initial; "
        "text-decoration-color: initial;\">"
        "%3<tbody>%4</tbody></table>"
    ).arg(tblW, 0, 'f', 3).arg(tblH, 0, 'f', 3).arg(colgroup).arg(tbody);
}

void MainWindow::copyHtmlToClipboard(const QString& html) {
    QString tableFragment = toExcelClipboardHtml(html);

    // 构建完整 CF_HTML 剪贴板数据 (与浏览器复制格式一致)
    QString sourceUrl = "file:///C:/Users/zhuyue/Desktop/RapidOcrEmbed/ocr-qt-cpp/release/output.html";
    QString htmlBody =
        "<html>\r\n"
        "<body>\r\n"
        "<!--StartFragment-->" + tableFragment + "<!--EndFragment-->\r\n"
        "</body>\r\n"
        "</html>";

    QByteArray hdrTemplate =
        "Version:0.9\r\n"
        "StartHTML:0000000000\r\n"
        "EndHTML:0000000000\r\n"
        "StartFragment:0000000000\r\n"
        "EndFragment:0000000000\r\n"
        "SourceURL:" + sourceUrl.toUtf8() + "\r\n"
        "\r\n";

    QByteArray bodyBytes = htmlBody.toUtf8();
    int hdrSize = hdrTemplate.size();
    int startHtml = hdrSize;
    int endHtml = hdrSize + bodyBytes.size();

    QByteArray fragMarker = "<!--StartFragment-->";
    int startFrag = hdrSize + bodyBytes.indexOf(fragMarker) + fragMarker.size();
    QByteArray endMarker = "<!--EndFragment-->";
    int endFrag = hdrSize + bodyBytes.indexOf(endMarker);

    QByteArray hdr = hdrTemplate;
    // 只替换每个字段的10位数字部分，保留字段名和 \r\n 不动
    int pos;
    pos = hdr.indexOf("StartHTML:");
    hdr.replace(pos + 10, 10, QByteArray::number(startHtml).rightJustified(10, '0'));
    pos = hdr.indexOf("EndHTML:");
    hdr.replace(pos + 8, 10, QByteArray::number(endHtml).rightJustified(10, '0'));
    pos = hdr.indexOf("StartFragment:");
    hdr.replace(pos + 14, 10, QByteArray::number(startFrag).rightJustified(10, '0'));
    pos = hdr.indexOf("EndFragment:");
    hdr.replace(pos + 12, 10, QByteArray::number(endFrag).rightJustified(10, '0'));

    QByteArray cfHtml = hdr + bodyBytes;

    // 用 Win32 API 写入剪贴板
    if (OpenClipboard(NULL)) {
        EmptyClipboard();

        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, cfHtml.size() + 1);
        if (hMem) {
            char* pMem = (char*)GlobalLock(hMem);
            memcpy(pMem, cfHtml.constData(), cfHtml.size());
            pMem[cfHtml.size()] = 0;
            GlobalUnlock(hMem);
            UINT cfHtmlFmt = RegisterClipboardFormatA("HTML Format");
            SetClipboardData(cfHtmlFmt, hMem);
        }

        // 同时写入纯文本 (CF_UNICODETEXT), 记事本等粘贴时用
        QTextDocument doc;
        doc.setHtml(tableFragment);
        QString plainText = doc.toPlainText();
        int utf16Size = plainText.size() * 2;
        HGLOBAL hText = GlobalAlloc(GMEM_MOVEABLE, utf16Size + 2);
        if (hText) {
            char* p = (char*)GlobalLock(hText);
            memcpy(p, plainText.utf16(), utf16Size);
            p[utf16Size] = 0;
            p[utf16Size + 1] = 0;
            GlobalUnlock(hText);
            SetClipboardData(CF_UNICODETEXT, hText);
        }

        CloseClipboard();
    } else {
        // fallback: Qt 方式
        QMimeData* mime = new QMimeData();
        mime->setHtml("<html><body><!--StartFragment-->" + tableFragment + "<!--EndFragment--></body></html>");
        QApplication::clipboard()->setMimeData(mime);
    }
}

void MainWindow::toggleAlwaysOnTop() {
    onPinClicked();
}

// ========== 网络服务功能 ==========

// 点击网络图标: 已开启则弹设置, 未开启则弹设置后按选择启动
void MainWindow::onNetClicked() {
    // checked 状态由 setCheckable 自动切换, 这里先还原, 由对话框决定最终状态
    m_netBtn->setChecked(m_httpServer != nullptr);
    showNetSettingsDialog();
}

// 网络服务设置对话框 (开关 + 端口)
void MainWindow::showNetSettingsDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("网络服务设置");
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    // 说明
    QLabel* desc = new QLabel(
        "开启后, 其他设备可通过 HTTP 访问本机进行 OCR 识别。\n"
        "浏览器访问: http://本机IP:端口");
    desc->setStyleSheet("color: #666; font-size: 12px;");
    desc->setWordWrap(true);
    lay->addWidget(desc);

    // 开关
    QCheckBox* enableCheck = new QCheckBox("启用 HTTP 服务");
    enableCheck->setChecked(m_httpServer != nullptr);
    lay->addWidget(enableCheck);

    // 端口
    QHBoxLayout* portLay = new QHBoxLayout();
    QLabel* portLabel = new QLabel("端口:");
    QSpinBox* portSpin = new QSpinBox();
    portSpin->setRange(1, 65535);
    quint16 savedPort = static_cast<quint16>(m_settings->value("http_port", 8080).toUInt());
    if (savedPort == 0) savedPort = 8080;
    portSpin->setValue(m_httpServer ? m_httpServer->port() : savedPort);
    portLay->addWidget(portLabel);
    portLay->addWidget(portSpin);
    portLay->addStretch();
    lay->addLayout(portLay);

    // 接口说明
    QGroupBox* apiGroup = new QGroupBox("可用接口");
    QVBoxLayout* apiLay = new QVBoxLayout(apiGroup);
    QLabel* apiText = new QLabel(
        "GET  /             浏览器界面\n"
        "POST /ocr          multipart 上传 (字段名 file)\n"
        "POST /ocr-raw      body 直接是图片字节\n"
        "POST /screenshot   触发本机截图框选\n"
        "参数: 加 ?mode=coords 返回带坐标");
    apiText->setStyleSheet("color: #555; font-size: 11px; font-family: Consolas, monospace;");
    apiLay->addWidget(apiText);
    lay->addWidget(apiGroup);

    // 按钮
    QDialogButtonBox* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        quint16 port = static_cast<quint16>(portSpin->value());
        m_settings->setValue("http_port", port);
        bool wantOn = enableCheck->isChecked();
        bool nowOn = (m_httpServer != nullptr);

        if (wantOn && (!nowOn || m_httpServer->port() != port)) {
            // 需要启动或重启 (端口变了)
            stopHttpServer();
            startHttpServer(port);
        } else if (!wantOn && nowOn) {
            // 需要停止
            stopHttpServer();
        }
    }
    // 更新按钮选中态
    m_netBtn->setChecked(m_httpServer != nullptr);
}

void MainWindow::showAdvancedSettingsDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("高级设置");
    dlg.setMinimumWidth(400);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    // ═══════════════ 通用参数 (普通OCR和表格共用) ═══════════════
    QGroupBox* commonGroup = new QGroupBox("通用");
    QFormLayout* commonLay = new QFormLayout(commonGroup);

    QCheckBox* doAngleCheck = new QCheckBox("自动检测文本方向并旋转矫正");
    doAngleCheck->setChecked(m_ocr && m_ocr->doAngle() != 0);
    commonLay->addRow("自动旋转:", doAngleCheck);

    QSpinBox* paddingSpin = new QSpinBox();
    paddingSpin->setRange(0, 200);
    paddingSpin->setValue(m_ocr ? m_ocr->padding() : 50);
    commonLay->addRow("Padding:", paddingSpin);

    QSpinBox* maxSideLenSpin = new QSpinBox();
    maxSideLenSpin->setRange(256, 4096);
    maxSideLenSpin->setSingleStep(128);
    maxSideLenSpin->setValue(m_ocr ? m_ocr->maxSideLen() : 1024);
    commonLay->addRow("最大边长:", maxSideLenSpin);

    QDoubleSpinBox* boxThreshSpin = new QDoubleSpinBox();
    boxThreshSpin->setRange(0.0, 1.0);
    boxThreshSpin->setSingleStep(0.05);
    boxThreshSpin->setDecimals(2);
    boxThreshSpin->setValue(m_ocr ? m_ocr->boxThresh() : 0.3);
    commonLay->addRow("文本框阈值:", boxThreshSpin);

    lay->addWidget(commonGroup);

    // ═══════════════ 普通OCR参数 ═══════════════
    QGroupBox* ocrGroup = new QGroupBox("普通OCR");
    QFormLayout* ocrLay = new QFormLayout(ocrGroup);

    QDoubleSpinBox* boxScoreThreshSpin = new QDoubleSpinBox();
    boxScoreThreshSpin->setRange(0.0, 1.0);
    boxScoreThreshSpin->setSingleStep(0.05);
    boxScoreThreshSpin->setDecimals(2);
    boxScoreThreshSpin->setValue(m_ocr ? m_ocr->boxScoreThresh() : 0.6);
    ocrLay->addRow("文本置信度:", boxScoreThreshSpin);

    QDoubleSpinBox* unClipRatioSpin = new QDoubleSpinBox();
    unClipRatioSpin->setRange(0.1, 5.0);
    unClipRatioSpin->setSingleStep(0.1);
    unClipRatioSpin->setDecimals(1);
    unClipRatioSpin->setValue(m_ocr ? m_ocr->unClipRatio() : 1.0);
    ocrLay->addRow("文本框扩展:", unClipRatioSpin);

    lay->addWidget(ocrGroup);

    // ═══════════════ 表格参数 ═══════════════
    QGroupBox* tableGroup = new QGroupBox("表格");
    QFormLayout* tableLay = new QFormLayout(tableGroup);

    QDoubleSpinBox* tableBoxScoreThreshSpin = new QDoubleSpinBox();
    tableBoxScoreThreshSpin->setRange(0.0, 1.0);
    tableBoxScoreThreshSpin->setSingleStep(0.05);
    tableBoxScoreThreshSpin->setDecimals(2);
    tableBoxScoreThreshSpin->setValue(m_ocr ? m_ocr->tableBoxScoreThresh() : 0.5);
    tableLay->addRow("文本置信度:", tableBoxScoreThreshSpin);

    QDoubleSpinBox* tableUnClipRatioSpin = new QDoubleSpinBox();
    tableUnClipRatioSpin->setRange(0.1, 5.0);
    tableUnClipRatioSpin->setSingleStep(0.1);
    tableUnClipRatioSpin->setDecimals(1);
    tableUnClipRatioSpin->setValue(m_ocr ? m_ocr->tableUnClipRatio() : 1.5);
    tableLay->addRow("文本框扩展:", tableUnClipRatioSpin);

    lay->addWidget(tableGroup);

    // 恢复默认值 + 说明 按钮行
    QHBoxLayout* btnRow = new QHBoxLayout();

    QPushButton* resetBtn = new QPushButton("恢复默认值");
    resetBtn->setStyleSheet("QPushButton { color: #E74C3C; border: 1px solid #E74C3C; border-radius: 4px; padding: 4px 12px; }"
                            "QPushButton:hover { background: #FDEDEC; }");
    connect(resetBtn, &QPushButton::clicked, this, [&, doAngleCheck, paddingSpin, maxSideLenSpin, boxThreshSpin, boxScoreThreshSpin, unClipRatioSpin, tableBoxScoreThreshSpin, tableUnClipRatioSpin]() {
        doAngleCheck->setChecked(false);
        paddingSpin->setValue(50);
        maxSideLenSpin->setValue(1024);
        boxThreshSpin->setValue(0.3);
        boxScoreThreshSpin->setValue(0.6);
        unClipRatioSpin->setValue(1.0);
        tableBoxScoreThreshSpin->setValue(0.5);
        tableUnClipRatioSpin->setValue(1.5);
    });
    btnRow->addWidget(resetBtn);

    QPushButton* helpBtn = new QPushButton("说明");
    helpBtn->setIcon(AppIcons::fromSvg(AppIcons::help(), 14));
    helpBtn->setStyleSheet("QPushButton { color: #5B9BD5; border: 1px solid #5B9BD5; border-radius: 4px; padding: 4px 12px; }"
                           "QPushButton:hover { background: #EBF5FB; }");
    connect(helpBtn, &QPushButton::clicked, this, [this]() {
        QDialog helpDlg(this);
        helpDlg.setWindowTitle("参数说明");
        helpDlg.setMinimumWidth(480);
        QVBoxLayout* hLay = new QVBoxLayout(&helpDlg);
        QTextEdit* text = new QTextEdit();
        text->setReadOnly(true);
        text->setHtml(
            "<h3>OCR 参数说明</h3>"
            "<h4>通用参数 (普通OCR和表格共用)</h4>"
            "<table style='font-size:12px; line-height:1.6;'>"
            "<tr><td style='font-weight:bold; white-space:nowrap; padding-right:12px;'>自动旋转</td>"
            "<td>是否使用文本行方向分类模型，自动检测文本是否倒置(180°)并旋转矫正。</td></tr>"
            "<tr><td style='font-weight:bold; white-space:nowrap; padding-right:12px;'>Padding</td>"
            "<td>图像边缘填充像素数，避免边缘文本被截断。<b>默认值: 50</b></td></tr>"
            "<tr><td style='font-weight:bold; white-space:nowrap; padding-right:12px;'>最大边长</td>"
            "<td>图像最长边超过此值时等比缩放。<b>默认值: 1024</b></td></tr>"
            "<tr><td style='font-weight:bold; white-space:nowrap; padding-right:12px;'>文本框阈值</td>"
            "<td>概率图像素阈值(text_det_thresh)。<b>默认值: 0.3</b></td></tr>"
            "</table>"
            "<h4>普通OCR参数</h4>"
            "<table style='font-size:12px; line-height:1.6;'>"
            "<tr><td style='font-weight:bold; white-space:nowrap; padding-right:12px;'>文本置信度</td>"
            "<td>检测框内像素平均得分阈值(text_det_box_thresh)。<b>默认值: 0.6</b></td></tr>"
            "<tr><td style='font-weight:bold; white-space:nowrap; padding-right:12px;'>文本框扩展</td>"
            "<td>扩张系数(text_det_unclip_ratio)。<b>默认值: 1.0</b></td></tr>"
            "</table>"
            "<h4>表格参数</h4>"
            "<table style='font-size:12px; line-height:1.6;'>"
            "<tr><td style='font-weight:bold; white-space:nowrap; padding-right:12px;'>文本置信度</td>"
            "<td>表格专用，比普通OCR更低以检测更多文本块。<b>默认值: 0.5</b></td></tr>"
            "<tr><td style='font-weight:bold; white-space:nowrap; padding-right:12px;'>文本框扩展</td>"
            "<td>表格专用，比普通OCR更大以提高IoU匹配率。<b>默认值: 1.5</b></td></tr>"
            "</table>"
        );
        hLay->addWidget(text);
        QDialogButtonBox* okBtn = new QDialogButtonBox(QDialogButtonBox::Ok);
        connect(okBtn, &QDialogButtonBox::accepted, &helpDlg, &QDialog::accept);
        hLay->addWidget(okBtn);
        helpDlg.exec();
    });
    btnRow->addWidget(helpBtn);

    btnRow->addStretch();
    lay->addLayout(btnRow);

    // 确定/取消
    QDialogButtonBox* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted && m_ocr) {
        m_doAngle = doAngleCheck->isChecked();
        m_ocr->setDoAngle(m_doAngle ? 1 : 0);
        m_ocr->setPadding(paddingSpin->value());
        m_ocr->setMaxSideLen(maxSideLenSpin->value());
        m_ocr->setBoxThresh((float)boxThreshSpin->value());
        m_ocr->setBoxScoreThresh((float)boxScoreThreshSpin->value());
        m_ocr->setUnClipRatio((float)unClipRatioSpin->value());
        m_ocr->setTableBoxScoreThresh((float)tableBoxScoreThreshSpin->value());
        m_ocr->setTableUnClipRatio((float)tableUnClipRatioSpin->value());
        saveSettings();
    }
}

void MainWindow::startHttpServer(quint16 port) {
    if (m_httpServer) return;
    // OCR 引擎共用 GUI 的 m_ocr 实例 (与 GUI 截图/文件识别同一个)
    m_httpServer = new OcrHttpServer(port, m_ocr, this);
    m_httpServer->setOcrActivityCallback([this]() { resetOcrIdleTimer(); });
    m_httpServer->setClipboardCallback([this](const QString& html) { copyHtmlToClipboard(html); });
    if (!m_httpServer->start()) {
        QMessageBox::warning(this, "网络服务",
            QString("端口 %1 启动失败, 可能被占用。").arg(port));
        delete m_httpServer;
        m_httpServer = nullptr;
        return;
    }
    m_netBtn->setChecked(true);
    m_ocrStatusLabel->setText(QString("HTTP 服务已开启 :%1").arg(port));
    m_ocrStatusLabel->setStyleSheet("color: #2e7d32; font-size: 12px;");
    QString urlStr = QString("http://localhost:%1").arg(port);
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("网络服务");
    msgBox.setText(QString("HTTP 服务已启动\n\n浏览器访问: %1\n端口: %2").arg(urlStr).arg(port));
    msgBox.setIcon(QMessageBox::Information);
    msgBox.addButton("确定", QMessageBox::AcceptRole);
    QPushButton *openBtn = msgBox.addButton("确定并打开网址", QMessageBox::AcceptRole);
    msgBox.exec();
    if (msgBox.clickedButton() == openBtn) {
        QDesktopServices::openUrl(QUrl(urlStr));
    }
}

void MainWindow::stopHttpServer() {
    if (!m_httpServer) return;
    delete m_httpServer;
    m_httpServer = nullptr;
    m_netBtn->setChecked(false);
    m_ocrStatusLabel->setText("OCR引擎就绪");
    m_ocrStatusLabel->setStyleSheet("color: #666; font-size: 12px;");
}

// 结果文本框自定义右键菜单 (中文)
void MainWindow::onResultTextContextMenu(const QPoint& pos) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { font-size: 13px; padding: 4px 8px; }"
        "QMenu::item { padding: 4px 20px; }"
        "QMenu::item:selected { background: #E6EDFC; }"
    );

    QAction* copyAct = menu.addAction("复制");
    QAction* copyAllAct = menu.addAction("复制全部");
    QAction* cutAct = menu.addAction("剪切");
    QAction* pasteAct = menu.addAction("粘贴");
    QAction* selectAllAct = menu.addAction("全选");
    QAction* clearAct = menu.addAction("清空");
    QAction* saveXlsxAct = menu.addAction("另存为 XLSX");
    menu.addSeparator();
    QAction* saveHtmlAct = menu.addAction("另存为 HTML");

    // 根据文本框状态启用/禁用菜单项
    QTextCursor cursor = m_resultText->textCursor();
    bool hasSelection = cursor.hasSelection();
    bool hasText = !m_resultText->document()->isEmpty();

    copyAct->setEnabled(hasSelection);
    cutAct->setEnabled(hasSelection);
    copyAllAct->setEnabled(hasText);
    selectAllAct->setEnabled(hasText);
    clearAct->setEnabled(hasText);
    saveXlsxAct->setEnabled(!m_lastHtml.isEmpty());
    saveHtmlAct->setEnabled(!m_lastHtml.isEmpty());

    // 执行菜单并获取用户选择
    QAction* action = menu.exec(m_resultText->mapToGlobal(pos));

    if (!action) return;

    if (action == copyAct) {
        m_resultText->copy();
    } else if (action == copyAllAct) {
        if (!m_lastHtml.isEmpty()) {
            copyHtmlToClipboard(m_lastHtml);
        } else {
            m_resultText->selectAll();
            m_resultText->copy();
            cursor.clearSelection();
            m_resultText->setTextCursor(cursor);
        }
    } else if (action == cutAct) {
        m_resultText->cut();
    } else if (action == pasteAct) {
        m_resultText->paste();
    } else if (action == selectAllAct) {
        m_resultText->selectAll();
    } else if (action == clearAct) {
        m_resultText->clear();
        m_lastHtml.clear();
    } else if (action == saveXlsxAct) {
        if (!m_lastHtml.isEmpty()) {
            QString path = QFileDialog::getSaveFileName(this, "另存为 XLSX",
                QDir::currentPath() + "/output.xlsx",
                "Excel 文件 (*.xlsx)");
            if (!path.isEmpty()) {
                if (htmlToXlsx(m_lastHtml.toStdString(), path.toStdString())) {
                    QMessageBox::information(this, "成功", "XLSX 已保存至:\n" + path);
                } else {
                    QMessageBox::warning(this, "警告", "XLSX 生成失败");
                }
            }
        }
    } else if (action == saveHtmlAct) {
        if (!m_lastHtml.isEmpty()) {
            QString path = QFileDialog::getSaveFileName(this, "另存为 HTML",
                QDir::currentPath() + "/output.html",
                "HTML 文件 (*.html)");
            if (!path.isEmpty()) {
                QFile f(path);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(m_lastHtml.toUtf8());
                    f.close();
                    QMessageBox::information(this, "成功", "HTML 已保存至:\n" + path);
                }
            }
        }
    }
}