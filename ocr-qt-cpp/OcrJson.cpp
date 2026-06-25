#include "OcrJson.h"
#include <sstream>
#include <iomanip>

// Base64 编码 (供 "pdf" 字段用, 把可搜索 PDF 字节编码进 JSON)
static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string base64Encode(const std::string& data) {
    std::string out;
    size_t len = data.size();
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < len) {
        unsigned int n = (unsigned char)data[i] << 16
                       | (unsigned char)data[i+1] << 8
                       | (unsigned char)data[i+2];
        out += kBase64Table[(n >> 18) & 0x3F];
        out += kBase64Table[(n >> 12) & 0x3F];
        out += kBase64Table[(n >> 6) & 0x3F];
        out += kBase64Table[n & 0x3F];
        i += 3;
    }
    size_t rem = len - i;
    if (rem == 1) {
        unsigned int n = (unsigned char)data[i] << 16;
        out += kBase64Table[(n >> 18) & 0x3F];
        out += kBase64Table[(n >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        unsigned int n = (unsigned char)data[i] << 16
                       | (unsigned char)data[i+1] << 8;
        out += kBase64Table[(n >> 18) & 0x3F];
        out += kBase64Table[(n >> 12) & 0x3F];
        out += kBase64Table[(n >> 6) & 0x3F];
        out += "=";
    }
    return out;
}

// 转义 JSON 字符串里的特殊字符
static std::string escapeJson(const std::string& s) {
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

std::string OcrJson::blocksToJson(const std::vector<OcrBlock>& blocks,
                                   const std::string& filename,
                                   const std::string& pdfBase64) {
    std::ostringstream ss;
    // 先拼接全部文本 (用每块的 end 分隔符, 排版后可能是 ''/' '/'\n')
    std::string allText;
    for (const auto& b : blocks) {
        allText += b.text;
        allText += b.end;  // 排版算法写入的分隔符; 默认 "\n"
    }

    ss << "{";
    if (!filename.empty()) {
        ss << "\"file\":\"" << escapeJson(filename) << "\",";
    }
    ss << "\"code\":0,";
    ss << "\"text\":\"" << escapeJson(allText) << "\",";
    ss << "\"blockCount\":" << blocks.size() << ",";
    if (!pdfBase64.empty()) {
        ss << "\"pdf\":\"" << pdfBase64 << "\",";
    }
    ss << "\"blocks\":[";
    for (size_t i = 0; i < blocks.size(); i++) {
        const OcrBlock& b = blocks[i];
        if (i > 0) ss << ",";
        ss << "{";
        ss << "\"text\":\"" << escapeJson(b.text) << "\",";
        ss << "\"score\":" << std::fixed << std::setprecision(4) << b.score << ",";
        ss << "\"box\":[";
        for (size_t j = 0; j < b.box.size(); j++) {
            if (j > 0) ss << ",";
            ss << b.box[j];
        }
        ss << "],";
        ss << "\"angle\":" << b.angle << ",";
        ss << "\"angleScore\":" << std::fixed << std::setprecision(4) << b.angleScore << ",";
        ss << "\"end\":\"" << escapeJson(b.end) << "\"";
        ss << "}";
    }
    ss << "]}";
    return ss.str();
}

// 普通模式: 仅返回拼接文本, 不含坐标 (体积小, 命令行/简单调用友好)
std::string OcrJson::textOnlyToJson(const std::vector<OcrBlock>& blocks,
                                     const std::string& filename,
                                     const std::string& pdfBase64) {
    std::ostringstream ss;
    std::string allText;
    for (const auto& b : blocks) {
        allText += b.text;
        allText += b.end;  // 排版算法写入的分隔符; 默认 "\n"
    }
    ss << "{";
    if (!filename.empty()) {
        ss << "\"file\":\"" << escapeJson(filename) << "\",";
    }
    ss << "\"code\":0,";
    ss << "\"text\":\"" << escapeJson(allText) << "\",";
    if (!pdfBase64.empty()) {
        ss << "\"pdf\":\"" << pdfBase64 << "\",";
    }
    ss << "\"blockCount\":" << blocks.size();
    ss << "}";
    return ss.str();
}

std::string OcrJson::errorJson(const std::string& msg) {
    std::ostringstream ss;
    ss << "{\"code\":1,\"msg\":\"" << escapeJson(msg) << "\"}";
    return ss.str();
}

std::string OcrJson::fileErrorJson(const std::string& filename, const std::string& msg) {
    std::ostringstream ss;
    ss << "{\"file\":\"" << escapeJson(filename) << "\",\"code\":1,\"msg\":\"" << escapeJson(msg) << "\"}";
    return ss.str();
}
