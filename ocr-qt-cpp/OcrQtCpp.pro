# OcrQtCpp 项目配置文件 (MSVC 全静态版本)
# 目标: 单文件 exe, 不再携带 RapidOcrOnnx.dll
#
# 三模式: GUI (默认) / 命令行 (--cli) / HTTP 服务 (--server / --port)
#
# 构建: python build_msvc.py [--clean]
# 前提:
#   1. 本目录有 RapidOcrOnnxStatic.lib (OCR 静态库)
#   2. 上级 ../RapidOcrEmbed/ 有 onnxruntime-static/ 和 opencv-static/
#   3. 使用 MSVC 静态 Qt: C:\Qt\5.15.2\msvc2019-x64-static
#
# ========================================================================
# ⚠️ 已知问题: libjpeg-turbo 与 Qt JPEG 符号冲突
# ========================================================================
# OpenCV 的 libjpeg-turbo.lib 与 Qt 的 JPEG 支持在静态链接时会冲突:
#   - 两者都定义了 jpeg_* 符号
#   - 链接器 /FORCE:MULTIPLE 强制链接, 运行时拿到错误实现
#   - 后果: QPixmap 加载 JPEG 时闪退 (ACCESS_VIOLATION)
#
# 当前解决方案: gen_libs_pri.py 中排除 libjpeg-turbo.lib
#   - OCR 用 BMP 格式, 不需要 JPEG codec
#   - Qt 自带 JPEG 给 QPixmap 用
#
# 如果需要 OpenCV 的 JPEG 能力, 参见 rename_jpeg_symbols.py
# ========================================================================

QT -= core gui widgets svg
QT += core gui widgets svg printsupport
# 不用 Qt Network (用原生 Winsock + QSocketNotifier 代替, 避免 /FORCE:MULTIPLE 符号冲突)

# ========================================================================
# ⚠️ JPEG 符号冲突解决方案
# ========================================================================
# MuPDF 的 mupdf.lib 内置了 libjpeg-turbo (jpeg_* 符号)
# Qt 的 qjpeg.lib 也定义了 jpeg_* 符号
#
# 解决方案: 强制 qjpeg 插件在 mupdf.lib 之前链接
# 这样链接器优先使用 Qt 的 JPEG 实现，避免 JPG 加载闪退
# ========================================================================
QTPLUGIN += qjpeg  # 强制链接 qjpeg 插件

CONFIG += release
CONFIG += static
CONFIG += optimize_full
CONFIG += c++17

# MSVC: 全程序优化 + C++17
# 注意：不使用 /arch:AVX2，因为 ONNX Runtime / OpenCV / absl 非 AVX2 编译，
#      堆栈帧对齐不一致会导致 STATUS_STACK_BUFFER_OVERRUN (0xC0000409)
msvc {
    QMAKE_CXXFLAGS += /utf-8 /permissive- /std:c++17 /GL /Gy
    QMAKE_CFLAGS   += /utf-8 /GL /Gy
}

# 嵌入模型 + C ABI 模式 (静态库消费端 _QM_OCR_API 为空宏, 符号正常解析)
DEFINES += __CLIB__ __EMBEDDED_MODELS__ UNICODE _UNICODE QT_STATIC NDEBUG NOCRYPT NOUNCRYPT

# ---------------- 源文件 ----------------
SOURCES += \
    main.cpp \
    MainWindow.cpp \
    ScreenshotDialog.cpp \
    WidgetPicker.cpp \
    OCRWrapper.cpp \
    CliRunner.cpp \
    OcrJson.cpp \
    OcrHttpServer.cpp \
    TbpuLayout.cpp \
    TextClickEngine.cpp \
    TextClickCli.cpp \
    PdfHelper.cpp \
    PdfTextLayer.cpp \
    libs/xlsxwriter-src/src/app.c \
    libs/xlsxwriter-src/src/chart.c \
    libs/xlsxwriter-src/src/chartsheet.c \
    libs/xlsxwriter-src/src/comment.c \
    libs/xlsxwriter-src/src/content_types.c \
    libs/xlsxwriter-src/src/core.c \
    libs/xlsxwriter-src/src/custom.c \
    libs/xlsxwriter-src/src/drawing.c \
    libs/xlsxwriter-src/src/format.c \
    libs/xlsxwriter-src/src/hash_table.c \
    libs/xlsxwriter-src/src/metadata.c \
    libs/xlsxwriter-src/src/packager.c \
    libs/xlsxwriter-src/src/relationships.c \
    libs/xlsxwriter-src/src/rich_value.c \
    libs/xlsxwriter-src/src/rich_value_rel.c \
    libs/xlsxwriter-src/src/rich_value_structure.c \
    libs/xlsxwriter-src/src/rich_value_types.c \
    libs/xlsxwriter-src/src/shared_strings.c \
    libs/xlsxwriter-src/src/styles.c \
    libs/xlsxwriter-src/src/table.c \
    libs/xlsxwriter-src/src/theme.c \
    libs/xlsxwriter-src/src/utility.c \
    libs/xlsxwriter-src/src/vml.c \
    libs/xlsxwriter-src/src/workbook.c \
    libs/xlsxwriter-src/src/worksheet.c \
    libs/xlsxwriter-src/src/xmlwriter.c \
    libs/xlsxwriter-src/third_party/minizip/ioapi.c \
    libs/xlsxwriter-src/third_party/minizip/iowin32.c \
    libs/xlsxwriter-src/third_party/minizip/zip.c \
    libs/xlsxwriter-src/third_party/md5/md5.c \
    libs/xlsxwriter-src/third_party/tmpfileplus/tmpfileplus.c

