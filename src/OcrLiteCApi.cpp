#ifdef __CLIB__

#include "OcrLiteCApi.h"
#include "OcrLite.h"
#include "TbpuLayout.h"
#include "OcrUtils.h"
#include <opencv2/opencv.hpp>
#include "table/extractor.h"
#include "table/html_gen.h"
#include <cmath>
#include <array>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif

extern "C"
{
typedef struct {
    OcrLite OcrObj;
    std::string strRes;
    OcrResult ocrResult;
    TableDetectResult tableResult;
    std::string tableStrRes;
    int tableMode = 0;  // 0=ONNX(SLANet), 1=img2table-cpp
    std::vector<std::array<int,4>> tableCells;
} OCR_OBJ;

_QM_OCR_API OCR_HANDLE
OcrInit(const char *szDetModel, const char *szClsModel, const char *szRecModel, const char *szKeyPath, int nThreads) {

    OCR_OBJ *pOcrObj = new OCR_OBJ;
    if (pOcrObj) {
        try {
            pOcrObj->OcrObj.setNumThread(nThreads);
            pOcrObj->OcrObj.initModels(szDetModel, szClsModel, szRecModel, szKeyPath);
        } catch (...) {
            delete pOcrObj;
            return nullptr;
        }
        return pOcrObj;
    } else {
        return nullptr;
    }
}

#ifdef __EMBEDDED_MODELS__
_QM_OCR_API OCR_HANDLE
OcrInitEmbedded(int nThreads) {
    OCR_OBJ *pOcrObj = new OCR_OBJ;
    if (pOcrObj) {
        try {
            pOcrObj->OcrObj.setNumThread(nThreads);
            auto f_ocr = std::async(std::launch::async, [&]{
                pOcrObj->OcrObj.initModels();
            });
            f_ocr.get();
            // Table init is best-effort; failure should not block OCR
            try {
                pOcrObj->OcrObj.initTableModel();
            } catch (const std::exception& e) {
                printf("Warning: Table model init failed (OCR still available): %s\n", e.what());
            } catch (...) {
                printf("Warning: Table model init failed (OCR still available)\n");
            }
        } catch (...) {
            delete pOcrObj;
            return nullptr;
        }
        return pOcrObj;
    } else {
        return nullptr;
    }
}
#endif

_QM_OCR_API OCR_BOOL
OcrDetect(OCR_HANDLE handle, const char *imgPath, const char *imgName, OCR_PARAM *pParam) {

    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return FALSE;

    OCR_PARAM Param;
    if (pParam) {
        Param = *pParam;
    } else {
        Param = {50, 1024, 0.6f, 0.3f, 2.0f, 1, 1};
    }
    
    if (Param.padding == 0)
        Param.padding = 50;

    if (Param.maxSideLen == 0)
        Param.maxSideLen = 1024;

    if (Param.boxScoreThresh == 0)
        Param.boxScoreThresh = 0.6;

    if (Param.boxThresh == 0)
        Param.boxThresh = 0.3f;

    if (Param.unClipRatio == 0)
        Param.unClipRatio = 2.0;

    if (Param.doAngle == 0)
        Param.doAngle = 1;

    if (Param.mostAngle == 0)
        Param.mostAngle = 1;

    OcrResult result = pOcrObj->OcrObj.detect(imgPath, imgName, Param.padding, Param.maxSideLen,
                                              Param.boxScoreThresh, Param.boxThresh, Param.unClipRatio,
                                              Param.doAngle != 0, Param.mostAngle != 0);
    if (result.strRes.length() > 0) {
        pOcrObj->strRes = result.strRes;
        return TRUE;
    } else
        return FALSE;
}

_QM_OCR_API OCR_BOOL
OcrDetectMem(OCR_HANDLE handle, const unsigned char *imgData, int imgSize, OCR_PARAM *pParam) {

    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return FALSE;

    if (!imgData || imgSize <= 0)
        return FALSE;

    OCR_PARAM Param = *pParam;
    if (Param.padding == 0)
        Param.padding = 50;

    if (Param.maxSideLen == 0)
        Param.maxSideLen = 1024;

    if (Param.boxScoreThresh == 0)
        Param.boxScoreThresh = 0.6;

    if (Param.boxThresh == 0)
        Param.boxThresh = 0.3f;

    if (Param.unClipRatio == 0)
        Param.unClipRatio = 2.0;

    if (Param.doAngle == 0)
        Param.doAngle = 1;

    if (Param.mostAngle == 0)
        Param.mostAngle = 1;

    std::vector<unsigned char> buffer(imgData, imgData + imgSize);
    cv::Mat img = cv::imdecode(buffer, cv::IMREAD_COLOR);

    if (img.empty())
        return FALSE;

    OcrResult result = pOcrObj->OcrObj.detect(img, Param.padding, Param.maxSideLen,
                                              Param.boxScoreThresh, Param.boxThresh, Param.unClipRatio,
                                              Param.doAngle != 0, Param.mostAngle != 0);
    if (result.strRes.length() > 0) {
        pOcrObj->strRes = result.strRes;
        return TRUE;
    } else
        return FALSE;
}


_QM_OCR_API int OcrGetLen(OCR_HANDLE handle) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0;
    return pOcrObj->strRes.size() + 1;
}

