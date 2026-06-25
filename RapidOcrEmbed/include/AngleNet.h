#ifndef __OCR_ANGLENET_H__
#define __OCR_ANGLENET_H__

#include "OcrStruct.h"
#include <MNN/Interpreter.hpp>
#include <MNN/Tensor.hpp>
#include <opencv2/opencv.hpp>

class AngleNet {
public:
    ~AngleNet();

    void setNumThread(int numOfThread);

    void setGpuIndex(int gpuIndex);

    void initModel(const std::string &pathStr);

#ifdef __EMBEDDED_MODELS__
    void initModel(const unsigned char* modelData, size_t modelSize);
#endif

    Angle getAngle(cv::Mat &src);

private:
    MNN::Interpreter *net = nullptr;
    MNN::Session *session = nullptr;
    MNN::ScheduleConfig config;
    int numThread = 4;
};


#endif //__OCR_ANGLENET_H__
