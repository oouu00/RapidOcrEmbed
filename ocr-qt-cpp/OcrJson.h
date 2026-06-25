#ifndef OCRJSON_H
#define OCRJSON_H

#include "OCRWrapper.h"
#include <string>
#include <vector>

// 把 OCR 结果序列化为 JSON
// 普通模式: {"code":0,"text":"...","blockCount":N}
// 坐标模式: {"code":0,"text":"...","blockCount":N,"blocks":[{...}]}
// 带 filename: {"file":"a.jpg","code":0,"text":"...","blockCount":N}
class OcrJson {
public:
    // 坐标模式: 完整 JSON 含 blocks 数组
    // pdfBase64 非空时附加 "pdf" 字段 (base64 编码的可搜索 PDF, 网页端 ?pdf=1 用)
    static std::string blocksToJson(const std::vector<OcrBlock>& blocks,
                                    const std::string& filename = "",
                                    const std::string& pdfBase64 = "");
    // 普通模式: 仅 text, 不含 blocks (体积小)
    static std::string textOnlyToJson(const std::vector<OcrBlock>& blocks,
                                      const std::string& filename = "",
                                      const std::string& pdfBase64 = "");
    // 错误
    static std::string errorJson(const std::string& msg);
    // 带文件名的错误 (多文件模式)
    static std::string fileErrorJson(const std::string& filename, const std::string& msg);
};

#endif // OCRJSON_H