_QM_OCR_API int OcrGetResult(OCR_HANDLE handle, char *szBuf, int nLen) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0;

    int copyLen = (int)pOcrObj->strRes.size();
    if (nLen > copyLen) {
        strncpy(szBuf, pOcrObj->strRes.c_str(), copyLen);
        szBuf[copyLen] = 0;
    }

    return copyLen;
}

_QM_OCR_API int OcrGetResultMem(OCR_HANDLE handle, char **szBuf) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0;

    int len = (int)pOcrObj->strRes.size() + 1;
    *szBuf = (char *)malloc(len);
    if (*szBuf) {
        strncpy(*szBuf, pOcrObj->strRes.c_str(), pOcrObj->strRes.size());
        (*szBuf)[pOcrObj->strRes.size()] = 0;
    }

    return len - 1;
}


_QM_OCR_API void OcrDestroy(OCR_HANDLE handle) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (pOcrObj)
        delete pOcrObj;
}

_QM_OCR_API int
OcrDetectEx(OCR_HANDLE handle, const char *imgPath, const char *imgName, OCR_PARAM *pParam) {

    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0;

    OCR_PARAM Param = *pParam;
    if (Param.padding == 0)
        Param.padding = 50;

    if (Param.maxSideLen == 0)
        Param.maxSideLen = 1024;

    if (Param.boxScoreThresh == 0)
        Param.boxScoreThresh = 0.6;

    if (Param.boxThresh == 0)
        Param.boxThresh = 0.3f;

    if (Param.unClipRatio == 0)
        Param.unClipRatio = 2.0;

    if (Param.doAngle == 0)
        Param.doAngle = 1;

    if (Param.mostAngle == 0)
        Param.mostAngle = 1;

    pOcrObj->ocrResult = pOcrObj->OcrObj.detect(imgPath, imgName, Param.padding, Param.maxSideLen,
                                              Param.boxScoreThresh, Param.boxThresh, Param.unClipRatio,
                                              Param.doAngle != 0, Param.mostAngle != 0);

    std::string allText;
    for (const auto& block : pOcrObj->ocrResult.textBlocks) {
        allText += block.text;
        allText += block.end;
    }
    pOcrObj->strRes = allText;

    return (int)pOcrObj->ocrResult.textBlocks.size();
}

