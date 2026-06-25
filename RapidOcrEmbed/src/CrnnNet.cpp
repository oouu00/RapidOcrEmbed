#include "CrnnNet.h"
#include "OcrUtils.h"
#include <fstream>
#include <numeric>
#include <cstring>
#include <windows.h>

void CrnnNet::setGpuIndex(int gpuIndex) {
    // MNN CPU-only for now
}

CrnnNet::~CrnnNet() {
    if (session) {
        net->releaseSession(session);
        session = nullptr;
    }
    if (net) {
        MNN::Interpreter::destroy(net);
        net = nullptr;
    }
}

void CrnnNet::setNumThread(int numOfThread) {
    numThread = numOfThread;
    config.numThread = numOfThread;
}

void CrnnNet::initModel(const std::string &pathStr, const std::string &keysPath) {
    net = MNN::Interpreter::createFromFile(pathStr.c_str());
    if (!net) {
        throw std::runtime_error("CrnnNet: Failed to load model from file: " + pathStr);
    }
    net->setSessionMode(MNN::Interpreter::Session_Release);
    session = net->createSession(config);
    if (!session) {
        throw std::runtime_error("CrnnNet: Failed to create session");
    }

    std::wstring keysPathW = strToWstr(keysPath);
    std::ifstream in(keysPathW.c_str());
    std::string line;
    if (in) {
        while (getline(in, line)) {
            keys.push_back(line);
        }
    } else {
        printf("The keys.txt file was not found\n");
        return;
    }
    keys.insert(keys.begin(), "#");
    keys.emplace_back(" ");
    printf("total keys size(%lu)\n", keys.size());
}

#ifdef __EMBEDDED_MODELS__
void CrnnNet::initModel(const unsigned char* modelData, size_t modelSize,
                        const unsigned char* keysData, size_t keysSize) {
    printf("CrnnNet: Loading embedded model, size=%zu bytes\n", modelSize);
    net = MNN::Interpreter::createFromBuffer(modelData, modelSize);
    if (!net) {
        printf("CrnnNet: Failed to create interpreter from buffer\n");
        throw std::runtime_error("Failed to create MNN interpreter for rec model");
    }
    net->setSessionMode(MNN::Interpreter::Session_Release);
    session = net->createSession(config);
    if (!session) {
        printf("CrnnNet: Failed to create session\n");
        throw std::runtime_error("Failed to create MNN session for rec model");
    }
    printf("CrnnNet: Session created successfully\n");

    printf("CrnnNet: Loading keys, size=%zu bytes\n", keysSize);
    if (keysData && keysSize > 0) {
        std::string content(reinterpret_cast<const char*>(keysData), keysSize);
        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line)) {
            keys.push_back(line);
        }
    }

    if (keys.empty()) {
        printf("Warning: keys is empty\n");
    }
    printf("CrnnNet: total keys size(%lu)\n", keys.size());
}
#endif

template<class ForwardIterator>
inline static size_t argmax(ForwardIterator first, ForwardIterator last) {
    return std::distance(first, std::max_element(first, last));
}

TextLine CrnnNet::scoreToTextLine(const std::vector<float> &outputData, size_t h, size_t w) {
    auto keySize = keys.size();
    auto dataSize = outputData.size();
    std::string strRes;
    std::vector<float> scores;
    size_t lastIndex = 0;
    size_t maxIndex;
    float maxValue;

    for (size_t i = 0; i < h; i++) {
        size_t start = i * w;
        size_t stop = (i + 1) * w;
        if (stop > dataSize - 1) {
            stop = (i + 1) * w - 1;
        }
        maxIndex = int(argmax(&outputData[start], &outputData[stop]));
        maxValue = float(*std::max_element(&outputData[start], &outputData[stop]));

        if (maxIndex > 0 && maxIndex < keySize && (!(i > 0 && maxIndex == lastIndex))) {
            scores.emplace_back(maxValue);
            strRes.append(keys[maxIndex]);
        }
        lastIndex = maxIndex;
    }
    return {strRes, scores};
}

TextLine CrnnNet::getTextLine(const cv::Mat &src) {
    float scale = (float) dstHeight / (float) src.rows;
    int dstWidth = int((float) src.cols * scale);
    cv::Mat srcResize;
    resize(src, srcResize, cv::Size(dstWidth, dstHeight));
    std::vector<float> inputTensorValues = substractMeanNormalize(srcResize, meanValues, normValues);

    // Resize input tensor
    MNN::Tensor *input = net->getSessionInput(session, nullptr);
    net->resizeTensor(input, {1, srcResize.channels(), srcResize.rows, srcResize.cols});
    net->resizeSession(session);

    // Copy data to input tensor
    ::memcpy(input->host<float>(), inputTensorValues.data(), inputTensorValues.size() * sizeof(float));

    // Run inference
    net->runSession(session);

    // Get output tensor
    MNN::Tensor *output = net->getSessionOutput(session, nullptr);
    std::vector<int> outputShape = output->shape();
    int64_t outputCount = std::accumulate(outputShape.begin(), outputShape.end(), (int64_t)1,
                                          std::multiplies<int64_t>());

    std::vector<float> outputData(output->host<float>(), output->host<float>() + outputCount);
    return scoreToTextLine(outputData, outputShape[1], outputShape[2]);
}

std::vector<TextLine> CrnnNet::getTextLines(std::vector<cv::Mat> &partImg, const char *path, const char *imgName) {
    int size = partImg.size();
    std::vector<TextLine> textLines(size);
    for (int i = 0; i < size; ++i) {
        if (isOutputDebugImg) {
            std::string debugImgFile = getDebugImgFilePath(path, imgName, i, "-debug-");
            saveImg(partImg[i], debugImgFile.c_str());
        }

        double startCrnnTime = getCurrentTime();
        TextLine textLine = getTextLine(partImg[i]);
        double endCrnnTime = getCurrentTime();
        textLine.time = endCrnnTime - startCrnnTime;
        textLines[i] = textLine;
    }
    return textLines;
}
