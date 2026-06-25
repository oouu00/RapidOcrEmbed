#ifndef __OCR_STRUCT_H__
#define __OCR_STRUCT_H__

#include "opencv2/core.hpp"
#include <vector>

struct ScaleParam {
    int srcWidth;
    int srcHeight;
    int dstWidth;
    int dstHeight;
    float ratioWidth;
    float ratioHeight;
};

struct TextBox {
    std::vector<cv::Point> boxPoint;
    float score;
};

struct Angle {
    int index;
    float score;
    double time;
};

struct TextLine {
    std::string text;
    std::vector<float> charScores;
    double time;
};

struct TextBlock {
    std::vector<cv::Point> boxPoint;
    float boxScore;
    int angleIndex;
    float angleScore;
    double angleTime;
    std::string text;
    std::vector<float> charScores;
    double crnnTime;
    double blockTime;
    std::string end = "\n";  // 排版算法写入的结尾分隔符
};

struct OcrResult {
    double dbNetTime;
    std::vector<TextBlock> textBlocks;
    cv::Mat boxImg;
    double detectTime;
    std::string strRes;
};

struct TableDetectResult {
    std::string htmlStructure;
    float structureScore;
    std::string ocrText;
    double detectTime;
    std::vector<std::vector<int>> cellBoxes;
};

#endif //__OCR_STRUCT_H__