_QM_OCR_API int
OcrDetectMemEx(OCR_HANDLE handle, const unsigned char *imgData, int imgSize, OCR_PARAM *pParam) {

    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0;

    if (!imgData || imgSize <= 0)
        return 0;

    OCR_PARAM Param = *pParam;
    if (Param.padding == 0)
        Param.padding = 50;

    if (Param.maxSideLen == 0)
        Param.maxSideLen = 1024;

    if (Param.boxScoreThresh == 0)
        Param.boxScoreThresh = 0.6;

    if (Param.boxThresh == 0)
        Param.boxThresh = 0.3f;

    if (Param.unClipRatio == 0)
        Param.unClipRatio = 2.0;

    if (Param.doAngle == 0)
        Param.doAngle = 1;

    if (Param.mostAngle == 0)
        Param.mostAngle = 1;

    std::vector<unsigned char> buffer(imgData, imgData + imgSize);
    cv::Mat img = cv::imdecode(buffer, cv::IMREAD_COLOR);

    if (img.empty())
        return 0;

    pOcrObj->ocrResult = pOcrObj->OcrObj.detect(img, Param.padding, Param.maxSideLen,
                                              Param.boxScoreThresh, Param.boxThresh, Param.unClipRatio,
                                              Param.doAngle != 0, Param.mostAngle != 0);

    std::string allText;
    for (const auto& block : pOcrObj->ocrResult.textBlocks) {
        allText += block.text;
        allText += block.end;
    }
    pOcrObj->strRes = allText;

    return (int)pOcrObj->ocrResult.textBlocks.size();
}

_QM_OCR_API int OcrGetBlockCount(OCR_HANDLE handle) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0;
    return (int)pOcrObj->ocrResult.textBlocks.size();
}

_QM_OCR_API int OcrGetBlockText(OCR_HANDLE handle, int index, char *szBuf, int nLen) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj || index < 0 || index >= (int)pOcrObj->ocrResult.textBlocks.size())
        return 0;

    const std::string& text = pOcrObj->ocrResult.textBlocks[index].text;
    int copyLen = (int)text.size();
    if (nLen > copyLen) {
        strncpy(szBuf, text.c_str(), copyLen);
        szBuf[copyLen] = 0;
    }
    return copyLen;
}

_QM_OCR_API float OcrGetBlockScore(OCR_HANDLE handle, int index) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj || index < 0 || index >= (int)pOcrObj->ocrResult.textBlocks.size())
        return 0.0f;
    return pOcrObj->ocrResult.textBlocks[index].boxScore;
}

_QM_OCR_API int OcrGetBlockBox(OCR_HANDLE handle, int index, int *szBuf) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj || index < 0 || index >= (int)pOcrObj->ocrResult.textBlocks.size())
        return 0;

    const auto& boxPoint = pOcrObj->ocrResult.textBlocks[index].boxPoint;
    if (boxPoint.size() != 4)
        return 0;

    szBuf[0] = boxPoint[0].x;
    szBuf[1] = boxPoint[0].y;
    szBuf[2] = boxPoint[1].x;
    szBuf[3] = boxPoint[1].y;
    szBuf[4] = boxPoint[2].x;
    szBuf[5] = boxPoint[2].y;
    szBuf[6] = boxPoint[3].x;
    szBuf[7] = boxPoint[3].y;

    return 8;
}

_QM_OCR_API int OcrGetBlockCharScores(OCR_HANDLE handle, int index, float *szBuf, int nLen) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj || index < 0 || index >= (int)pOcrObj->ocrResult.textBlocks.size())
        return 0;

    const auto& charScores = pOcrObj->ocrResult.textBlocks[index].charScores;
    int count = (int)charScores.size();
    int copyCount = count < nLen ? count : nLen;
    for (int i = 0; i < copyCount; i++) {
        szBuf[i] = charScores[i];
    }
    return copyCount;
}

_QM_OCR_API int OcrGetBlockAngle(OCR_HANDLE handle, int index) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj || index < 0 || index >= (int)pOcrObj->ocrResult.textBlocks.size())
        return -1;
    return pOcrObj->ocrResult.textBlocks[index].angleIndex;
}

_QM_OCR_API float OcrGetBlockAngleScore(OCR_HANDLE handle, int index) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj || index < 0 || index >= (int)pOcrObj->ocrResult.textBlocks.size())
        return 0.0f;
    return pOcrObj->ocrResult.textBlocks[index].angleScore;
}

_QM_OCR_API void OcrSetLayoutStrategy(OCR_HANDLE handle, const char *szStrategy) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return;
    pOcrObj->OcrObj.setLayoutStrategy(szStrategy ? szStrategy : "");
}

