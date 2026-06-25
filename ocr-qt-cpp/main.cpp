#include "MainWindow.h"
#include "CliRunner.h"
#include "OcrHttpServer.h"
#include "TextClickCli.h"
#include "TextClickEngine.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <tlhelp32.h>  // 进程枚举

// GUI 子系统 exe 默认没有控制台。此函数为 CLI 模式 (--cli/--shot/--server/-h) 分配控制台，
// 并将 stdout/stderr 重定向到新控制台，确保输出可见。
static bool isStdoutPipe() {
    // 检测 stdout 是否是管道（从 subprocess 启动）
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdout == INVALID_HANDLE_VALUE || hStdout == NULL) return false;
    DWORD type = GetFileType(hStdout);
    // FILE_TYPE_PIPE: 0x0003 (管道或字符设备)
    // 从 subprocess.run 启动时，stdout 是管道
    return (type == FILE_TYPE_PIPE);
}

static bool attachConsole() {
    // 如果 stdout 已经是管道（从 subprocess 启动），不需要附加控制台
    if (isStdoutPipe()) {
        return true;  // 直接使用管道输出
    }
    
    // 先尝试附加到父进程的控制台（如果是从 cmd.exe 启动的）
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        // 成功附加父控制台，重定向 stdout/stderr
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        SetConsoleOutputCP(CP_UTF8);
        return true;
    }
    // 父进程没有控制台（如双击运行、资源管理器等），分配一个新控制台
    if (AllocConsole()) {
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);
        SetConsoleOutputCP(CP_UTF8);
        return true;
    }
    return false;
}

// 终止所有同名进程（排除当前进程）
static void terminateExistingProcesses() {
    // 获取当前进程ID
    DWORD currentPid = GetCurrentProcessId();

    // 获取当前进程的可执行文件名
    wchar_t currentExeName[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, currentExeName, MAX_PATH);
    std::wstring exeName(currentExeName);
    // 只取文件名部分（不含路径）
    size_t pos = exeName.find_last_of(L"\\");
    if (pos != std::wstring::npos) {
        exeName = exeName.substr(pos + 1);
    }

    // 创建进程快照
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return;
    }

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    // 遍历所有进程
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            // 检查进程名是否相同
            std::wstring processName(pe32.szExeFile);
            if (processName == exeName && pe32.th32ProcessID != currentPid) {
                // 找到同名进程（排除当前进程），终止它
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                if (hProcess != nullptr) {
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                }
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);
}
#endif

