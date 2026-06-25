#ifndef __OCR_CRNNNET_H__
#define __OCR_CRNNNET_H__

#include "OcrStruct.h"
#include <MNN/Interpreter.hpp>
#include <MNN/Tensor.hpp>
#include <opencv2/opencv.hpp>

class CrnnNet {
public:

    ~CrnnNet();

    void setNumThread(int numOfThread);

    void setGpuIndex(int gpuIndex);

    void initModel(const std::string &pathStr, const std::string &keysPath);

#ifdef __EMBEDDED_MODELS__
    void initModel(const unsigned char* modelData, size_t modelSize,
                   const unsigned char* keysData, size_t keysSize);
#endif

    std::vector<TextLine> getTextLines(std::vector<cv::Mat> &partImg, const char *path, const char *imgName);

private:
    bool isOutputDebugImg = false;
    MNN::Interpreter *net = nullptr;
    MNN::Session *session = nullptr;
    MNN::ScheduleConfig config;
    int numThread = 4;

    const float meanValues[3] = {127.5f, 127.5f, 127.5f};
    const float normValues[3] = {1.0f / 127.5f, 1.0f / 127.5f, 1.0f / 127.5f};
    const int dstHeight = 48;

    std::vector<std::string> keys;

    TextLine scoreToTextLine(const std::vector<float> &outputData, size_t h, size_t w);

    TextLine getTextLine(const cv::Mat &src);
};


#endif //__OCR_CRNNNET_H__
