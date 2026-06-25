#ifndef __OCR_LITE_H__
#define __OCR_LITE_H__

#include "opencv2/core.hpp"
#include "OcrStruct.h"
#include "DbNet.h"
#include "AngleNet.h"
#include "CrnnNet.h"
#include "TableNet.h"
#include <future>

class OcrLite {
public:
    OcrLite();

    ~OcrLite();

    void setNumThread(int numOfThread);

    void initLogger(bool isConsole, bool isPartImg, bool isResultImg);

    void enableResultTxt(const char *path, const char *imgName);

    void setGpuIndex(int gpuIndex);

    void setLayoutStrategy(const std::string &strategy);

    std::string getLayoutStrategy() const;

    bool initModels(const std::string &detPath, const std::string &clsPath,
                    const std::string &recPath, const std::string &keysPath);

#ifdef __EMBEDDED_MODELS__
    bool initModels();
#endif

    bool initTableModel(const std::string &tableModelPath, const std::string &tableDictPath);

#ifdef __EMBEDDED_MODELS__
    bool initTableModel();
#endif

    TableDetectResult detectTable(const cv::Mat &mat, int padding, int maxSideLen,
                                   float boxScoreThresh, float boxThresh, float unClipRatio,
                                   bool doAngle);

    void Logger(const char *format, ...);

    OcrResult detect(const char *path, const char *imgName,
                     int padding, int maxSideLen,
                     float boxScoreThresh, float boxThresh, float unClipRatio, bool doAngle);

    OcrResult detect(const cv::Mat &mat,
                     int padding, int maxSideLen,
                     float boxScoreThresh, float boxThresh, float unClipRatio, bool doAngle);

private:
    bool isOutputConsole = false;
    bool isOutputPartImg = false;
    bool isOutputResultTxt = false;
    bool isOutputResultImg = false;
    FILE *resultTxt;
    std::string resultTxtPathForWrite;
    std::string layoutStrategy;  // 排版策略, 默认空字符串(不排版)
    DbNet dbNet;
    AngleNet angleNet;
    CrnnNet crnnNet;
    TableNet tableNet;

    std::vector<cv::Mat> getPartImages(cv::Mat &src, std::vector<TextBox> &textBoxes,
                                       const char *path, const char *imgName);

    OcrResult detect(const char *path, const char *imgName,
                     cv::Mat &src, cv::Rect &originRect, ScaleParam &scale,
                     float boxScoreThresh = 0.6f, float boxThresh = 0.3f,
                     float unClipRatio = 2.0f, bool doAngle = true);
};

#endif //__OCR_LITE_H__
