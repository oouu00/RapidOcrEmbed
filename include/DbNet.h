#ifndef __OCR_DBNET_H__
#define __OCR_DBNET_H__

#include "OcrStruct.h"
#include <MNN/Interpreter.hpp>
#include <MNN/Tensor.hpp>
#include <opencv2/opencv.hpp>

class DbNet {
public:
    ~DbNet();

    void setNumThread(int numOfThread);

    void setGpuIndex(int gpuIndex);

    void initModel(const std::string &pathStr);

#ifdef __EMBEDDED_MODELS__
    void initModel(const unsigned char* modelData, size_t modelSize);
#endif

    std::vector<TextBox> getTextBoxes(cv::Mat &src, ScaleParam &s, float boxScoreThresh,
                                      float boxThresh, float unClipRatio);

private:
    MNN::Interpreter *net = nullptr;
    MNN::Session *session = nullptr;
    MNN::ScheduleConfig config;
    int numThread = 4;

    const float meanValues[3] = {0.485f * 255.0f, 0.456f * 255.0f, 0.406f * 255.0f};
    const float normValues[3] = {1.0f / 0.229f / 255.0f, 1.0f / 0.224f / 255.0f, 1.0f / 0.225f / 255.0f};
};


#endif //__OCR_DBNET_H__
