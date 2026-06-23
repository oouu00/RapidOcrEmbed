#include "AngleNet.h"
#include "OcrUtils.h"
#include <cstring>

void AngleNet::setGpuIndex(int gpuIndex) {
    // MNN CPU-only for now
}

AngleNet::~AngleNet() {
    if (session) {
        net->releaseSession(session);
        session = nullptr;
    }
    if (net) {
        MNN::Interpreter::destroy(net);
        net = nullptr;
    }
}

void AngleNet::setNumThread(int numOfThread) {
    numThread = numOfThread;
    config.numThread = numOfThread;
}

void AngleNet::initModel(const std::string &pathStr) {
    net = MNN::Interpreter::createFromFile(pathStr.c_str());
    if (!net) {
        throw std::runtime_error("AngleNet: Failed to load model from file: " + pathStr);
    }
    net->setSessionMode(MNN::Interpreter::Session_Release);
    session = net->createSession(config);
    if (!session) {
        throw std::runtime_error("AngleNet: Failed to create session");
    }
}

#ifdef __EMBEDDED_MODELS__
void AngleNet::initModel(const unsigned char* modelData, size_t modelSize) {
    printf("AngleNet: Loading embedded model, size=%zu bytes\n", modelSize);
    net = MNN::Interpreter::createFromBuffer(modelData, modelSize);
    if (!net) {
        printf("AngleNet: Failed to create interpreter from buffer\n");
        throw std::runtime_error("Failed to create MNN interpreter for cls model");
    }
    net->setSessionMode(MNN::Interpreter::Session_Release);
    session = net->createSession(config);
    if (!session) {
        printf("AngleNet: Failed to create session\n");
        throw std::runtime_error("Failed to create MNN session for cls model");
    }
    printf("AngleNet: Model initialized successfully\n");
}
#endif

Angle AngleNet::getAngle(cv::Mat &src) {
    cv::Mat resizeImg;
    cv::resize(src, resizeImg, cv::Size(160, 80));

    cv::cvtColor(resizeImg, resizeImg, cv::COLOR_BGR2RGB);

    resizeImg.convertTo(resizeImg, CV_32FC3, 1.0 / 255.0);

    const float mean[3] = {0.485f, 0.456f, 0.406f};
    const float norm[3] = {1.0f / 0.229f, 1.0f / 0.224f, 1.0f / 0.225f};

    std::vector<float> inputTensorValues;
    for (int c = 0; c < 3; c++) {
        for (int h = 0; h < resizeImg.rows; h++) {
            for (int w = 0; w < resizeImg.cols; w++) {
                float val = resizeImg.at<cv::Vec3f>(h, w)[c];
                inputTensorValues.push_back((val - mean[c]) * norm[c]);
            }
        }
    }

    // Resize input tensor
    MNN::Tensor *input = net->getSessionInput(session, nullptr);
    net->resizeTensor(input, {1, 3, resizeImg.rows, resizeImg.cols});
    net->resizeSession(session);

    // Copy data to input tensor
    ::memcpy(input->host<float>(), inputTensorValues.data(), inputTensorValues.size() * sizeof(float));

    // Run inference
    net->runSession(session);

    // Get output tensor
    MNN::Tensor *output = net->getSessionOutput(session, nullptr);
    float *floatArray = output->host<float>();

    int angleIndex = 0;
    float maxScore = floatArray[0];
    for (int i = 1; i < 2; i++) {
        if (floatArray[i] > maxScore) {
            maxScore = floatArray[i];
            angleIndex = i;
        }
    }

    Angle angle;
    angle.index = angleIndex;
    angle.score = maxScore;
    angle.time = 0;

    return angle;
}
