#include <windows.h>
#include "OcrLite.h"
#include "OcrUtils.h"
#include "TbpuLayout.h"
#include <stdarg.h> //windows&linux

#ifdef __EMBEDDED_MODELS__
#include "models/embedded_models.h"
#endif

OcrLite::OcrLite() {}

OcrLite::~OcrLite() {
    if (isOutputResultTxt) {
        fclose(resultTxt);
    }
}

void OcrLite::setNumThread(int numOfThread) {
    dbNet.setNumThread(numOfThread);
    angleNet.setNumThread(numOfThread);
    crnnNet.setNumThread(numOfThread);
}

void OcrLite::initLogger(bool isConsole, bool isPartImg, bool isResultImg) {
    isOutputConsole = isConsole;
    isOutputPartImg = isPartImg;
    isOutputResultImg = isResultImg;
}

void OcrLite::enableResultTxt(const char *path, const char *imgName) {
    isOutputResultTxt = true;
    std::string resultTxtPath = getResultTxtFilePath(path, imgName);
    printf("resultTxtPath(%s)\n", resultTxtPath.c_str());
    resultTxtPathForWrite = resultTxtPath; // Store path for later use
}

void OcrLite::setGpuIndex(int gpuIndex) {
    dbNet.setGpuIndex(gpuIndex);
    angleNet.setGpuIndex(gpuIndex);
    crnnNet.setGpuIndex(gpuIndex);
}

void OcrLite::setLayoutStrategy(const std::string &strategy) {
    layoutStrategy = strategy;
}

std::string OcrLite::getLayoutStrategy() const {
    return layoutStrategy;
}

bool OcrLite::initModels(const std::string &detPath, const std::string &clsPath,
                         const std::string &recPath, const std::string &keysPath) {
    Logger("=====Init Models (PP-OCRv6 with Classifier)=====\n");
    Logger("--- Init DbNet ---\n");
    dbNet.initModel(detPath);

    Logger("--- Init AngleNet ---\n");
    angleNet.initModel(clsPath);

    Logger("--- Init CrnnNet ---\n");
    crnnNet.initModel(recPath, keysPath);

    Logger("Init Models Success!\n");
    return true;
}

#ifdef __EMBEDDED_MODELS__
bool OcrLite::initModels() {
    Logger("=====Init Embedded Models (PP-OCRv6 with Classifier)=====\n");
    Logger("--- Init DbNet + AngleNet + CrnnNet (parallel) ---\n");

    auto f1 = std::async(std::launch::async, [&]{
        dbNet.initModel(det_model, det_model_size);
    });
    auto f2 = std::async(std::launch::async, [&]{
        angleNet.initModel(cls_model, cls_model_size);
    });
    auto f3 = std::async(std::launch::async, [&]{
        crnnNet.initModel(rec_model, rec_model_size, keys_data, keys_data_size);
    });
    f1.get(); f2.get(); f3.get();

    Logger("Init Embedded Models Success!\n");
    return true;
}
#endif

bool OcrLite::initTableModel(const std::string &tableModelPath, const std::string &tableDictPath) {
    Logger("=====Init Table Model=====\n");
    Logger("--- Init TableNet ---\n");
    tableNet.initModel(tableModelPath);
    tableNet.loadDict(tableDictPath);
    Logger("Init Table Model Success!\n");
    return true;
}

#ifdef __EMBEDDED_MODELS__
bool OcrLite::initTableModel() {
    Logger("=====Init Embedded Table Model=====\n");
    Logger("--- Init TableNet (embedded) ---\n");
    tableNet.initModel(table_model, table_model_size);
    tableNet.loadDict(table_keys_model, table_keys_model_size);
    Logger("Init Embedded Table Model Success!\n");
    return true;
}
#endif