// 详细中文帮助信息
static void printHelp() {
    std::cout <<
        "OcrQtCpp - PP-OCRv6 文字识别工具 (GUI / 命令行 / HTTP 服务)\n"
        "基于 RapidOcrEmbed, 支持截图识别、图片文件识别、HTTP 接口调用。\n"
        "\n"
        "用法:\n"
        "  OcrQtCpp.exe [选项] [图片路径或文件夹...]\n"
        "\n"
        "运行模式 (互斥, 默认 GUI):\n"
        "  (无参数)              GUI 图形界面 (默认)\n"
        "  --cli <路径...>       命令行模式: 识别图片, 输出 JSON 后退出\n"
        "  --shot                截图模式: 弹出截图框框选屏幕区域, 识别后输出 JSON 退出\n"
        "  --shot --region <x,y,w,h> 截图模式: 直接截取指定区域, 无需手动框选\n"
        "                          例如: --shot --region 0,0,500,300 截取左上角 500x300 区域\n"
        "  --server              HTTP 服务模式: 启动 HTTP 服务 (无界面, headless)\n"
        "\n"
        "通用选项:\n"
        "  -h, --help            显示本帮助信息\n"
        "  --coords              坐标模式: 输出带坐标的文本块 (命令行/HTTP 通用)\n"
        "                          不加此项默认只返回纯文本 (体积小)\n"
        "  --layout              排版模式: TBPU 排版算法, 按人类阅读顺序排序文本块,\n"
        "                          智能合并段落, 预测分隔符。需配合 --coords 或单独使用。\n"
        "  --layout-strategy <策略> 排版策略 (默认 multi_para):\n"
        "                          multi_para  - 多栏段落\n"
        "                          multi_line  - 多栏换行\n"
        "                          multi_none  - 多栏连续\n"
        "                          single_para - 单栏段落\n"
        "                          single_line - 单栏换行\n"
        "                          single_none - 单栏连续\n"
        "                          single_code - 单栏缩进\n"
        "  --pdf                  生成可搜索 PDF: 识别后生成带文字层的 PDF 文件,\n"
        "                          可在阅读器中搜索/复制文字。输出文件同名 .pdf。\n"
        "  --table[=模式]         表格识别模式:\n"
        "                          不加值或 --table=1  SLANet 模型 (需 ONNX)\n"
        "                          --table=2          img2table 纯 OpenCV (无模型, 仅识别有线框表格)\n"
        "                          输出 JSON (含 html 字段), 同时自动写入 _table.html 和 _table.xlsx 文件。\n"
        "  --clipboard, -cb      将识别结果复制到系统剪贴板:\n"
        "                          普通模式: 复制纯文本\n"
        "                          表格模式: 复制 HTML 表格 (可直接粘贴到 Excel)\n"
        "  --rotate <0或1>       旋转矫正 (默认从配置文件读取):\n"
        "                          1 开启文本方向检测和矫正\n"
        "                          0 关闭\n"
        "                          不指定时使用 ocr_config.json 中的全局设置\n"
        "\n"
        "全局配置文件:\n"
        "  OCR 参数存储在用户目录的 ocr_config.json 中:\n"
        "    %LOCALAPPDATA%\\OcrQtCpp\\ocr_config.json\n"
        "  GUI 高级设置、HTTP 网页设置、CLI 参数均读写此文件。\n"
        "  任意入口修改后, 其他入口下次启动自动生效。\n"
        "\n"
        "命令行多文件/文件夹:\n"
        "  支持同时传入多个图片路径和/或文件夹, 文件夹递归扫描图片文件。\n"
        "  单文件时输出格式不变 (向后兼容)。\n"
        "  多文件时每行输出一个 JSON (NDJSON), 含 \"file\" 字段标识来源。\n"
        "\n"
        "  OcrQtCpp.exe --cli 图片1.png 图片2.jpg           # 多文件\n"
        "  OcrQtCpp.exe --cli ./images/                      # 文件夹 (递归)\n"
        "  OcrQtCpp.exe --cli 图片1.png ./folder/ 图片2.jpg   # 混合\n"
        "  OcrQtCpp.exe 图片1.png 图片2.jpg                  # 简写 (多文件)\n"
        "\n"
        "HTTP 服务选项:\n"
        "  --port <端口号>       HTTP 服务端口 (默认 8080), 隐含 --server\n"
        "\n"
        "=== GUI 模式 ===\n"
        "  OcrQtCpp.exe\n"
        "  打开图形界面, 支持截图/文件识别, 右下角地球图标可开启 HTTP 服务。\n"
        "\n"
        "=== 命令行模式 ===\n"
        "  OcrQtCpp.exe --cli 图片.png              # 识别, 输出 JSON (仅文本)\n"
        "  OcrQtCpp.exe 图片.png                    # 简写: 直接给路径\n"
        "  OcrQtCpp.exe --cli --coords 图片.png     # 坐标模式, 返回每个文本块的位置\n"
        "  OcrQtCpp.exe --cli --layout 图片.png     # 排版模式, 智能排序+合并段落\n"
        "  OcrQtCpp.exe --cli --layout --layout-strategy single_para 图片.png\n"
        "  OcrQtCpp.exe --cli --pdf 图片.png        # 生成可搜索 PDF (同名 .pdf)\n"
        "  OcrQtCpp.exe --cli --pdf 文档.pdf        # PDF 文件生成可搜索 PDF (_ocr.pdf)\n"
        "  OcrQtCpp.exe --cli --table 图片.png      # SLANet 表格识别 (输出 HTML)\n"
        "  OcrQtCpp.exe --cli --table=2 图片.png    # img2table 纯 OpenCV 表格识别\n"
        "  OcrQtCpp.exe --cli --clipboard 图片.png   # 识别后复制文本到剪贴板\n"
        "  OcrQtCpp.exe --cli --table --clipboard 图片.png  # 表格识别, 复制 HTML 到剪贴板\n"
        "  OcrQtCpp.exe --shot                      # 截图识别 (仅文本)\n"
        "  OcrQtCpp.exe --shot --coords             # 截图识别 (带坐标)\n"
        "  OcrQtCpp.exe --shot --layout             # 截图识别 (排版模式)\n"
        "  说明: --shot 会弹出全屏截图框, 鼠标拖拽框选区域后自动识别。\n"
        "\n"
        "=== HTTP 服务模式 ===\n"
        "  OcrQtCpp.exe --server                    # 启动服务 (默认 8080, 无界面)\n"
        "  OcrQtCpp.exe --port 9000                 # 指定端口\n"
        "  说明: 命令行启动为后台服务, 不弹图形界面 (headless), 直接退出 Ctrl+C 结束。\n"
        "        如需图形界面 + HTTP 服务, 请运行 GUI 模式后点击右下角地球图标开启。\n"
        "  访问控制: 网页界面和图片 OCR 接口可从外网访问;\n"
        "            /screenshot (截图) 和 /textclick (鼠标操控) 仅限本机 127.0.0.1。\n"
        "  启动后浏览器访问 http://localhost:端口 即可上传图片识别。\n"
        "\n"
        "HTTP 接口:\n"
        "  GET  /\n"
        "    浏览器图形界面 (拖拽/点击上传, 支持多文件, 显示识别文本与坐标)\n"
        "\n"
        "  POST /ocr          (multipart/form-data 表单上传)\n"
        "    字段名: file (支持多文件, 字段名均为 file)\n"
        "    curl -F \"file=@图片.png\" http://localhost:8080/ocr\n"
        "    多文件: curl -F \"file=@a.png\" -F \"file=@b.jpg\" http://localhost:8080/ocr\n"
        "\n"
        "  POST /ocr-batch     (multipart 多文件批量, 返回 NDJSON)\n"
        "    与 /ocr 相同, 但始终返回多行 JSON (每行一个文件结果)\n"
        "    curl -F \"file=@a.png\" -F \"file=@b.jpg\" http://localhost:8080/ocr-batch\n"
        "\n"
        "  POST /ocr-raw      (body 直接是图片二进制字节)\n"
        "    curl --data-binary @图片.png http://localhost:8080/ocr-raw\n"
        "\n"
        "  POST /screenshot   (触发服务器端截图框选, 用户框选后返回识别结果)\n"
        "    curl -X POST http://localhost:8080/screenshot\n"
        "\n"
        "  以上 POST 接口均可加 ?mode=coords 返回带坐标的文本块:\n"
        "    curl -F \"file=@图片.png\" \"http://localhost:8080/ocr?mode=coords\"\n"
        "\n"
        "  排版模式 (HTTP):\n"
        "    加 ?layout=1 启用排版, ?layoutStrategy=策略名 指定策略:\n"
        "    curl -F \"file=@文档.png\" \"http://localhost:8080/ocr?layout=1&layoutStrategy=multi_para\"\n"
        "\n"
        "  可搜索 PDF (HTTP):\n"
        "    加 ?pdf=1 在 JSON 里返回 base64 编码的可搜索 PDF:\n"
        "    curl -F \"file=@图片.png\" \"http://localhost:8080/ocr?pdf=1\"\n"
        "    或使用 POST /ocr-pdf 直接返回 PDF 二进制:\n"
        "    curl -F \"file=@图片.png\" http://localhost:8080/ocr-pdf --output result.pdf\n"
        "\n"
        "  表格识别 (HTTP):\n"
        "    加 ?table=1 (SLANet) 或 ?table=2 (img2table) 返回 HTML 表格:\n"
        "    curl -F \"file=@表格.png\" \"http://localhost:8080/ocr?table=1\"\n"
        "    curl -F \"file=@表格.png\" \"http://localhost:8080/ocr?table=2\"\n"
        "    返回 JSON 含 html 和 xlsx (base64 编码) 字段。\n"
        "\n"
        "返回 JSON 格式:\n"
        "  普通模式 (默认):\n"
        "    {\"code\":0,\"text\":\"识别出的全部文本\",\"blockCount\":3}\n"
        "  多文件模式:\n"
        "    {\"file\":\"a.jpg\",\"code\":0,\"text\":\"...\",\"blockCount\":3}\n"
        "    {\"file\":\"b.png\",\"code\":0,\"text\":\"...\",\"blockCount\":2}\n"
        "  坐标模式 (--coords 或 ?mode=coords):\n"
        "    {\"code\":0,\"text\":\"全部文本\",\"blockCount\":3,\"blocks\":[\n"
        "      {\"text\":\"第一块\",\"score\":0.88,\n"
        "       \"box\":[x1,y1,x2,y2,x3,y3,x4,y4],\"angle\":0,\"angleScore\":0.99,\n"
        "       \"end\":\"\\n\"}\n"
        "    ]}\n"
        "    box 为文本框四角坐标 (顺时针: 左上、右上、右下、左下)\n"
        "    score 为置信度 (0~1), angle 为方向角度, angleScore 为角度置信度\n"
        "    end 为结尾分隔符 (排版算法写入: '''/' '/'\\n', 默认 '\\n')\n"
        "\n"
        "  错误:\n"
        "    {\"code\":1,\"msg\":\"错误说明\"}\n"
        "    {\"file\":\"bad.jpg\",\"code\":1,\"msg\":\"cannot decode image\"}\n"
        "\n"
        "支持图片格式: PNG / JPG / BMP / WEBP / GIF / TIFF\n"
        "\n"
        "全局配置接口 (HTTP):\n"
        "  GET  /api/config    获取当前 OCR 参数 (JSON)\n"
        "    curl http://localhost:8080/api/config\n"
        "    返回: {\"doAngle\":false,\"padding\":50,\"maxSideLen\":1024,\n"
        "           \"boxScoreThresh\":0.6,\"boxThresh\":0.3,\"unClipRatio\":1.0}\n"
        "\n"
        "  POST /api/config    修改 OCR 参数 (JSON body), 全局生效\n"
        "    curl -X POST -H \"Content-Type: application/json\" \\\n"
        "         -d '{\"doAngle\":true,\"padding\":50}' \\\n"
        "         http://localhost:8080/api/config\n"
        "    修改后立即写入 ocr_config.json, GUI/CLI/HTTP 均生效。\n"
        "\n"
        "=== TextClick 模式 (文字点击) ===\n"
        "  用法: OcrQtCpp.exe --tc [选项] 或 OcrQtCpp.exe -click \"文字\" (隐式触发)\n"
        "\n"
        "  功能选项 (中英文等价):\n"
        "    -get/-获取              获取区域文本, 复制到剪贴板\n"
        "    -click <文字>/-单击     单击文字位置\n"
        "    -double <文字>/-双击    双击文字位置\n"
        "    -right <文字>/-右击     右击文字位置\n"
        "    -move <文字>/-移动      移动鼠标到文字位置 (不点击)\n"
        "    -check <文字>/-检查     检查是否包含文字 (是/否)\n"
        "    -pos <文字>/-坐标       获取文字坐标, 复制到剪贴板\n"
        "    -posall/-全部坐标       获取所有文字坐标\n"
        "    -list/-列表             列出所有文字块 (JSON 数组)\n"
        "\n"
        "  附加参数:\n"
        "    -n <数字>/-第几个       第几个匹配 (默认 1)\n"
        "    -r <x,y,w,h>/-区域      屏幕区域 (默认全屏)\n"
        "    -loc <位置>/-位置       center/topleft/topright/bottomleft/bottomright\n"
        "                            中心/左上/右上/左下/右下 (默认 center)\n"
        "    -output <路径>/-输出    保存带框标注的图片\n"
        "    -image <路径>           识别指定图片 (不截屏)\n"
        "\n"
        "  示例:\n"
        "    OcrQtCpp.exe --tc -get\n"
        "    OcrQtCpp.exe --tc -click \"确定\"\n"
        "    OcrQtCpp.exe --tc -pos \"菜单\" -loc topleft\n"
        "    OcrQtCpp.exe --tc -check \"登录\" -r 0,0,500,500\n"
        "    OcrQtCpp.exe --tc -list -image C:\\test.png\n"
        "    OcrQtCpp.exe -click \"确定\"    (隐式触发, 等价 --tc)\n"
        "\n"
        "  HTTP 接口: POST /textclick\n"
        "    body: {\"action\":\"get|list|check|pos|posall|click|double|right|move\",\n"
        "           \"text\":\"...\", \"occurrence\":1, \"location\":\"center\",\n"
        "           \"region\":\"x,y,w,h\", \"image\":\"C:\\test.png\"}\n"
        "    返回: {\"success\":true/false,\"data\":\"...\",\"message\":\"...\"}\n"
        "\n"
        "  HTTP 参数说明:\n"
        "    action: 操作类型 (get/list/check/pos/posall/click/double/right/move)\n"
        "    text: 要查找的文字 (check/pos/click/double/right/move 需要)\n"
        "    occurrence: 第几个匹配, 对应 CLI 的 -n 参数 (默认 1)\n"
        "    location: 点击位置, 对应 CLI 的 -loc 参数\n"
        "              center/topleft/topright/bottomleft/bottomright (默认 center)\n"
        "    region: 屏幕区域 \"x,y,w,h\", 对应 CLI 的 -r 参数 (默认全屏)\n"
        "    image: 识别指定图片路径, 对应 CLI 的 -image 参数 (不截屏)\n"
        "\n"
        "  HTTP 示例:\n"
        "    curl -X POST -H \"Content-Type: application/json\" \\\n"
        "         -d '{\"action\":\"list\",\"image\":\"C:\\test.png\"}' \\\n"
        "         http://localhost:8080/textclick\n"
        "    curl -X POST -H \"Content-Type: application/json\" \\\n"
        "         -d '{\"action\":\"click\",\"text\":\"确定\",\"occurrence\":2}' \\\n"
        "         http://localhost:8080/textclick\n"
        "    curl -X POST -H \"Content-Type: application/json\" \\\n"
        "         -d '{\"action\":\"pos\",\"text\":\"菜单\",\"location\":\"topleft\"}' \\\n"
        "         http://localhost:8080/textclick\n"
        "    curl -X POST -H \"Content-Type: application/json\" \\\n"
        "         -d '{\"action\":\"check\",\"text\":\"登录\",\"region\":\"0,0,500,500\"}' \\\n"
        "         http://localhost:8080/textclick\n"
        ;
    std::cout.flush();
}

