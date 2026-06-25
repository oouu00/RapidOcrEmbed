// TextClick 命令行模式: 移植自 TextClick/src/main.cpp 的命令行分支,
// 改为复用 OCRWrapper + TextClickEngine, 兼容原有中英文参数。
#include "TextClickCli.h"
#include "TextClickEngine.h"

#include <iostream>
#include <sstream>
#include <algorithm>

// ============================================================
// 触发参数检测 (供 main.cpp 分流用)
// ============================================================
// TextClick 动作参数集合 (中英文)
static const char* const kActionParams[] = {
    "-get", "-\xE8\x8E\xB7\xE5\x8F\x96",                    // -get / -获取
    "-click", "-\xE5\x8D\x95\xE5\x87\xBB",                  // -click / -单击
    "-double", "-\xE5\x8F\x8C\xE5\x87\xBB",                 // -double / -双击
    "-right", "-\xE5\x8F\xB3\xE5\x87\xBB",                  // -right / -右击
    "-move", "-\xE7\xA7\xBB\xE5\x8A\xA8",                   // -move / -移动
    "-check", "-\xE6\xA3\x80\xE6\x9F\xA5",                  // -check / -检查
    "-pos", "-\xE5\x9D\x90\xE6\xA0\x87",                    // -pos / -坐标
    "-posall", "-\xE5\x85\xA8\xE9\x83\xA8\xE5\x9D\x90\xE6\xA0\x87", // -posall / -全部坐标
    "-list", "-\xE5\x88\x97\xE8\xA1\xA8",                   // -list / -列表
};

bool isTextClickTrigger(const std::vector<std::string>& args) {
    for (const auto& a : args) {
        if (a == "--tc" || a == "-tc") return true;
        for (const char* p : kActionParams) {
            if (a == p) return true;
        }
    }
    return false;
}