TableDetectResult OcrLite::detectTable(const cv::Mat &mat, int padding, int maxSideLen,
                                       float boxScoreThresh, float boxThresh, float unClipRatio,
                                       bool doAngle) {
    TableDetectResult result;
    result.detectTime = 0;

    double startTime = getCurrentTime();

    // First run OCR to get text blocks
    OcrResult ocrResult = detect(mat, padding, maxSideLen,
                                 boxScoreThresh, boxThresh, unClipRatio, doAngle);

    // Then run table structure recognition
    TableResult tableResult = tableNet.recognize(mat, padding, maxSideLen);

    // Fill OCR text into table cells
    tableNet.fillCellTexts(tableResult, ocrResult.textBlocks);

    result.htmlStructure = tableResult.htmlStructure;
    result.structureScore = tableResult.structureScore;
    result.ocrText = ocrResult.strRes;
    result.cellBoxes = tableResult.cellBoxes;

    double endTime = getCurrentTime();
    result.detectTime = endTime - startTime;

    return result;
}

void OcrLite::Logger(const char *format, ...) {
    if (!isOutputConsole) return;
    char *buffer = (char *) malloc(8192);
    va_list args;
    va_start(args, format);
    vsprintf_s(buffer, 8192, format, args);
    va_end(args);
    if (isOutputConsole) printf("%s", buffer);
    free(buffer);
}

cv::Mat makePadding(cv::Mat &src, const int padding) {
    if (padding <= 0) return src;
    cv::Scalar paddingScalar = {255, 255, 255};
    cv::Mat paddingSrc;
    cv::copyMakeBorder(src, paddingSrc, padding, padding, padding, padding, cv::BORDER_ISOLATED, paddingScalar);
    return paddingSrc;
}

OcrResult OcrLite::detect(const char *path, const char *imgName,
                          const int padding, const int maxSideLen,
                          float boxScoreThresh, float boxThresh, float unClipRatio, bool doAngle) {
    // Convert path and name to UTF-8 string
    std::string imgFile = getSrcImgFilePath(path, imgName);
    
    // Convert UTF-8 path to wide string
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, imgFile.c_str(), -1, NULL, 0);
    std::wstring wimgFile(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, imgFile.c_str(), -1, &wimgFile[0], size_needed);
    
    // Use wide char API to read file
    HANDLE hFile = CreateFileW(wimgFile.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        Logger("Failed to open file: %s\n", imgFile.c_str());
        return OcrResult();
    }


    if (hFile == INVALID_HANDLE_VALUE) {
        Logger("Failed to open file: %s\n", imgFile.c_str());
        return OcrResult();
    }
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE) {
        Logger("Failed to get file size: %s\n", imgFile.c_str());
        CloseHandle(hFile);
        return OcrResult();
    }
    std::vector<BYTE> buffer(fileSize);
    DWORD bytesRead;
    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
        Logger("Failed to read file: %s (read %d/%d bytes)\n", imgFile.c_str(), bytesRead, fileSize);
        CloseHandle(hFile);
        return OcrResult();
    }
    CloseHandle(hFile);

    cv::Mat originSrc = cv::imdecode(buffer, cv::IMREAD_COLOR);
    int originMaxSide = (std::max)(originSrc.cols, originSrc.rows);
    int resize;
    if (maxSideLen <= 0 || maxSideLen > originMaxSide) {
        resize = originMaxSide;
    } else {
        resize = maxSideLen;
    }
    resize += 2 * padding;
    cv::Rect paddingRect(padding, padding, originSrc.cols, originSrc.rows);
    cv::Mat paddingSrc = makePadding(originSrc, padding);
    ScaleParam scale = getScaleParam(paddingSrc, resize);
    OcrResult result;
    result = detect(path, imgName, paddingSrc, paddingRect, scale,
                    boxScoreThresh, boxThresh, unClipRatio, doAngle);
    return result;
}

