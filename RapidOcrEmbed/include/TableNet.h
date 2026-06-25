#ifndef __OCR_TABLENET_H__
#define __OCR_TABLENET_H__

#include "OcrStruct.h"
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>

struct TableCell {
    std::vector<cv::Point> boxPoint;
    int colspan;
    int rowspan;
    std::string text;
};

struct TableResult {
    std::string htmlStructure;
    float structureScore;
    std::vector<TableCell> cells;
    std::vector<std::vector<int>> cellBoxes;
};

class TableNet {
public:
    ~TableNet();

    void setNumThread(int numOfThread);

    void setGpuIndex(int gpuIndex);

    void initModel(const std::string &pathStr);

#ifdef __EMBEDDED_MODELS__
    void initModel(const unsigned char* modelData, size_t modelSize);
#endif

    void loadDict(const std::string &dictPath);

#ifdef __EMBEDDED_MODELS__
    void loadDict(const unsigned char* dictData, size_t dictSize);
#endif

    TableResult recognize(const cv::Mat &src, int padding, int maxSideLen);

    void fillCellTexts(TableResult &result, const std::vector<TextBlock> &ocrBlocks);

private:
    Ort::Session *session = nullptr;
    Ort::Env env = Ort::Env(ORT_LOGGING_LEVEL_ERROR, "TableNet");
    Ort::SessionOptions sessionOptions = Ort::SessionOptions();
    int numThread = 0;

    std::vector<Ort::AllocatedStringPtr> inputNamesPtr;
    std::vector<Ort::AllocatedStringPtr> outputNamesPtr;

    std::vector<std::string> charDict;

    const float meanValues[3] = {0.485f * 255.0f, 0.456f * 255.0f, 0.406f * 255.0f};
    const float normValues[3] = {1.0f / 0.229f / 255.0f, 1.0f / 0.224f / 255.0f, 1.0f / 0.225f / 255.0f};

    TableResult decodeOutput(std::vector<Ort::Value> &outputs,
                             int origWidth, int origHeight,
                             int resizedWidth, int resizedHeight);
};

#endif //__OCR_TABLENET_H__