_QM_OCR_API int OcrGetLayoutStrategy(OCR_HANDLE handle, char *szBuf, int nLen) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0;
    std::string strategy = pOcrObj->OcrObj.getLayoutStrategy();
    int copyLen = (int)strategy.size();
    if (nLen > copyLen) {
        strncpy(szBuf, strategy.c_str(), copyLen);
        szBuf[copyLen] = 0;
    }
    return copyLen;
}

_QM_OCR_API int OcrGetLayoutStrategyCount() {
    return layoutStrategyCount();
}

_QM_OCR_API int OcrGetLayoutStrategyInfo(int index, char *szKey, int keyLen, char *szLabel, int labelLen, char *szDesc, int descLen) {
    int count = layoutStrategyCount();
    if (index < 0 || index >= count)
        return 0;
    const char* key = layoutStrategyKey(index);
    if (szKey && keyLen > 0) {
        int len = (int)strlen(key);
        if (len >= keyLen) len = keyLen - 1;
        strncpy(szKey, key, len);
        szKey[len] = 0;
    }
    if (szLabel && labelLen > 0) {
        const char* label = "";
        if (index == 0) label = "Multi-Para";
        else if (index == 1) label = "Multi-Line";
        else if (index == 2) label = "Multi-None";
        else if (index == 3) label = "Single-Para";
        else if (index == 4) label = "Single-Line";
        else if (index == 5) label = "Single-None";
        else if (index == 6) label = "Single-Code";
        int len = (int)strlen(label);
        if (len >= labelLen) len = labelLen - 1;
        strncpy(szLabel, label, len);
        szLabel[len] = 0;
    }
    if (szDesc && descLen > 0) {
        const char* desc = "";
        if (index == 0) desc = "Multi-column, merge paragraphs";
        else if (index == 1) desc = "Multi-column, each line";
        else if (index == 2) desc = "Multi-column, continuous";
        else if (index == 3) desc = "Single-column, merge paragraphs";
        else if (index == 4) desc = "Single-column, each line";
        else if (index == 5) desc = "Single-column, continuous";
        else if (index == 6) desc = "Single-column, preserve indent";
        int len = (int)strlen(desc);
        if (len >= descLen) len = descLen - 1;
        strncpy(szDesc, desc, len);
        szDesc[len] = 0;
    }
    return 1;
}

_QM_OCR_API OCR_BOOL
OcrInitTable(const char *szTableModel, const char *szTableDict, int nThreads) {
    OCR_OBJ *pOcrObj = new OCR_OBJ;
    if (pOcrObj) {
        try {
            pOcrObj->OcrObj.setNumThread(nThreads);
            pOcrObj->OcrObj.initTableModel(szTableModel, szTableDict);
        } catch (...) {
            delete pOcrObj;
            return FALSE;
        }
        return TRUE;
    } else {
        return FALSE;
    }
}

#ifdef __EMBEDDED_MODELS__
_QM_OCR_API OCR_HANDLE
OcrInitTableEmbedded(int nThreads) {
    OCR_OBJ *pOcrObj = new OCR_OBJ;
    if (pOcrObj) {
        try {
            pOcrObj->OcrObj.setNumThread(nThreads);
            pOcrObj->OcrObj.initModels();
            try {
                pOcrObj->OcrObj.initTableModel();
            } catch (const std::exception& e) {
                printf("Warning: Table model init failed: %s\n", e.what());
            } catch (...) {
                printf("Warning: Table model init failed\n");
            }
        } catch (...) {
            delete pOcrObj;
            return nullptr;
        }
        return pOcrObj;
    } else {
        return nullptr;
    }
}
#endif

