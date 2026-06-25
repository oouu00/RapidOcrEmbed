#ifndef TEXTCLICKENGINE_H
#define TEXTCLICKENGINE_H

#include "OCRWrapper.h"
#include <string>
#include <vector>
#include <utility>

// 文字点击引擎: 移植自 TextClick 的核心逻辑
// 程序化截图 (非交互) -> OCR (复用 OCRWrapper) -> 按文字精确定位坐标 -> 鼠标操作/剪贴板
// 纯逻辑层, 不含网络/CLI 代码, 被 OcrHttpServer 与 TextClickCli 共用
// 不拥有 OCRWrapper 的生命周期 (由外部管理)
class TextClickEngine {
public:
    // 文字框位置点
    enum class Location {
        Center = 0,
        TopLeft = 1,
        TopRight = 2,
        BottomLeft = 3,
        BottomRight = 4
    };

    // 鼠标动作类型
    enum class MouseAction {
        Move,       // 仅移动
        Click,      // 单击左键
        DoubleClick,// 双击左键
        RightClick  // 右击
    };

    explicit TextClickEngine(OCRWrapper* ocr);

    // 直接注入已有 OCR 结果 (避免重复 OCR)
    void setBlocks(const std::vector<OcrBlock>& blocks);

    // 程序化截图并 OCR (region=0/负值 表全屏)
    // 返回文字块数量, <0 表示失败
    int detectScreen(int x = 0, int y = 0, int width = 0, int height = 0);

    // 识别图片文件
    // 返回文字块数量, <0 表示失败
    int detectImage(const std::string& imagePath);

    // 保存带框标注的结果图片 (基于最近一次截图/图片)
    bool saveResultImage(const std::string& outputPath) const;

    // 取当前识别结果
    const std::vector<OcrBlock>& blocks() const { return m_blocks; }
    int blockCount() const { return (int)m_blocks.size(); }
    std::string getBlockText(int index) const;
    void getBlockBox(int index, int box[8]) const;
    float getBlockScore(int index) const;
    std::string getAllText() const;

    // 在当前结果中查找第 occurrence 个匹配 text 的文字块, 算出精确坐标
    // (UTF-8 部分匹配 + 按字符比例算偏移, 与 TextClick 行为一致)
    // 找到返回 true 并写入 outX/outY, 否则返回 false
    bool findPoint(const std::string& text, int occurrence,
                   Location loc, int& outX, int& outY) const;

    // 执行鼠标动作 (move/click/double/right), 先移动到 (x,y)
    static bool performMouseAction(MouseAction act, int x, int y);

    // ---- 静态工具 (与 TextClick 行为一致, 供外部直接使用) ----
    static Location parseLocation(const std::string& pos);
    static void getPointFromBox(const int box[8], Location loc, int& x, int& y);
    static void getPointFromBoxPartial(const int box[8], Location loc,
                                       const std::string& fullText,
                                       int charStart, int charEnd,
                                       int& x, int& y);
    static std::string getCoordFromBox(const int box[8], Location loc);
    static std::string getCoordFromBoxPartial(const int box[8], Location loc,
                                              const std::string& fullText,
                                              int charStart, int charEnd);
    static int utf8CharCount(const std::string& str);
    static int utf8CharToByteOffset(const std::string& str, int charIndex);

    // 垂直投影切分字符: 从灰度图的 box 区域中切出每个字符的 x 边界
    static std::vector<std::pair<int,int>> projectCharSegments(
        const unsigned char* gray, int imgW, int imgH,
        const int box[8]);

    // 加权字符比例: 根据字符类型(中/英/其他)估算宽度, 返回归一化比例 [0,1]
    static float getWeightedCharRatio(const std::string& text, int charIndex);

    // 鼠标 / 剪贴板
    static void moveTo(int x, int y);
    static void click();
    static void doubleClick();
    static void rightClick();
    static bool setClipboardText(const std::string& text);
    static std::string getClipboardText();

private:
    OCRWrapper* m_ocr;  // 外部传入, 不拥有
    std::vector<OcrBlock> m_blocks;
    // 最近一次截图/图片的原始数据 (用于 saveResultImage 标注), 存为 BMP 字节
    std::vector<unsigned char> m_lastImageBmp;
    int m_lastImageWidth;
    int m_lastImageHeight;
    // 灰度像素数据 (用于垂直投影切分字符)
    std::vector<unsigned char> m_lastImageGray;
    int m_lastImageGrayW = 0;
    int m_lastImageGrayH = 0;
};

#endif // TEXTCLICKENGINE_H
