#include "DbNet.h"
#include "OcrUtils.h"
#include <cstring>

void DbNet::setGpuIndex(int gpuIndex) {
    // MNN CPU-only for now
}

DbNet::~DbNet() {
    if (session) {
        net->releaseSession(session);
        session = nullptr;
    }
    if (net) {
        MNN::Interpreter::destroy(net);
        net = nullptr;
    }
}

void DbNet::setNumThread(int numOfThread) {
    numThread = numOfThread;
    config.numThread = numOfThread;
}

void DbNet::initModel(const std::string &pathStr) {
    net = MNN::Interpreter::createFromFile(pathStr.c_str());
    if (!net) {
        throw std::runtime_error("DbNet: Failed to load model from file: " + pathStr);
    }
    net->setSessionMode(MNN::Interpreter::Session_Release);
    session = net->createSession(config);
    if (!session) {
        throw std::runtime_error("DbNet: Failed to create session");
    }
}

#ifdef __EMBEDDED_MODELS__
void DbNet::initModel(const unsigned char* modelData, size_t modelSize) {
    printf("DbNet: Loading embedded model, size=%zu bytes\n", modelSize);
    net = MNN::Interpreter::createFromBuffer(modelData, modelSize);
    if (!net) {
        printf("DbNet: Failed to create interpreter from buffer\n");
        throw std::runtime_error("Failed to create MNN interpreter for det model");
    }
    net->setSessionMode(MNN::Interpreter::Session_Release);
    session = net->createSession(config);
    if (!session) {
        printf("DbNet: Failed to create session\n");
        throw std::runtime_error("Failed to create MNN session for det model");
    }
    printf("DbNet: Model initialized successfully\n");
}
#endif

std::vector<TextBox> findRsBoxes(const cv::Mat &predMat, const cv::Mat &dilateMat, ScaleParam &s,
                                 const float boxScoreThresh, const float unClipRatio) {
    const int longSideThresh = 3;
    const int maxCandidates = 1000;

    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;

    cv::findContours(dilateMat, contours, hierarchy, cv::RETR_LIST,
                     cv::CHAIN_APPROX_SIMPLE);

    size_t numContours = contours.size() >= maxCandidates ? maxCandidates : contours.size();

    std::vector<TextBox> rsBoxes;

    for (size_t i = 0; i < numContours; i++) {
        if (contours[i].size() <= 2) {
            continue;
        }
        cv::RotatedRect minAreaRect = cv::minAreaRect(contours[i]);

        float longSide;
        std::vector<cv::Point2f> minBoxes = getMinBoxes(minAreaRect, longSide);

        if (longSide < longSideThresh) {
            continue;
        }

        float boxScore = boxScoreFast(minBoxes, predMat);
        if (boxScore < boxScoreThresh)
            continue;

        cv::RotatedRect clipRect = unClip(minBoxes, unClipRatio);
        if (clipRect.size.height < 1.001 && clipRect.size.width < 1.001) {
            continue;
        }

        std::vector<cv::Point2f> clipMinBoxes = getMinBoxes(clipRect, longSide);
        if (longSide < longSideThresh + 2)
            continue;

        std::vector<cv::Point> intClipMinBoxes;

        for (auto &clipMinBox: clipMinBoxes) {
            float x = clipMinBox.x / s.ratioWidth;
            float y = clipMinBox.y / s.ratioHeight;
            int ptX = (std::min)((std::max)(int(x), 0), s.srcWidth - 1);
            int ptY = (std::min)((std::max)(int(y), 0), s.srcHeight - 1);
            cv::Point point{ptX, ptY};
            intClipMinBoxes.push_back(point);
        }
        rsBoxes.push_back(TextBox{intClipMinBoxes, boxScore});
    }
    reverse(rsBoxes.begin(), rsBoxes.end());
    return rsBoxes;
}

std::vector<TextBox>
DbNet::getTextBoxes(cv::Mat &src, ScaleParam &s, float boxScoreThresh, float boxThresh, float unClipRatio) {
    cv::Mat srcResize;
    resize(src, srcResize, cv::Size(s.dstWidth, s.dstHeight));
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
    int outHeight = outputShape[2];
    int outWidth = outputShape[3];
    size_t area = outHeight * outWidth;

    // Copy output data
    std::vector<float> outputData(output->host<float>(), output->host<float>() + area);

    //-----Data preparation-----
    std::vector<float> predData(area, 0.0);
    std::vector<unsigned char> cbufData(area, ' ');

    for (int i = 0; i < (int)area; i++) {
        predData[i] = float(outputData[i]);
        cbufData[i] = (unsigned char) ((outputData[i]) * 255);
    }

    cv::Mat predMat(outHeight, outWidth, CV_32F, (float *) predData.data());
    cv::Mat cBufMat(outHeight, outWidth, CV_8UC1, (unsigned char *) cbufData.data());

    //-----boxThresh-----
    const double maxValue = 255;
    const double threshold = boxThresh * 255;
    cv::Mat thresholdMat;
    cv::threshold(cBufMat, thresholdMat, threshold, maxValue, cv::THRESH_BINARY);

    //-----dilate-----
    cv::Mat dilateMat;
    cv::Mat dilateElement = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
    cv::dilate(thresholdMat, dilateMat, dilateElement);

    return findRsBoxes(predMat, dilateMat, s, boxScoreThresh, unClipRatio);
}