static std::string buildTableHtml(const std::vector<table::Table>& tables,
                                  const std::vector<TextBlock>& ocrBlocks,
                                  std::vector<std::array<int,4>>* outCells = nullptr) {
    std::string html = "<table>";
    for (auto& tbl : tables) {
        for (auto& row : tbl.rows) {
            html += "<tr>";
            size_t i = 0;
            while (i < row.cells.size()) {
                auto& cell = row.cells[i];
                int colspan = 1;
                while (i + colspan < row.cells.size()) {
                    auto& next = row.cells[i + colspan];
                    if (next.x1 == cell.x1 && next.y1 == cell.y1 &&
                        next.x2 == cell.x2 && next.y2 == cell.y2) {
                        colspan++;
                    } else {
                        break;
                    }
                }

                if (outCells) {
                    outCells->push_back({cell.x1, cell.y1, cell.x2, cell.y2});
                }

                std::string cellText;
                float bestIou = 0.0f;
                for (auto& block : ocrBlocks) {
                    float bx0 = (float)block.boxPoint[0].x, by0 = (float)block.boxPoint[0].y;
                    float bx1 = bx0, by1 = by0;
                    for (auto& pt : block.boxPoint) {
                        bx0 = std::min(bx0, (float)pt.x); by0 = std::min(by0, (float)pt.y);
                        bx1 = std::max(bx1, (float)pt.x); by1 = std::max(by1, (float)pt.y);
                    }
                    float cx0 = (float)cell.x1, cy0 = (float)cell.y1;
                    float cx1 = (float)cell.x2, cy1 = (float)cell.y2;
                    float ix0 = std::max(bx0, cx0), iy0 = std::max(by0, cy0);
                    float ix1 = std::min(bx1, cx1), iy1 = std::min(by1, cy1);
                    float inter = std::max(0.0f, ix1 - ix0) * std::max(0.0f, iy1 - iy0);
                    float iou = inter / ((bx1-bx0)*(by1-by0) + (cx1-cx0)*(cy1-cy0) - inter + 1e-8f);
                    if (iou > bestIou && iou > 0.1f) { bestIou = iou; cellText = block.text; }
                }

                html += "<td";
                if (colspan > 1) html += " colspan='" + std::to_string(colspan) + "'";
                int cellW = cell.x2 - cell.x1;
                int cellH = cell.y2 - cell.y1;
                html += " style='width:" + std::to_string(cellW) + "px;height:" + std::to_string(cellH) + "px'";
                html += ">" + cellText + "</td>";
                i += colspan;
            }
            html += "</tr>";
        }
    }
    html += "</table>";
    return html;
}

static std::string wrapFullHtml(const std::string& tableHtml) {
    std::string result = "<html>\n<head><meta charset=\"UTF-8\">\n"
        "<style>table{border-collapse:collapse;table-layout:fixed}\n"
        "td,th{border:1px solid black;padding:8px;text-align:left}</style>\n"
        "</head>\n<body>\n";
    // Remove <tbody> and </tbody> tags
    std::string clean = tableHtml;
    size_t pos;
    while ((pos = clean.find("<tbody>")) != std::string::npos)
        clean.erase(pos, 7);
    while ((pos = clean.find("</tbody>")) != std::string::npos)
        clean.erase(pos, 8);

    // Move height from <td> to <tr> for Excel compatibility
    // Excel ignores height on <td>, only respects height on <tr>
    {
        size_t trPos = 0;
        while ((trPos = clean.find("<tr>", trPos)) != std::string::npos) {
            size_t trEnd = clean.find("</tr>", trPos);
            if (trEnd == std::string::npos) break;
            // Find first <td> with height in this row
            size_t tdPos = clean.find("<td", trPos + 4);
            if (tdPos != std::string::npos && tdPos < trEnd) {
                size_t hPos = clean.find("height:", tdPos);
                if (hPos != std::string::npos && hPos < trEnd) {
                    // Extract height value
                    size_t hStart = hPos + 7;
                    int h = 0;
                    while (hStart < clean.size() && clean[hStart] >= '0' && clean[hStart] <= '9') {
                        h = h * 10 + (clean[hStart] - '0');
                        hStart++;
                    }
                    if (h > 0) {
                        // Add height to <tr>
                        clean.insert(trPos + 3, " style='height:" + std::to_string(h) + "px'");
                        // Remove height from <td> style: "height:XXXpx;" or "height:XXXpx"
                        size_t tdStyleStart = clean.find("style='", tdPos);
                        if (tdStyleStart != std::string::npos && tdStyleStart < trEnd) {
                            size_t hErase = clean.find("height:", tdStyleStart);
                            if (hErase != std::string::npos && hErase < trEnd) {
                                size_t hEnd = hErase;
                                while (hEnd < clean.size() && clean[hEnd] != ';' && clean[hEnd] != '\'') hEnd++;
                                if (hEnd < clean.size() && clean[hEnd] == ';') hEnd++; // skip semicolon
                                clean.erase(hErase, hEnd - hErase);
                            }
                        }
                    }
                }
            }
            trPos = trEnd + 5;
        }
    }

    // Calculate total table width from first row's <td> widths
    int totalW = 0;
    pos = clean.find("<tr>");
    if (pos != std::string::npos) {
        size_t trEnd = clean.find("</tr>", pos);
        std::string firstRow = clean.substr(pos, trEnd - pos);
        size_t tdPos = 0;
        while ((tdPos = firstRow.find("width:", tdPos)) != std::string::npos) {
            tdPos += 6;
            int w = 0;
            while (tdPos < firstRow.size() && firstRow[tdPos] >= '0' && firstRow[tdPos] <= '9') {
                w = w * 10 + (firstRow[tdPos] - '0');
                tdPos++;
            }
            totalW += w;
        }
    }
    if (totalW > 0) {
        // Inject width into <table> tag
        pos = clean.find("<table>");
        if (pos != std::string::npos) {
            clean.insert(pos + 6, " style='width:" + std::to_string(totalW) + "px'");
        }
    }

    result += clean;
    result += "\n</body>\n</html>";
    return result;
}