OcrResult OcrLite::detect(const cv::Mat &mat, int padding, int maxSideLen, float boxScoreThresh, float boxThresh,
                          float unClipRatio, bool doAngle) {
    cv::Mat originSrc = mat;
    int originMaxSide = (std::max)(originSrc.cols, originSrc.rows);
    int resize;
    if (maxSideLen <= 0 || maxSideLen > originMaxSide) {
        resize = originMaxSide;
    } else {
        resize = maxSideLen;
    }
    resize += 2 * padding;
    cv::Rect paddingRect(padding, padding, originSrc.cols, originSrc.rows);
    cv::Mat paddingSrc = makePadding(originSrc, padding);
    ScaleParam scale = getScaleParam(paddingSrc, resize);
    OcrResult result;
    result = detect(NULL, NULL, paddingSrc, paddingRect, scale,
                    boxScoreThresh, boxThresh, unClipRatio, doAngle);
    return result;
}

std::vector<cv::Mat> OcrLite::getPartImages(cv::Mat &src, std::vector<TextBox> &textBoxes,
                                            const char *path, const char *imgName) {
    std::vector<cv::Mat> partImages;
    for (size_t i = 0; i < textBoxes.size(); ++i) {
        cv::Mat partImg = getRotateCropImage(src, textBoxes[i].boxPoint);
        partImages.emplace_back(partImg);
        //OutPut DebugImg
        if (isOutputPartImg) {
            std::string debugImgFile = getDebugImgFilePath(path, imgName, i, "-part-");
            saveImg(partImg, debugImgFile.c_str());
        }
    }
    return partImages;
}