// 检查路径是否看起来像图片文件或文件夹
static bool looksLikeImageOrDir(const QString& arg) {
    if (arg.startsWith("-")) return false;
    QFileInfo fi(arg);
    if (fi.isDir()) return true;
    if (fi.isFile()) {
        QString lower = arg.toLower();
        return lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg")
            || lower.endsWith(".bmp") || lower.endsWith(".webp") || lower.endsWith(".gif")
            || lower.endsWith(".tiff") || lower.endsWith(".tif");
    }
    return false;
}

int main(int argc, char* argv[]) {
    // 先检测 -h / --help (在任何 QApplication 之前, 避免无谓初始化)
    // GUI 子系统 exe 默认没有控制台, 需要先分配控制台才能显示帮助信息
    for (int i = 1; i < argc; i++) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "-h" || arg == "--help" || arg == "/?") {
#ifdef _WIN32
            attachConsole();
#endif
            printHelp();
            return 0;
        }
    }

    // ---------- 检测是否为 GUI 模式 ----------
    bool isGuiMode = true;
    for (int i = 1; i < argc; i++) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--cli" || arg == "--shot" || arg == "--server" || arg == "--port") {
            isGuiMode = false;
            break;
        }
        // 检测是否为 TextClick 模式
        if (arg.startsWith("-click") || arg == "-单击" || arg.startsWith("-pos") || arg == "-坐标"
            || arg == "-get" || arg == "-获取" || arg == "-double" || arg == "-双击"
            || arg == "-right" || arg == "-右击" || arg == "-move" || arg == "-移动"
            || arg == "-check" || arg == "-检查" || arg == "-list" || arg == "-列表"
            || arg == "-posall" || arg == "-全部坐标" || arg == "--tc" || arg == "-tc") {
            isGuiMode = false;
            break;
        }
        // 检测是否为直接传入图片路径
        if (looksLikeImageOrDir(arg)) {
            isGuiMode = false;
            break;
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("OcrQtCpp");
    app.setStyle("Fusion");
    QFont appFont("Microsoft YaHei", 10);
    app.setFont(appFont);

    // ---------- GUI 模式多开检测 ----------
    if (isGuiMode) {
        terminateExistingProcesses();
    }

    // ---------- TextClick 模式检测 (在 QCommandLineParser 之前) ----------
    // 检测 --tc 或任一 TextClick 动作参数 (-click/-单击/-pos/-坐标/...)
    std::vector<std::string> tcArgs;
    for (int i = 1; i < argc; i++) {
        tcArgs.push_back(QString::fromLocal8Bit(argv[i]).toStdString());
    }
    if (isTextClickTrigger(tcArgs)) {
#ifdef _WIN32
        attachConsole();  // GUI 子系统需要分配控制台才能输出
#endif
        // 初始化 OCR 引擎
        OCRWrapper tcOcr;
        if (!tcOcr.initEmbedded(4)) {
            std::cerr << "[textclick] OCR 引擎初始化失败" << std::endl;
            return 4;
        }
        tcOcr.loadConfig();
        // 剔除 --tc 前缀本身 (如果存在)
        std::vector<std::string> cleanArgs;
        bool skipFirst = false;
        for (const auto& a : tcArgs) {
            if (a == "--tc" || a == "-tc") {
                skipFirst = true;
                continue;
            }
            cleanArgs.push_back(a);
        }
            int ret = runTextClickCli(&tcOcr, cleanArgs);
            return ret;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("OCR 识别工具 (GUI / 命令行 / HTTP 服务)");
    parser.addOption({{"c", "cli"}, "命令行模式"});
    parser.addOption({{"s", "shot"}, "截图模式"});
    parser.addOption({"region", "截图区域 (x,y,w,h), 配合 --shot 使用, 无需手动框选", "rect", ""});
    parser.addOption({{"k", "coords"}, "坐标模式"});
    parser.addOption({{"l", "layout"}, "排版模式"});
    parser.addOption({"layout-strategy", "排版策略", "strategy", "multi_para"});
    parser.addOption({{"d", "pdf"}, "生成可搜索 PDF (命令行模式)"});
    parser.addOption({{"t", "table"}, "表格模式 (1=SLANet 2=img2table, 不指定值=1)", "mode", ""});
    parser.addOption({{"b", "clipboard"}, "将识别结果复制到系统剪贴板"});
    parser.addOption({"pages", "PDF 页码选择, 格式: 1,2-5,all (默认 all)", "pages", ""});
    parser.addOption({{"S", "server"}, "HTTP 服务模式"});
    parser.addOption({{"p", "port"}, "端口", "port", "8080"});
    parser.addOption({"rotate", "旋转矫正: 0=关闭 1=开启 (默认 1)", "mode", "1"});
    parser.addPositionalArgument("images", "图片路径或文件夹 (支持多个)", "[路径...]");
    parser.parse(QCoreApplication::arguments());

    const QStringList positional = parser.positionalArguments();
    const bool coords = parser.isSet("coords");
    const bool layout = parser.isSet("layout");
    const std::string layoutStrategy = parser.value("layout-strategy").toStdString();
    const bool pdf = parser.isSet("pdf");
    const bool clipboard = parser.isSet("clipboard");
    int tableMode = 0;
    if (parser.isSet("table")) {
        QString tableVal = parser.value("table");
        tableMode = tableVal.isEmpty() ? 1 : tableVal.toInt();
        if (tableMode < 0 || tableMode > 2) tableMode = 1;
    }
    const std::string pages = parser.value("pages").toStdString();
    const std::string shotRegion = parser.value("region").toStdString();
    const bool doAngle = parser.value("rotate") != "0";

    // ---------- 模式 1: 截图命令行模式 (--shot) ----------
    if (parser.isSet("shot")) {
        int ret = runCliShot(coords, layout, layoutStrategy, shotRegion, clipboard, doAngle);
        return ret;
    }

    // ---------- 模式 2: 命令行文件模式 (--cli 或直接给图片/文件夹路径) ----------
    bool cliMode = parser.isSet("cli");
    // 简写: 没有 --cli 但有位置参数且看起来像图片/文件夹
    if (!cliMode && !positional.isEmpty()) {
        bool anyImage = false;
        for (const QString& arg : positional) {
            if (looksLikeImageOrDir(arg)) { anyImage = true; break; }
        }
        if (anyImage) cliMode = true;
    }
    if (cliMode) {
#ifdef _WIN32
        attachConsole();  // GUI 子系统需要分配控制台才能输出
#endif
        if (positional.isEmpty()) {
            std::cerr << "[error] 命令行模式需要图片路径或文件夹: OcrQtCpp --cli <image> [image2] [folder] ..." << std::endl;
            std::cerr << "        运行 OcrQtCpp --help 查看完整用法" << std::endl;
            return 1;
        }
        // 收集有效路径
        std::vector<std::string> paths;
        for (const QString& arg : positional) {
            paths.push_back(arg.toStdString());
        }
        if (paths.size() == 1) {
            int ret = runCliFile(paths[0], coords, layout, layoutStrategy, pdf, tableMode, pages, clipboard, doAngle);
            return ret;
        } else {
            int ret = runCliFiles(paths, coords, layout, layoutStrategy, pdf, tableMode, clipboard, doAngle);
            return ret;
        }
    }

    // ---------- 模式 3: HTTP 服务模式 (--server / --port) ----------
    bool serverMode = parser.isSet("server") || parser.isSet("port");
    OcrHttpServer* httpServer = nullptr;
    OCRWrapper* serverOcr = nullptr;
    if (serverMode) {
        serverOcr = new OCRWrapper();
        if (!serverOcr->initEmbedded(4)) {
            std::cerr << "[error] OCR 引擎初始化失败" << std::endl;
            delete serverOcr;
            return 4;
        }
        std::cerr << "[http] OCR 引擎已初始化" << std::endl;
        serverOcr->loadConfig();
        if (!doAngle) serverOcr->setDoAngle(0); // --rotate=0 覆盖配置

        quint16 port = static_cast<quint16>(parser.value("port").toUShort());
        if (port == 0) port = 8080;
        httpServer = new OcrHttpServer(port, serverOcr, &app);
        if (!httpServer->start()) {
            std::cerr << "[error] HTTP 服务启动失败, 端口 " << port << " 可能被占用" << std::endl;
            delete httpServer;
            delete serverOcr;
            return 3;
        }
        std::cout << "[http] 服务已启动: http://localhost:" << httpServer->port() << std::endl;
        std::cout << "[http] 浏览器访问上述地址即可上传图片识别" << std::endl;
        std::cout << "[http] 接口: GET / | POST /ocr | POST /ocr-batch | POST /ocr-raw | POST /screenshot" << std::endl;
        std::cout << "[http] 参数: 加 ?mode=coords 返回带坐标 (运行 --help 查看详情)" << std::endl;
        std::cout.flush();
    }

    if (serverMode) {
        int ret = app.exec();
        delete serverOcr;
        return ret;
    }

    // ---------- GUI 模式 ----------
    MainWindow window(nullptr, false);
    window.show();

    int ret = app.exec();
    return ret;
}