_QM_OCR_API void
OcrSetTableMode(OCR_HANDLE handle, int mode) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return;
    pOcrObj->tableMode = mode;
}

_QM_OCR_API OCR_BOOL
OcrDetectTable(OCR_HANDLE handle, const char *imgPath, const char *imgName, OCR_PARAM *pParam) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return FALSE;

    OCR_PARAM Param;
    if (pParam) {
        Param = *pParam;
    } else {
        Param = {50, 1024, 0.6f, 0.3f, 2.0f, 1, 1};
    }

    if (Param.padding == 0) Param.padding = 50;
    if (Param.maxSideLen == 0) Param.maxSideLen = 1024;
    if (Param.boxScoreThresh == 0) Param.boxScoreThresh = 0.6f;
    if (Param.boxThresh == 0) Param.boxThresh = 0.3f;
    if (Param.unClipRatio == 0) Param.unClipRatio = 2.0f;
    if (Param.doAngle == 0) Param.doAngle = 1;
    if (Param.mostAngle == 0) Param.mostAngle = 1;

    // Read image
    std::string imgFile = getSrcImgFilePath(imgPath, imgName);
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, imgFile.c_str(), -1, NULL, 0);
    std::wstring wimgFile(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, imgFile.c_str(), -1, &wimgFile[0], size_needed);

    HANDLE hFile = CreateFileW(wimgFile.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return FALSE;
    }

    std::vector<BYTE> buffer(fileSize);
    DWORD bytesRead;
    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
        CloseHandle(hFile);
        return FALSE;
    }
    CloseHandle(hFile);

    cv::Mat img = cv::imdecode(buffer, cv::IMREAD_COLOR);
    if (img.empty())
        return FALSE;

    if (pOcrObj->tableMode == 1) {
        // img2table 纯 OpenCV 模式
        pOcrObj->tableCells.clear();
        auto tblResult = table::extractBorderedTables(img);
        OcrResult ocrResult = pOcrObj->OcrObj.detect(imgPath, imgName, Param.padding, Param.maxSideLen,
                                                     Param.boxScoreThresh, Param.boxThresh, Param.unClipRatio,
                                                     Param.doAngle != 0, Param.mostAngle != 0);
        std::string html = buildTableHtml(tblResult.tables, ocrResult.textBlocks, &pOcrObj->tableCells);
        pOcrObj->tableResult.htmlStructure = html;
        pOcrObj->tableResult.structureScore = 0.0f;
        pOcrObj->tableResult.ocrText = ocrResult.strRes;
        pOcrObj->tableStrRes = wrapFullHtml(html);
    } else {
        // ONNX mode (tableMode=1 或默认)
        pOcrObj->tableResult = pOcrObj->OcrObj.detectTable(img, Param.padding, Param.maxSideLen,
                                                           Param.boxScoreThresh, Param.boxThresh, Param.unClipRatio,
                                                           Param.doAngle != 0, Param.mostAngle != 0);
        pOcrObj->tableCells.clear();
        for (size_t bi = 0; bi < pOcrObj->tableResult.cellBoxes.size(); bi++) {
            const std::vector<int>& box = pOcrObj->tableResult.cellBoxes[bi];
            if (box.size() >= 8) {
                int xs[4] = {box[0], box[2], box[4], box[6]};
                int ys[4] = {box[1], box[3], box[5], box[7]};
                pOcrObj->tableCells.push_back({
                    *std::min_element(xs, xs+4), *std::min_element(ys, ys+4),
                    *std::max_element(xs, xs+4), *std::max_element(ys, ys+4)
                });
            }
        }
        // Inject width/height into each <td> tag
        std::string html = pOcrObj->tableResult.htmlStructure;
        size_t tdIdx = 0;
        size_t pos = 0;
        while (true) {
            pos = html.find("<td", pos);
            if (pos == std::string::npos) break;
            int w = 80, h = 30;
            if (tdIdx < pOcrObj->tableCells.size()) {
                auto& c = pOcrObj->tableCells[tdIdx];
                w = c[2] - c[0];
                h = c[3] - c[1];
            }
            std::string style = " style='width:" + std::to_string(w) + "px;height:" + std::to_string(h) + "px'";
            size_t tagEnd = html.find(">", pos);
            if (tagEnd == std::string::npos) break;
            html.insert(tagEnd, style);
            pos = tagEnd + style.size() + 1;
            tdIdx++;
        }
        pOcrObj->tableResult.htmlStructure = html;
        pOcrObj->tableStrRes = wrapFullHtml(html);
    }
    return TRUE;
}