// ============================================================
// region 解析: "x,y,w,h"
// ============================================================
static bool parseRegion(const std::string& str, int& x, int& y, int& w, int& h) {
    size_t p1 = str.find(',');
    if (p1 == std::string::npos) return false;
    size_t p2 = str.find(',', p1 + 1);
    if (p2 == std::string::npos) return false;
    size_t p3 = str.find(',', p2 + 1);
    if (p3 == std::string::npos) return false;
    try {
        x = std::stoi(str.substr(0, p1));
        y = std::stoi(str.substr(p1 + 1, p2 - p1 - 1));
        w = std::stoi(str.substr(p2 + 1, p3 - p2 - 1));
        h = std::stoi(str.substr(p3 + 1));
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================
// 输出 TextClick 风格 JSON (保持与手册一致)
// {"success": true/false, "data": "...", "message": "..."}
// ============================================================
// 把字符串转义为 JSON 字符串内容
static std::string escJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

static void emitResult(bool success, const std::string& dataStr, const std::string& message) {
    std::cout << "{\"success\":" << (success ? "true" : "false")
              << ",\"data\":\"" << escJson(dataStr) << "\""
              << ",\"message\":\"" << escJson(message) << "\"}" << std::endl;
}

// list action: data 是 JSON 数组, 不能套引号, 单独输出
static void emitListResult(TextClickEngine& eng) {
    std::ostringstream oss;
    oss << "[";
    int n = eng.blockCount();
    for (int i = 0; i < n; i++) {
        if (i > 0) oss << ",";
        int box[8];
        eng.getBlockBox(i, box);
        oss << "{\"index\":" << i
            << ",\"text\":\"" << escJson(eng.getBlockText(i)) << "\""
            << ",\"box\":[" << box[0] << "," << box[1] << "," << box[2] << "," << box[3]
            << "," << box[4] << "," << box[5] << "," << box[6] << "," << box[7] << "]"
            << ",\"score\":" << eng.getBlockScore(i) << "}";
    }
    oss << "]";
    std::cout << "{\"success\":true,\"data\":" << oss.str()
              << ",\"message\":\"识别完成\"}" << std::endl;
}

// ============================================================
// 帮助信息
// ============================================================
static void printTcHelp(const char* exeName) {
    std::cout <<
        "========================================\n"
        "        TextClick 模式 (集成于 OcrQtCpp)\n"
        "========================================\n"
        "\n"
        "用法:\n"
        "  显式前缀: " << exeName << " --tc [选项] [参数]\n"
        "  隐式触发: " << exeName << " -click \"确定\"   (含 TextClick 动作参数即进入)\n"
        "\n"
        "功能选项 (中英文等价):\n"
        "  -get/-获取              获取区域文本, 复制到剪贴板\n"
        "  -click <文字>/-单击     单击文字位置\n"
        "  -double <文字>/-双击    双击文字位置\n"
        "  -right <文字>/-右击     右击文字位置\n"
        "  -move <文字>/-移动      移动鼠标到文字位置 (不点击)\n"
        "  -check <文字>/-检查     检查是否包含文字 (是/否)\n"
        "  -pos <文字>/-坐标       获取文字坐标, 复制到剪贴板\n"
        "  -posall/-全部坐标       获取所有文字坐标\n"
        "  -list/-列表             列出所有文字块 (JSON 数组)\n"
        "\n"
        "附加参数:\n"
        "  -n <数字>/-第几个       第几个匹配 (默认 1)\n"
        "  -r <x,y,w,h>/-区域      屏幕区域 (默认全屏)\n"
        "  -loc <位置>/-位置       center/topleft/topright/bottomleft/bottomright\n"
        "                          中心/左上/右上/左下/右下 (默认 center)\n"
        "  -output <路径>/-输出    保存带框标注的图片\n"
        "  -image <路径>           识别指定图片 (不截屏)\n"
        "  -h/-help/-帮助          显示本帮助\n"
        "\n"
        "输出: TextClick 风格 JSON {\"success\":..,\"data\":..,\"message\":..}\n"
        "\n"
        "示例:\n"
        "  OcrQtCpp --tc -get\n"
        "  OcrQtCpp --tc -click \"确定\"\n"
        "  OcrQtCpp --tc -pos \"菜单\" -loc topleft\n"
        "  OcrQtCpp --tc -check \"登录\" -r 0,0,500,500\n"
        "  OcrQtCpp --tc -list -image C:\\\\test.png\n"
        "  OcrQtCpp -click \"确定\"    (隐式触发, 等价 --tc)\n";
    std::cout.flush();
}

// ============================================================
// 主入口
// ============================================================
int runTextClickCli(OCRWrapper* ocr, const std::vector<std::string>& args) {
    if (!ocr || !ocr->isInitialized()) {
        std::cerr << "[textclick] OCR 引擎未初始化" << std::endl;
        return 1;
    }

    // 解析参数
    std::string action;
    std::string text;
    int occurrence = 1;
    std::string regionStr;
    TextClickEngine::Location loc = TextClickEngine::Location::Center;
    std::string outputPath;
    std::string imagePath;
    bool showHelp = false;

    for (size_t i = 0; i < args.size(); i++) {
        const std::string& a = args[i];
        auto next = [&](std::string& dst) -> bool {
            if (i + 1 < args.size()) { dst = args[++i]; return true; }
            return false;
        };

        // 跳过 --tc 前缀本身
        if (a == "--tc" || a == "-tc") continue;

        if (a == "-h" || a == "-help" || a == "--help" || a == "-\xE5\xB8\xAE\xE5\x8A\xA9") { // -帮助
            showHelp = true;
        }
        else if (a == "-get" || a == "-\xE8\x8E\xB7\xE5\x8F\x96") { action = "get"; }                       // -获取
        else if (a == "-click" || a == "-\xE5\x8D\x95\xE5\x87\xBB") { action = "click"; next(text); }       // -单击
        else if (a == "-double" || a == "-\xE5\x8F\x8C\xE5\x87\xBB") { action = "double"; next(text); }     // -双击
        else if (a == "-right" || a == "-\xE5\x8F\xB3\xE5\x87\xBB") { action = "right"; next(text); }       // -右击
        else if (a == "-move" || a == "-\xE7\xA7\xBB\xE5\x8A\xA8") { action = "move"; next(text); }         // -移动
        else if (a == "-check" || a == "-\xE6\xA3\x80\xE6\x9F\xA5") { action = "check"; next(text); }       // -检查
        else if (a == "-pos" || a == "-\xE5\x9D\x90\xE6\xA0\x87") { action = "pos"; next(text); }           // -坐标
        else if (a == "-posall" || a == "-\xE5\x85\xA8\xE9\x83\xA8\xE5\x9D\x90\xE6\xA0\x87") { action = "posall"; } // -全部坐标
        else if (a == "-list" || a == "-\xE5\x88\x97\xE8\xA1\xA8") { action = "list"; }                     // -列表
        else if (a == "-n" || a == "-\xE7\xAC\xAC\xE5\x87\xA0\xE4\xB8\xAA") {                               // -第几个
            std::string v; if (next(v)) { try { occurrence = std::stoi(v); } catch (...) {} }
        }
        else if (a == "-r" || a == "-\xE5\x8C\xBA\xE5\x9F\x9F") { next(regionStr); }                        // -区域
        else if (a == "-loc" || a == "-\xE4\xBD\x8D\xE7\xBD\xAE") {                                         // -位置
            std::string v; if (next(v)) loc = TextClickEngine::parseLocation(v);
        }
        else if (a == "-output" || a == "-\xE8\xBE\x93\xE5\x87\xBA") { next(outputPath); }                  // -输出
        else if (a == "-image" || a == "-\xE5\x9B\xBE\xE7\x89\x87") { next(imagePath); }                    // -图片
    }

    if (showHelp) {
        printTcHelp("OcrQtCpp.exe");
        return 0;
    }

    if (action.empty()) {
        printTcHelp("OcrQtCpp.exe");
        return 0;
    }

    TextClickEngine eng(ocr);

    // 截屏 或 识别图片
    int blockCount = 0;
    if (!imagePath.empty()) {
        blockCount = eng.detectImage(imagePath);
        if (blockCount < 0) {
            std::cerr << "[textclick] 图片识别失败: " << imagePath << std::endl;
            emitResult(false, "", "图片检测失败");
            return 1;
        }
    } else {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        if (!regionStr.empty()) {
            parseRegion(regionStr, rx, ry, rw, rh);
        }
        blockCount = eng.detectScreen(rx, ry, rw, rh);
        if (blockCount < 0) {
            std::cerr << "[textclick] 屏幕截图失败" << std::endl;
            emitResult(false, "", "屏幕截图失败");
            return 1;
        }
    }

    // 标注图片输出 (在任何动作前, 基于本次识别结果)
    if (!outputPath.empty()) {
        if (eng.saveResultImage(outputPath)) {
            std::cerr << "[textclick] 已保存标注图片: " << outputPath << std::endl;
        } else {
            std::cerr << "[textclick] 保存标注图片失败" << std::endl;
        }
    }

    // 分发动作
    if (action == "get") {
        std::string all = eng.getAllText();
        TextClickEngine::setClipboardText(all);
        std::cerr << "[textclick] 已复制到剪贴板" << std::endl;
        emitResult(true, all, "文本已复制到剪贴板");
    }
    else if (action == "list") {
        emitListResult(eng);
    }
    else if (action == "check") {
        bool found = false;
        for (int i = 0; i < blockCount; i++) {
            if (eng.getBlockText(i).find(text) != std::string::npos) {
                found = true;
                break;
            }
        }
        std::string r = found ? "\xE6\x98\xAF" : "\xE5\x90\xA6"; // 是 / 否
        TextClickEngine::setClipboardText(r);
        emitResult(true, r, "检查完成");
    }
    else if (action == "pos") {
        int x, y;
        if (eng.findPoint(text, occurrence, loc, x, y)) {
            std::string coord = std::to_string(x) + "," + std::to_string(y);
            TextClickEngine::setClipboardText(coord);
            emitResult(true, coord, "坐标已复制到剪贴板");
        } else {
            emitResult(false, "", "未找到指定文字");
            return 1;
        }
    }
    else if (action == "posall") {
        std::ostringstream oss;
        for (int i = 0; i < blockCount; i++) {
            if (i > 0) oss << "\n";
            int box[8];
            eng.getBlockBox(i, box);
            oss << eng.getBlockText(i) << ": " << TextClickEngine::getCoordFromBox(box, loc);
        }
        std::string r = oss.str();
        TextClickEngine::setClipboardText(r);
        emitResult(true, r, "坐标已复制到剪贴板");
    }
    else if (action == "click" || action == "double" || action == "right" || action == "move") {
        int x, y;
        if (!eng.findPoint(text, occurrence, loc, x, y)) {
            emitResult(false, "", "未找到指定文字");
            return 1;
        }
        TextClickEngine::MouseAction ma = TextClickEngine::MouseAction::Move;
        std::string msg;
        if (action == "click")        { ma = TextClickEngine::MouseAction::Click;       msg = "已单击"; }
        else if (action == "double")  { ma = TextClickEngine::MouseAction::DoubleClick; msg = "已双击"; }
        else if (action == "right")   { ma = TextClickEngine::MouseAction::RightClick;  msg = "已右击"; }
        else                          { ma = TextClickEngine::MouseAction::Move;        msg = "已移动"; }
        TextClickEngine::performMouseAction(ma, x, y);
        std::string coord = std::to_string(x) + "," + std::to_string(y);
        std::cerr << "[textclick] " << msg << ": " << text << " (" << coord << ")" << std::endl;
        emitResult(true, coord, msg + ": " + text + " (" + coord + ")");
    }
    else {
        emitResult(false, "", "未知操作");
        return 1;
    }

    return 0;
}