HEADERS += \
    MainWindow.h \
    ScreenshotDialog.h \
    WidgetPicker.h \
    OCRWrapper.h \
    Icons.h \
    CliRunner.h \
    OcrJson.h \
    OcrHttpServer.h \
    TbpuLayout.h \
    webui.h \
    TextClickEngine.h \
    TextClickCli.h \
    PdfHelper.h \
    PdfTextLayer.h \
    HtmlToXlsx.h

# ---------------- 包含路径 ----------------
INCLUDEPATH += $$PWD/../RapidOcrEmbed/include
INCLUDEPATH += $$PWD/../RapidOcrEmbed/include/models
INCLUDEPATH += $$PWD/../RapidOcrEmbed/MNN_lib/include
INCLUDEPATH += $$PWD/../RapidOcrEmbed/onnxruntime-static/windows-x64/include
INCLUDEPATH += $$PWD/../RapidOcrEmbed/opencv-static/windows-x64/include
# MuPDF 及 thirdparty 头文件 (已复制到 libs/include-mupdf-1.27.2)
INCLUDEPATH += $$PWD/libs/include-mupdf-1.27.2
INCLUDEPATH += $$PWD/libs/include-mupdf-1.27.2/zlib
INCLUDEPATH += $$PWD/libs/include-xlsxwriter
INCLUDEPATH += $$PWD/libs/xlsxwriter-src/third_party/minizip
INCLUDEPATH += $$PWD/libs/xlsxwriter-src/third_party/tmpfileplus

# ---------------- MSVC 链接选项 ----------------
msvc {
    # 增加堆栈大小到 4MB，避免排版算法和 PDF 处理的堆栈溢出
    # 默认堆栈大小为 1MB，排版算法中的局部变量可能导致堆栈溢出
    QMAKE_LFLAGS += /STACK:4194304
    QMAKE_LFLAGS_RELEASE += /LTCG /OPT:REF /OPT:ICF /FORCE:MULTIPLE
}

# ---------------- 链接库 ----------------
# ⚠️ libs.pri 由 gen_libs_pri.py 自动生成, 已排除 libjpeg-turbo.lib (与 Qt JPEG 冲突)
include(libs.pri)

# Windows 系统库
LIBS += -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -luuid -ladvapi32
LIBS += -lws2_32 -lopengl32 -limm32 -lwinmm -ldwmapi
LIBS += -lbcrypt -lcrypt32 -lmfplat -lwmcodecdspuuid -lsecur32 -lncrypt

# MuPDF thirdparty dependencies
# ============================================================================
# WARNING: MuPDF thirdparty/libjpeg MUST be libjpeg-turbo (NOT IJG libjpeg).
# OpenCV static libs include libjpeg-turbo. If MuPDF uses IJG libjpeg (JPEG_LIB_VERSION=90),
# the linker picks ONE of the two via /FORCE:MULTIPLE, causing "struct mismatch" errors
# at runtime: "library thinks size is 600, caller expects 632".
# MuPDF thirdparty/libjpeg was replaced with libjpeg-turbo from SumatraPDF's ext/libjpeg-turbo,
# jconfig.h sets JPEG_LIB_VERSION=62 to match OpenCV's libjpeg-turbo.
# SHARE_JPEG=1 is defined in MuPDF CMakeLists.txt to skip jmemcust.c (custom memory manager)
# which conflicts with libjpeg-turbo's jmemsys.h.
# libjpeg.lib is excluded from this link to avoid duplicate symbols.
# ============================================================================
LIBS += $$PWD/libs/freetype.lib
LIBS += $$PWD/libs/harfbuzz.lib
LIBS += $$PWD/libs/openjpeg.lib
LIBS += $$PWD/libs/jbig2dec.lib
LIBS += $$PWD/libs/zlib.lib
LIBS += $$PWD/libs/lcms2.lib
LIBS += $$PWD/libs/mujs.lib
LIBS += $$PWD/libs/brotli.lib

# 目标名称
TARGET = OcrQtCpp

# Windows 资源文件 (exe 图标)
RC_FILE = app.rc