_QM_OCR_API OCR_BOOL
OcrDetectTableMem(OCR_HANDLE handle, const unsigned char *imgData, int imgSize, OCR_PARAM *pParam) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return FALSE;

    if (!imgData || imgSize <= 0)
        return FALSE;

    OCR_PARAM Param;
    if (pParam) {
        Param = *pParam;
    } else {
        Param = {50, 1024, 0.6f, 0.3f, 2.0f, 1, 1};
    }

    if (Param.padding == 0) Param.padding = 50;
    if (Param.maxSideLen == 0) Param.maxSideLen = 1024;
    if (Param.boxScoreThresh == 0) Param.boxScoreThresh = 0.6f;
    if (Param.boxThresh == 0) Param.boxThresh = 0.3f;
    if (Param.unClipRatio == 0) Param.unClipRatio = 2.0f;
    if (Param.doAngle == 0) Param.doAngle = 1;
    if (Param.mostAngle == 0) Param.mostAngle = 1;

    std::vector<unsigned char> buffer(imgData, imgData + imgSize);
    cv::Mat img = cv::imdecode(buffer, cv::IMREAD_COLOR);
    if (img.empty())
        return FALSE;

    if (pOcrObj->tableMode == 1) {
        // img2table 纯 OpenCV 模式
        pOcrObj->tableCells.clear();
        auto tblResult = table::extractBorderedTables(img);
        OcrResult ocrResult = pOcrObj->OcrObj.detect(img, Param.padding, Param.maxSideLen,
                                                     Param.boxScoreThresh, Param.boxThresh, Param.unClipRatio,
                                                     Param.doAngle != 0, Param.mostAngle != 0);
        std::string html = buildTableHtml(tblResult.tables, ocrResult.textBlocks, &pOcrObj->tableCells);
        pOcrObj->tableResult.htmlStructure = html;
        pOcrObj->tableResult.structureScore = 0.0f;
        pOcrObj->tableResult.ocrText = ocrResult.strRes;
        pOcrObj->tableStrRes = wrapFullHtml(html);
    } else {
        // ONNX mode (tableMode=0 SLANet 或默认)
        pOcrObj->tableResult = pOcrObj->OcrObj.detectTable(img, Param.padding, Param.maxSideLen,
                                                           Param.boxScoreThresh, Param.boxThresh, Param.unClipRatio,
                                                           Param.doAngle != 0, Param.mostAngle != 0);
        pOcrObj->tableCells.clear();
        for (size_t bi = 0; bi < pOcrObj->tableResult.cellBoxes.size(); bi++) {
            const std::vector<int>& box = pOcrObj->tableResult.cellBoxes[bi];
            if (box.size() >= 8) {
                int xs[4] = {box[0], box[2], box[4], box[6]};
                int ys[4] = {box[1], box[3], box[5], box[7]};
                pOcrObj->tableCells.push_back({
                    *std::min_element(xs, xs+4), *std::min_element(ys, ys+4),
                    *std::max_element(xs, xs+4), *std::max_element(ys, ys+4)
                });
            }
        }
        // Inject width/height into each <td> tag
        std::string html = pOcrObj->tableResult.htmlStructure;
        size_t tdIdx = 0;
        size_t pos = 0;
        while (true) {
            pos = html.find("<td", pos);
            if (pos == std::string::npos) break;
            int w = 80, h = 30;
            if (tdIdx < pOcrObj->tableCells.size()) {
                auto& c = pOcrObj->tableCells[tdIdx];
                w = c[2] - c[0];
                h = c[3] - c[1];
            }
            std::string style = " style='width:" + std::to_string(w) + "px;height:" + std::to_string(h) + "px'";
            size_t tagEnd = html.find(">", pos);
            if (tagEnd == std::string::npos) break;
            html.insert(tagEnd, style);
            pos = tagEnd + style.size() + 1;
            tdIdx++;
        }
        pOcrObj->tableResult.htmlStructure = html;
        pOcrObj->tableStrRes = wrapFullHtml(html);
    }
    return TRUE;
}