OcrResult OcrLite::detect(const char *path, const char *imgName,
                          cv::Mat &src, cv::Rect &originRect, ScaleParam &scale,
                          float boxScoreThresh, float boxThresh, float unClipRatio, bool doAngle) {

    cv::Mat textBoxPaddingImg = src.clone();
    int thickness = getThickness(src);

    Logger("=====Start detect=====\n");
    Logger("ScaleParam(sw:%d,sh:%d,dw:%d,dh:%d,%f,%f)\n", scale.srcWidth, scale.srcHeight,
           scale.dstWidth, scale.dstHeight,
           scale.ratioWidth, scale.ratioHeight);

    Logger("---------- step: dbNet getTextBoxes ----------\n");
    double startTime = getCurrentTime();
    std::vector<TextBox> textBoxes = dbNet.getTextBoxes(src, scale, boxScoreThresh, boxThresh, unClipRatio);
    double endDbNetTime = getCurrentTime();
    double dbNetTime = endDbNetTime - startTime;
    Logger("dbNetTime(%fms)\n", dbNetTime);

    for (size_t i = 0; i < textBoxes.size(); ++i) {
        Logger("TextBox[%d](+padding)[score(%f),[x: %d, y: %d], [x: %d, y: %d], [x: %d, y: %d], [x: %d, y: %d]]\n", i,
               textBoxes[i].score,
               textBoxes[i].boxPoint[0].x, textBoxes[i].boxPoint[0].y,
               textBoxes[i].boxPoint[1].x, textBoxes[i].boxPoint[1].y,
               textBoxes[i].boxPoint[2].x, textBoxes[i].boxPoint[2].y,
               textBoxes[i].boxPoint[3].x, textBoxes[i].boxPoint[3].y);
    }

    Logger("---------- step: drawTextBoxes ----------\n");
    drawTextBoxes(textBoxPaddingImg, textBoxes, thickness);

    //---------- getPartImages ----------
    std::vector<cv::Mat> partImages = getPartImages(src, textBoxes, path, imgName);

    Logger("---------- step: angleNet getAngles ----------\n");
    std::vector<Angle> angles;
    if (doAngle) {
        for (size_t i = 0; i < partImages.size(); ++i) {
            Angle angle = angleNet.getAngle(partImages[i]);
            angles.emplace_back(angle);
            // Rotate image based on angle index
            // angle.index: 0=normal, 1=rotated 180 degrees
            if (angle.index == 1) {
                partImages[i] = matRotateClockWise180(partImages[i]);
            }
        }
    } else {
        for (size_t i = 0; i < partImages.size(); ++i) {
            angles.emplace_back(Angle{0, 0.0f, 0.0f});
        }
    }

    //Log Angles
    for (size_t i = 0; i < angles.size(); ++i) {
        Logger("angle[%d][index(%d), score(%f), time(%fms)]\n", i, angles[i].index, angles[i].score, angles[i].time);
    }

    Logger("---------- step: crnnNet getTextLine ----------\n");
    std::vector<TextLine> textLines = crnnNet.getTextLines(partImages, path, imgName);
    //Log TextLines
    for (size_t i = 0; i < textLines.size(); ++i) {
        Logger("textLine[%d](%s)\n", i, textLines[i].text.c_str());
        std::ostringstream txtScores;
        for (size_t s = 0; s < textLines[i].charScores.size(); ++s) {
            if (s == 0) {
                txtScores << textLines[i].charScores[s];
            } else {
                txtScores << " ," << textLines[i].charScores[s];
            }
        }
        Logger("textScores[%d]{%s}\n", i, std::string(txtScores.str()).c_str());
        Logger("crnnTime[%d](%fms)\n", i, textLines[i].time);
    }

    std::vector<TextBlock> textBlocks;
    for (size_t i = 0; i < textLines.size(); ++i) {
        std::vector<cv::Point> boxPoint = std::vector<cv::Point>(4);
        int padding = originRect.x;//padding conversion
        boxPoint[0] = cv::Point(textBoxes[i].boxPoint[0].x - padding, textBoxes[i].boxPoint[0].y - padding);
        boxPoint[1] = cv::Point(textBoxes[i].boxPoint[1].x - padding, textBoxes[i].boxPoint[1].y - padding);
        boxPoint[2] = cv::Point(textBoxes[i].boxPoint[2].x - padding, textBoxes[i].boxPoint[2].y - padding);
        boxPoint[3] = cv::Point(textBoxes[i].boxPoint[3].x - padding, textBoxes[i].boxPoint[3].y - padding);
        TextBlock textBlock{boxPoint, textBoxes[i].score, angles[i].index, angles[i].score,
                            angles[i].time, textLines[i].text, textLines[i].charScores, textLines[i].time,
                            angles[i].time + textLines[i].time};
        textBlocks.emplace_back(textBlock);
    }

    double endTime = getCurrentTime();
    double fullTime = endTime - startTime;
    Logger("=====End detect=====\n");
    Logger("FullDetectTime(%fms)\n", fullTime);

    //cropped to original size
    cv::Mat textBoxImg;

    if (originRect.x > 0 && originRect.y > 0) {
        textBoxPaddingImg(originRect).copyTo(textBoxImg);
    } else {
        textBoxImg = textBoxPaddingImg;
    }

    //Save result.jpg
    if (isOutputResultImg) {
        // Convert result image path to wide string
    std::string resultImgFile = getResultImgFilePath(path, imgName);
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, resultImgFile.c_str(), -1, NULL, 0);
    std::wstring wResultImgFile(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, resultImgFile.c_str(), -1, &wResultImgFile[0], size_needed);
    
    // Save image using wide char API
    cv::imwrite(resultImgFile, textBoxImg);
    }

    if (!layoutStrategy.empty()) {
        applyLayout(textBlocks, layoutStrategy);
        layoutStrategy.clear();
    }

    std::string strRes;
    for (auto &textBlock: textBlocks) {
        strRes.append(textBlock.text);
        strRes.append(textBlock.end);
    }

    if (isOutputResultTxt) {
        // Convert UTF-8 path to wide string
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, resultTxtPathForWrite.c_str(), -1, NULL, 0);
        std::wstring wResultTxtPath(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, resultTxtPathForWrite.c_str(), -1, &wResultTxtPath[0], size_needed);

        // Use _wfopen to handle UTF-16 paths
        FILE* resultTxt = _wfopen(wResultTxtPath.c_str(), L"w");
        if (resultTxt != nullptr) {
            fwprintf(resultTxt, L"%S", strRes.c_str());
            fflush(resultTxt);
            fclose(resultTxt);
        }
    }

    return OcrResult{dbNetTime, textBlocks, textBoxImg, fullTime, strRes};
}