_QM_OCR_API int OcrGetTableLen(OCR_HANDLE handle) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0;
    return (int)pOcrObj->tableStrRes.size() + 1;
}

_QM_OCR_API int OcrGetTableResult(OCR_HANDLE handle, char *szBuf, int nLen) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0;

    int copyLen = (int)pOcrObj->tableStrRes.size();
    if (nLen > copyLen) {
        strncpy(szBuf, pOcrObj->tableStrRes.c_str(), copyLen);
        szBuf[copyLen] = 0;
    }
    return copyLen;
}

_QM_OCR_API int OcrGetTableResultMem(OCR_HANDLE handle, char **szBuf) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0;

    int len = (int)pOcrObj->tableStrRes.size() + 1;
    *szBuf = (char *)malloc(len);
    if (*szBuf) {
        strncpy(*szBuf, pOcrObj->tableStrRes.c_str(), pOcrObj->tableStrRes.size());
        (*szBuf)[pOcrObj->tableStrRes.size()] = 0;
    }
    return len - 1;
}

_QM_OCR_API float OcrGetTableStructureScore(OCR_HANDLE handle) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0.0f;
    return pOcrObj->tableResult.structureScore;
}

_QM_OCR_API int OcrGetTableOcrText(OCR_HANDLE handle, char *szBuf, int nLen) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0;

    int copyLen = (int)pOcrObj->tableResult.ocrText.size();
    if (nLen > copyLen) {
        strncpy(szBuf, pOcrObj->tableResult.ocrText.c_str(), copyLen);
        szBuf[copyLen] = 0;
    }
    return copyLen;
}

_QM_OCR_API int OcrGetTableCellCount(OCR_HANDLE handle) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj)
        return 0;
    return (int)pOcrObj->tableCells.size();
}

_QM_OCR_API int OcrGetTableCell(OCR_HANDLE handle, int index, int *x1, int *y1, int *x2, int *y2) {
    OCR_OBJ *pOcrObj = (OCR_OBJ *) handle;
    if (!pOcrObj || index < 0 || index >= (int)pOcrObj->tableCells.size())
        return 0;
    auto& c = pOcrObj->tableCells[index];
    *x1 = c[0]; *y1 = c[1]; *x2 = c[2]; *y2 = c[3];
    return 1;
}

};
#endif

