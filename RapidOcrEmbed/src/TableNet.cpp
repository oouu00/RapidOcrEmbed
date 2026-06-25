#include "TableNet.h"
#include "OcrUtils.h"
#include <algorithm>
#include <numeric>
#include <fstream>
#include <sstream>
#include <cmath>

TableNet::~TableNet() {
    delete session;
    inputNamesPtr.clear();
    outputNamesPtr.clear();
}

void TableNet::setNumThread(int numOfThread) {
    numThread = numOfThread;
    sessionOptions.SetInterOpNumThreads(numThread);
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
}

void TableNet::setGpuIndex(int gpuIndex) {
#ifdef __CUDA__
    if (gpuIndex >= 0) {
        OrtCUDAProviderOptions cuda_options;
        cuda_options.device_id = gpuIndex;
        cuda_options.arena_extend_strategy = 0;
        cuda_options.gpu_mem_limit = 2 * 1024 * 1024 * 1024;
        cuda_options.cudnn_conv_algo_search = OrtCudnnConvAlgoSearch::OrtCudnnConvAlgoSearchExhaustive;
        cuda_options.do_copy_in_default_stream = 1;
        sessionOptions.AppendExecutionProvider_CUDA(cuda_options);
    }
#endif
}

void TableNet::initModel(const std::string &pathStr) {
#ifdef _WIN32
    std::wstring modelPath = strToWstr(pathStr);
    session = new Ort::Session(env, modelPath.c_str(), sessionOptions);
#else
    session = new Ort::Session(env, pathStr.c_str(), sessionOptions);
#endif
    inputNamesPtr = getInputNames(session);
    outputNamesPtr = getOutputNames(session);
}

#ifdef __EMBEDDED_MODELS__
void TableNet::initModel(const unsigned char* modelData, size_t modelSize) {
    // 从嵌入内存加载 ONNX 模型
    // ⚠️ 依赖: OnnxRuntimeConfig.cmake 中必须排除 flatbuffers.lib
    //   否则 MNN 的 flatbuffers 会覆盖 ORT 的, 导致模型验证失败
    void* modelCopy = malloc(modelSize);
    if (!modelCopy) {
        throw std::runtime_error("Failed to allocate memory for table model");
    }
    memcpy(modelCopy, modelData, modelSize);
    try {
        session = new Ort::Session(env, modelCopy, modelSize, sessionOptions);
    } catch (const std::exception& e) {
        free(modelCopy);
        throw;
    }
    inputNamesPtr = getInputNames(session);
    outputNamesPtr = getOutputNames(session);
}
#endif

void TableNet::loadDict(const std::string &dictPath) {
    charDict.clear();
    std::ifstream file(dictPath);
    if (!file.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        // Strip \r from Windows line endings
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            charDict.push_back(line);
        }
    }
    file.close();
    fprintf(stderr, "[TableNet] loadDict: %d entries loaded\n", (int)charDict.size());
    for (int i = 0; i < (int)charDict.size(); i++) {
        fprintf(stderr, "  [%d]='%s' (len=%d)\n", i, charDict[i].c_str(), (int)charDict[i].size());
    }
}

#ifdef __EMBEDDED_MODELS__
void TableNet::loadDict(const unsigned char* dictData, size_t dictSize) {
    charDict.clear();
    std::string dictStr(reinterpret_cast<const char*>(dictData), dictSize);
    std::istringstream iss(dictStr);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            charDict.push_back(line);
        }
    }
}
#endif

TableResult TableNet::decodeOutput(std::vector<Ort::Value> &outputs,
                                   int origWidth, int origHeight,
                                   int resizedWidth, int resizedHeight) {
    TableResult result;
    result.structureScore = 0.0f;

    if (outputs.empty()) {
        return result;
    }

    float* sData = nullptr;
    float* bData = nullptr;
    int seqLen = 0;
    int numCls = 0;

    if (outputs.size() == 1) {
        auto sShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        seqLen = (int)sShape[1];
        numCls = (int)sShape[2];
        sData = outputs[0].GetTensorMutableData<float>();
    } else {
        auto bShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        auto sShape = outputs[1].GetTensorTypeAndShapeInfo().GetShape();
        seqLen = (int)sShape[1];
        numCls = (int)sShape[2];
        bData = outputs[0].GetTensorMutableData<float>();
        sData = outputs[1].GetTensorMutableData<float>();
    }

    std::vector<std::string> fullDict = charDict;
    fullDict.insert(fullDict.begin(), "sos");
    fullDict.push_back("eos");
    int sosIdx = 0;
    int eosIdx = (int)fullDict.size() - 1;

    fprintf(stderr, "[TableNet] dict size=%d, seqLen=%d, numCls=%d\n", (int)fullDict.size(), seqLen, numCls);
    fprintf(stderr, "[TableNet] fullDict: ");
    for (int i = 0; i < (int)fullDict.size(); i++) {
        fprintf(stderr, "[%d]=%s ", i, fullDict[i].c_str());
    }
    fprintf(stderr, "\n");

    std::vector<std::string> structureList;
    std::vector<float> scoreList;

    for (int i = 0; i < seqLen; i++) {
        int maxIdx = 0;
        float maxVal = sData[i * numCls];
        for (int j = 1; j < numCls; j++) {
            float val = sData[i * numCls + j];
            if (val > maxVal) {
                maxVal = val;
                maxIdx = j;
            }
        }
        if (i > 0 && maxIdx == eosIdx) break;
        if (maxIdx == sosIdx || maxIdx == eosIdx) continue;
        if (maxIdx < (int)fullDict.size()) {
            structureList.push_back(fullDict[maxIdx]);
            scoreList.push_back(maxVal);
        }
    }

    fprintf(stderr, "[TableNet] decoded %d structure tokens:\n", (int)structureList.size());
    for (int i = 0; i < (int)structureList.size(); i++) {
        fprintf(stderr, "  [%d] %s (score=%.4f)\n", i, structureList[i].c_str(), scoreList[i]);
    }

    if (!scoreList.empty()) {
        result.structureScore = std::accumulate(scoreList.begin(), scoreList.end(), 0.0f) / scoreList.size();
    }

    // Reassemble tokens into uniform HTML with <td></td> placeholders for ALL cells.
    // Colspan/rowspan attributes are stored in TableCell struct and will be applied
    // in fillCellTexts() when replacing each placeholder.
    std::string html = "<table>";
    for (size_t i = 0; i < structureList.size(); i++) {
        std::string t = structureList[i];
        t.erase(std::remove_if(t.begin(), t.end(), ::isspace), t.end());

        if (t == "<td" || t == "<td>") {
            // Every cell gets a uniform <td></td> placeholder
            html += "<td></td>";
            // Consume attribute tokens until ">" is found
            if (t == "<td") {
                size_t j = i + 1;
                while (j < structureList.size()) {
                    std::string nxt = structureList[j];
                    nxt.erase(std::remove_if(nxt.begin(), nxt.end(), ::isspace), nxt.end());
                    if (nxt == ">") { j++; break; }
                    j++;
                }
                i = j - 1;
            }
        } else if (t == "<td></td>") {
            // Empty cell token - also a uniform placeholder
            html += "<td></td>";
        } else if (t == ">" || t == " " || t == "</td>" ||
                   t.find("colspan=\"") != std::string::npos ||
                   t.find("rowspan=\"") != std::string::npos) {
            // Skip orphaned tokens (now consumed in the <td branch)
        } else {
            html += t;
        }
    }
    html += "</table>";

    fprintf(stderr, "[TableNet] assembled HTML (%d chars):\n%s\n", (int)html.size(), html.c_str());

    result.htmlStructure = html;

    fprintf(stderr, "[TableNet] outputs.size()=%d, bData=%p, sData=%p\n", (int)outputs.size(), (void*)bData, (void*)sData);

    if (bData != nullptr) {
        std::string tdToken = "<td>";
        std::string tdSpanToken = "<td";
        std::string tdEmptyToken = "<td></td>";

        fprintf(stderr, "[TableNet] bData is valid, scanning %d tokens for bbox\n", seqLen);

        for (int i = 0; i < seqLen; i++) {
            int maxIdx = 0;
            float maxVal = sData[i * numCls];
            for (int j = 1; j < numCls; j++) {
                float val = sData[i * numCls + j];
                if (val > maxVal) {
                    maxVal = val;
                    maxIdx = j;
                }
            }
        if (maxIdx < (int)fullDict.size()) {
            std::string token = fullDict[maxIdx];
            if (i < 5 || token.find("td") != std::string::npos) {
                fprintf(stderr, "  bbox_scan[%d] idx=%d token='%s' cmp_td=%d cmp_span=%d cmp_empty=%d\n",
                    i, maxIdx, token.c_str(),
                    (int)(token == tdToken), (int)(token == tdSpanToken), (int)(token == tdEmptyToken));
            }
            if (token == tdToken || token == tdSpanToken || token == tdEmptyToken) {
                    std::vector<int> bbox(8);
                    int maxDim = std::max(origWidth, origHeight);
                    for (int k = 0; k < 8; k++) {
                        bbox[k] = (int)(bData[i * 8 + k] * maxDim);
                    }
                    result.cellBoxes.push_back(bbox);

                    TableCell cell;
                    cell.boxPoint = {cv::Point(bbox[0], bbox[1]), cv::Point(bbox[2], bbox[3]), cv::Point(bbox[4], bbox[5]), cv::Point(bbox[6], bbox[7])};
                    cell.colspan = 1;
                    cell.rowspan = 1;

                    // Only <td (without closing >) has attributes like colspan/rowspan.
                    // <td> and <td></td> are self-contained tokens — scanning their
                    // following tokens would pick up attributes belonging to the NEXT cell.
                    if (token == tdSpanToken) {
                        for (int j = i + 1; j < seqLen && j < i + 4; j++) {
                            int nextIdx = 0;
                            float nextVal = sData[j * numCls];
                            for (int k = 1; k < numCls; k++) {
                                float val = sData[j * numCls + k];
                                if (val > nextVal) { nextVal = val; nextIdx = k; }
                            }
                            if (nextIdx < (int)fullDict.size()) {
                                std::string nextToken = fullDict[nextIdx];
                                size_t pos = nextToken.find("colspan=\"");
                                if (pos != std::string::npos) {
                                    size_t end = nextToken.find("\"", pos + 9);
                                    if (end != std::string::npos) cell.colspan = std::stoi(nextToken.substr(pos + 9, end - pos - 9));
                                }
                                pos = nextToken.find("rowspan=\"");
                                if (pos != std::string::npos) {
                                    size_t end = nextToken.find("\"", pos + 9);
                                    if (end != std::string::npos) cell.rowspan = std::stoi(nextToken.substr(pos + 9, end - pos - 9));
                                }
                            }
                        }
                    }
                    result.cells.push_back(cell);
                }
            }
        }
    }

    return result;
}

TableResult TableNet::recognize(const cv::Mat &src, int pad, int maxSideLen) {
    int srcWidth = src.cols;
    int srcHeight = src.rows;

    int targetSize = 488;
    float scale = (float)targetSize / std::max(srcWidth, srcHeight);
    int dstWidth = (int)(srcWidth * scale);
    int dstHeight = (int)(srcHeight * scale);

    cv::Mat srcResize;
    cv::resize(src, srcResize, cv::Size(dstWidth, dstHeight));

    cv::Mat rgb;
    cv::cvtColor(srcResize, rgb, cv::COLOR_BGR2RGB);

    int paddedWidth = dstWidth;
    int paddedHeight = dstHeight;

    std::vector<float> inputTensorValues(3 * paddedHeight * paddedWidth);
    for (int c = 0; c < 3; c++) {
        for (int h = 0; h < paddedHeight; h++) {
            for (int w = 0; w < paddedWidth; w++) {
                float val = (float)rgb.at<cv::Vec3b>(h, w)[c];
                inputTensorValues[c * paddedHeight * paddedWidth + h * paddedWidth + w] =
                    (val / 255.0f - meanValues[c] / 255.0f) * normValues[c] * 255.0f;
            }
        }
    }

    int targetPad = 488;
    std::vector<float> paddedValues(3 * targetPad * targetPad, 0.0f);
    int copyWidth = std::min(paddedWidth, targetPad);
    int copyHeight = std::min(paddedHeight, targetPad);
    for (int c = 0; c < 3; c++) {
        for (int h = 0; h < copyHeight; h++) {
            for (int w = 0; w < copyWidth; w++) {
                paddedValues[c * targetPad * targetPad + h * targetPad + w] =
                    inputTensorValues[c * paddedHeight * paddedWidth + h * paddedWidth + w];
            }
        }
    }

    std::array<int64_t, 4> inputShape{1, 3, targetPad, targetPad};
    auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(memoryInfo, paddedValues.data(), paddedValues.size(), inputShape.data(), inputShape.size());

    std::vector<const char*> inputNames;
    for (auto& ptr : inputNamesPtr) inputNames.push_back(ptr.get());

    std::vector<const char*> outputNames;
    for (auto& ptr : outputNamesPtr) outputNames.push_back(ptr.get());

    auto outputs = session->Run(Ort::RunOptions{nullptr}, inputNames.data(), &inputTensor, inputNames.size(), outputNames.data(), outputNames.size());

    return decodeOutput(outputs, srcWidth, srcHeight, paddedWidth, paddedHeight);
}

static float calcIoU(const float a[4], const float b[4]) {
    float interL = std::max(a[0], b[0]);
    float interR = std::min(a[2], b[2]);
    float interT = std::max(a[1], b[1]);
    float interB = std::min(a[3], b[3]);
    if (interR <= interL || interB <= interT) return 0.0f;
    float interArea = (interR - interL) * (interB - interT);
    float aArea = (a[2] - a[0]) * (a[3] - a[1]);
    float bArea = (b[2] - b[0]) * (b[3] - b[1]);
    return interArea / (aArea + bArea - interArea + 1e-8f);
}

// ----------------------------------------------------------------------------
// Portions of the following matching logic are derived from MinerU / PaddleOCR,
// which is licensed under the Apache License, Version 2.0.
//
// Original source (MinerU 3.4.0):
//   mineru/model/table/rec/slanet_plus/matcher.py
//   mineru/model/table/rec/slanet_plus/matcher_utils.py
// Copyright (c) Opendatalab. All rights reserved.
// Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserve.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------------------------------------------------------

// Auto-complete: ensures the table HTML is rectangular by padding rows
// that have fewer columns than the maximum column count across all rows.
// Each row's column count is computed by summing colspan values of its cells,
// respecting rowspan from previous rows (which pre-occupy columns).
// Missing cells are appended as empty <td></td> tags.
static std::string autoCompleteTableHtml(const std::string& html) {
    std::string result = html;
    struct RowInfo {
        size_t trEndPos;   // position of </tr> in result string
        int colCount;      // total columns in this row (sum of colspan)
        int rowspanOffset; // columns pre-occupied by rowspan from previous rows
    };
    std::vector<RowInfo> rowInfo;

    // First pass: find all <tr>...</tr> rows and count columns per row
    size_t pos = 0;
    // Track rowspan: each entry is (cols_occupied, rows_remaining)
    std::vector<std::pair<int, int>> activeRowspans;

    while (true) {
        size_t trStart = result.find("<tr", pos);
        if (trStart == std::string::npos) break;

        size_t trTagEnd = result.find(">", trStart);
        if (trTagEnd == std::string::npos) break;

        size_t trEnd = result.find("</tr>", trTagEnd);
        if (trEnd == std::string::npos) break;

        // Decrement active rowspan counters; remove expired ones
        int currentRowspanOffset = 0;
        std::vector<std::pair<int, int>> nextActive;
        for (auto& rs : activeRowspans) {
            rs.second--;
            if (rs.second > 0) {
                nextActive.push_back(rs);
                currentRowspanOffset += rs.first;
            }
        }
        activeRowspans = nextActive;

        // Count columns in this row by parsing <td ...> cells
        int colCount = 0;
        size_t tdPos = trTagEnd + 1;
        while (true) {
            size_t tdStart = result.find("<td", tdPos);
            if (tdStart == std::string::npos || tdStart >= trEnd) break;

            size_t attrEnd = result.find(">", tdStart);
            if (attrEnd == std::string::npos || attrEnd >= trEnd) break;

            // Extract colspan and rowspan from the opening <td ...> tag
            int colspan = 1;
            int rowspan = 1;
            std::string tdTag = result.substr(tdStart, attrEnd - tdStart);

            // Parse colspan="N" or colspan='N'
            size_t csPos = tdTag.find("colspan=");
            if (csPos != std::string::npos) {
                size_t q1 = tdTag.find("'", csPos + 8);
                size_t q2 = tdTag.find("\"", csPos + 8);
                size_t q = (q1 != std::string::npos) ? q1 : q2;
                if (q != std::string::npos) {
                    char quoteChar = tdTag[q];
                    size_t qEnd = tdTag.find(quoteChar, q + 1);
                    if (qEnd != std::string::npos) {
                        colspan = std::stoi(tdTag.substr(q + 1, qEnd - q - 1));
                    }
                }
            }

            // Parse rowspan="N" or rowspan='N'
            size_t rsPos = tdTag.find("rowspan=");
            if (rsPos != std::string::npos) {
                size_t q1 = tdTag.find("'", rsPos + 8);
                size_t q2 = tdTag.find("\"", rsPos + 8);
                size_t q = (q1 != std::string::npos) ? q1 : q2;
                if (q != std::string::npos) {
                    char quoteChar = tdTag[q];
                    size_t qEnd = tdTag.find(quoteChar, q + 1);
                    if (qEnd != std::string::npos) {
                        rowspan = std::stoi(tdTag.substr(q + 1, qEnd - q - 1));
                    }
                }
            }

            colCount += colspan;

            // Track rowspan: this cell occupies colspan columns for rowspan rows
            if (rowspan > 1) {
                activeRowspans.push_back({colspan, rowspan});
            }

            // Move past </td>
            size_t tdClose = result.find("</td>", tdStart);
            if (tdClose == std::string::npos || tdClose >= trEnd) break;
            tdPos = tdClose + 5;
        }

        rowInfo.push_back({trEnd, colCount, currentRowspanOffset});
        pos = trEnd + 5;
    }

    // Find maximum "effective" column count across all rows
    // effective = rowspanOffset (pre-occupied by previous row's rowspan) + colCount
    int maxEffCols = 0;
    for (const auto& ri : rowInfo) {
        int eff = ri.colCount + ri.rowspanOffset;
        if (eff > maxEffCols) maxEffCols = eff;
    }

    if (maxEffCols == 0) return result;

    // Second pass (reverse order to preserve positions): pad rows with missing cells
    for (int i = (int)rowInfo.size() - 1; i >= 0; i--) {
        int eff = rowInfo[i].colCount + rowInfo[i].rowspanOffset;
        if (eff < maxEffCols) {
            int padCount = maxEffCols - eff;
            std::string padStr;
            for (int j = 0; j < padCount; j++) {
                padStr += "<td>&nbsp;</td>"; // &nbsp; ensures border renders in Excel
            }
            result.insert(rowInfo[i].trEndPos, padStr);
            fprintf(stderr, "[TableNet] auto-complete Row[%d]: padded %d cells (%d -> %d)\n",
                    i, padCount, eff, maxEffCols);
        }
    }

    return result;
}

// MinerU-style distance metric (ported from MinerU matcher_utils.py::distance()):
// sum of all 4 corner differences + min(top-left diff sum, bottom-right diff sum)
static float calcMineruDist(const float a[4], const float b[4]) {
    float dx1 = std::abs(a[0] - b[0]);
    float dy1 = std::abs(a[1] - b[1]);
    float dx2 = std::abs(a[2] - b[2]);
    float dy2 = std::abs(a[3] - b[3]);
    float dis  = dx1 + dy1 + dx2 + dy2;
    float disTL = dx1 + dy1;  // top-left corner diff
    float disBR = dx2 + dy2;  // bottom-right corner diff
    return dis + std::min(disTL, disBR);
}

// Adds row heights as percentages to the table HTML.
// SLANet cell bounding boxes don't form a clean grid (unlike img2table),
// so we only compute row heights from y-coordinates. Column widths use
// uniform distribution based on the logical column count from HTML structure.
static std::string addTableSizes(const std::string& html,
                                 const std::vector<TableCell>& cells) {
    if (cells.empty()) return html;

    // Compute bounding box [x0,y0,x1,y1] for each cell from 8-point boxPoint
    struct CellBox { float x0, y0, x1, y1; };
    std::vector<CellBox> boxes(cells.size());
    for (size_t i = 0; i < cells.size(); i++) {
        const auto& pts = cells[i].boxPoint;
        float xs[4], ys[4];
        for (int k = 0; k < 4 && k < (int)pts.size(); k++) {
            xs[k] = (float)pts[k].x;
            ys[k] = (float)pts[k].y;
        }
        boxes[i].x0 = *std::min_element(xs, xs + 4);
        boxes[i].y0 = *std::min_element(ys, ys + 4);
        boxes[i].x1 = *std::max_element(xs, xs + 4);
        boxes[i].y1 = *std::max_element(ys, ys + 4);
    }

    // Parse HTML to find <tr> rows
    struct RowInfo {
        size_t trStart;
        int tdCount; // number of <td> in this row
    };
    std::vector<RowInfo> rows;
    std::string lower = html;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    size_t pos = 0;
    while (pos < html.size()) {
        size_t trS = lower.find("<tr", pos);
        if (trS == std::string::npos) break;
        size_t trTagEnd = html.find('>', trS);
        if (trTagEnd == std::string::npos) break;
        size_t trE = lower.find("</tr>", trTagEnd);
        if (trE == std::string::npos) break;

        // Count <td in this row
        int tdCount = 0;
        size_t tdPos = trTagEnd + 1;
        while (tdPos < trE) {
            size_t tdS = lower.find("<td", tdPos);
            if (tdS == std::string::npos || tdS >= trE) break;
            tdCount++;
            size_t tdClose = lower.find("</td>", tdS);
            if (tdClose == std::string::npos || tdClose >= trE) break;
            tdPos = tdClose + 5;
        }

        rows.push_back({trS, tdCount});
        pos = trE + 5;
    }

    if (rows.empty()) return html;

    // Map cells to rows: cells are stored sequentially, each row has tdCount cells
    size_t cellIdx = 0;
    std::vector<std::vector<int>> rowCellIdx(rows.size());
    for (size_t ri = 0; ri < rows.size() && cellIdx < cells.size(); ri++) {
        for (int ci = 0; ci < rows[ri].tdCount && cellIdx < cells.size(); ci++) {
            rowCellIdx[ri].push_back((int)cellIdx);
            cellIdx++;
        }
    }

    // Compute total table Y bounds
    float totalY0 = 1e9f, totalY1 = 0;
    for (auto& b : boxes) {
        totalY0 = std::min(totalY0, b.y0);
        totalY1 = std::max(totalY1, b.y1);
    }
    float totalH = totalY1 - totalY0;
    if (totalH <= 0) return html;

    // Compute row heights as percentages
    std::vector<double> rowPcts(rows.size(), 0);
    for (size_t ri = 0; ri < rows.size(); ri++) {
        float rowY0 = 1e9f, rowY1 = 0;
        for (int ci : rowCellIdx[ri]) {
            if (ci >= 0 && ci < (int)boxes.size()) {
                rowY0 = std::min(rowY0, boxes[ci].y0);
                rowY1 = std::max(rowY1, boxes[ci].y1);
            }
        }
        if (rowY1 > rowY0)
            rowPcts[ri] = (double)(rowY1 - rowY0) / totalH * 100.0;
    }

    // Normalize rowPcts to sum to exactly 100%
    double sum = 0;
    for (double p : rowPcts) sum += p;
    if (sum > 0) {
        for (double& p : rowPcts) p = p / sum * 100.0;
    }

    // Find max logical column count (sum of colspan per row)
    int maxCols = 0;
    for (auto& r : rows) {
        int c = 0;
        size_t tdPos = html.find('>', r.trStart) + 1;
        size_t trEnd = lower.find("</tr>", r.trStart);
        while (tdPos < trEnd) {
            size_t tdS = lower.find("<td", tdPos);
            if (tdS == std::string::npos || tdS >= trEnd) break;
            size_t tdTagEnd = html.find('>', tdS);
            std::string tdTag = html.substr(tdS, tdTagEnd - tdS);
            // Parse colspan
            int colspan = 1;
            size_t csPos = tdTag.find("colspan=");
            if (csPos != std::string::npos) {
                size_t q = tdTag.find_first_of("'\"", csPos + 8);
                if (q != std::string::npos) {
                    char qc = tdTag[q];
                    size_t qEnd = tdTag.find(qc, q + 1);
                    if (qEnd != std::string::npos)
                        colspan = std::stoi(tdTag.substr(q + 1, qEnd - q - 1));
                }
            }
            c += colspan;
            size_t tdClose = lower.find("</td>", tdS);
            if (tdClose == std::string::npos || tdClose >= trEnd) break;
            tdPos = tdClose + 5;
        }
        if (c > maxCols) maxCols = c;
    }
    if (maxCols <= 0) maxCols = 1;

    // Build modified HTML
    std::string result = html;

    // Replace table tag with fixed layout
    size_t tableTag = result.find("<table");
    if (tableTag != std::string::npos) {
        size_t tableEnd = result.find('>', tableTag);
        if (tableEnd != std::string::npos) {
            result.replace(tableTag, tableEnd - tableTag + 1,
                "<table style='table-layout:fixed;width:100%'>");
        }
    }

    // Add <colgroup> with uniform column widths
    tableTag = result.find("<table");
    if (tableTag != std::string::npos) {
        size_t tableEnd = result.find('>', tableTag);
        double colPct = 100.0 / maxCols;
        std::string colgroup = "\n  <colgroup>\n";
        for (int c = 0; c < maxCols; c++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f", colPct);
            colgroup += "    <col style='width:" + std::string(buf) + "%'>\n";
        }
        colgroup += "  </colgroup>\n";
        result.insert(tableEnd + 1, colgroup);
    }

    // Add row heights to <tr> tags (process in reverse to preserve positions)
    // Re-find <tr> positions since colgroup insertion shifted everything
    std::vector<size_t> trPositions;
    {
        std::string lowerR = result;
        std::transform(lowerR.begin(), lowerR.end(), lowerR.begin(), ::tolower);
        size_t sp = 0;
        while (sp < result.size()) {
            size_t found = lowerR.find("<tr", sp);
            if (found == std::string::npos) break;
            trPositions.push_back(found);
            sp = found + 3;
        }
    }

    int numTrs = (int)trPositions.size();
    int numRowsToUse = std::min((int)rows.size(), numTrs);
    for (int ri = numRowsToUse - 1; ri >= 0; ri--) {
        size_t trS = trPositions[ri];
        size_t trTagEnd = result.find('>', trS);
        if (trTagEnd == std::string::npos) continue;

        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", rowPcts[ri]);
        result.insert(trTagEnd, " style='height:" + std::string(buf) + "%'");
    }

    return result;
}

void TableNet::fillCellTexts(TableResult &result, const std::vector<TextBlock> &ocrBlocks) {
    if (result.cellBoxes.empty() || ocrBlocks.empty()) {
        fprintf(stderr, "[TableNet] fillCellTexts: cellBoxes=%d, ocrBlocks=%d, skip\n",
                (int)result.cellBoxes.size(), (int)ocrBlocks.size());
        return;
    }

    int numCells = (int)result.cellBoxes.size();
    int numOcr = (int)ocrBlocks.size();

    fprintf(stderr, "[TableNet] fillCellTexts: %d cells, %d OCR blocks\n", numCells, numOcr);

    // Convert cell boxes to [x0,y0,x1,y1] from 8-point
    std::vector<float> cellBBox4(numCells * 4);
    for (int i = 0; i < numCells; i++) {
        const auto &b8 = result.cellBoxes[i];
        float xs[4] = {(float)b8[0], (float)b8[2], (float)b8[4], (float)b8[6]};
        float ys[4] = {(float)b8[1], (float)b8[3], (float)b8[5], (float)b8[7]};
        cellBBox4[i*4+0] = *std::min_element(xs, xs+4);
        cellBBox4[i*4+1] = *std::min_element(ys, ys+4);
        cellBBox4[i*4+2] = *std::max_element(xs, xs+4);
        cellBBox4[i*4+3] = *std::max_element(ys, ys+4);
    }

    // Convert OCR boxes to [x0,y0,x1,y1]
    std::vector<float> ocrBBox4(numOcr * 4);
    for (int i = 0; i < numOcr; i++) {
        const auto &pts = ocrBlocks[i].boxPoint;
        float xs[4] = {(float)pts[0].x, (float)pts[1].x, (float)pts[2].x, (float)pts[3].x};
        float ys[4] = {(float)pts[0].y, (float)pts[1].y, (float)pts[2].y, (float)pts[3].y};
        ocrBBox4[i*4+0] = *std::min_element(xs, xs+4);
        ocrBBox4[i*4+1] = *std::min_element(ys, ys+4);
        ocrBBox4[i*4+2] = *std::max_element(xs, xs+4);
        ocrBBox4[i*4+3] = *std::max_element(ys, ys+4);
    }

    // Ported from MinerU TableMatch._filter_ocr_result() (matcher.py:288-297).
    // Filters out OCR blocks whose bottom edge is above the topmost cell.
    float tableTop = 1e9f;
    for (int ci = 0; ci < numCells; ci++) {
        tableTop = std::min(tableTop, cellBBox4[ci * 4 + 1]);
    }
    std::vector<int> ocrIndexMap;  // maps filtered index -> original ocrBlocks index
    std::vector<float> filteredOcrBBox4;
    for (int oi = 0; oi < numOcr; oi++) {
        float ocrBottom = ocrBBox4[oi * 4 + 3];  // max y = bottom
        if (ocrBottom < tableTop) continue;  // entirely above table
        filteredOcrBBox4.insert(filteredOcrBBox4.end(), &ocrBBox4[oi * 4], &ocrBBox4[oi * 4 + 4]);
        ocrIndexMap.push_back(oi);
    }
    int numFilteredOcr = (int)ocrIndexMap.size();
    fprintf(stderr, "[TableNet] filter_ocr_result: %d -> %d (tableTop=%.0f)\n",
            numOcr, numFilteredOcr, tableTop);

    // Ported from MinerU TableMatch.match_result() + _select_best_cell_indices()
    // (matcher.py:132-159, matcher_utils.py:105-117).
    // For each OCR block, picks the cell with the highest IoU, using the
    // MinerU-style distance metric as tiebreaker when IoU values are equal.
    // Blocks with IoU <= 1e-8 are not assigned to any cell.
    std::vector<std::vector<int>> cellToOcr(numCells);
    for (int foi = 0; foi < numFilteredOcr; foi++) {
        int bestCell = -1;
        float bestIou = 0.0f;
        float bestDist = 0.0f;

        for (int ci = 0; ci < numCells; ci++) {
            float iou = calcIoU(&filteredOcrBBox4[foi * 4], &cellBBox4[ci * 4]);
            if (iou > 1e-8f) {
                float dist = calcMineruDist(&filteredOcrBBox4[foi * 4], &cellBBox4[ci * 4]);
                if (bestCell < 0 || iou > bestIou ||
                    (std::abs(iou - bestIou) < 1e-8f && dist < bestDist)) {
                    bestIou = iou;
                    bestDist = dist;
                    bestCell = ci;
                }
            }
        }

        int oi = ocrIndexMap[foi];
        if (bestCell >= 0) {
            cellToOcr[bestCell].push_back(oi);
            fprintf(stderr, "  OCR[%d] '%s' -> Cell[%d] iou=%.4f dist=%.1f\n",
                    oi, ocrBlocks[oi].text.c_str(), bestCell, bestIou, bestDist);
        } else {
            fprintf(stderr, "  OCR[%d] '%s' -> NO MATCH (no IoU > 1e-8)\n",
                    oi, ocrBlocks[oi].text.c_str());
        }
    }

    // Build per-cell text
    std::vector<std::string> cellTexts(numCells);
    for (int ci = 0; ci < numCells; ci++) {
        for (int oi : cellToOcr[ci]) {
            if (!cellTexts[ci].empty()) cellTexts[ci] += " ";
            cellTexts[ci] += ocrBlocks[oi].text;
        }
        fprintf(stderr, "  Cell[%d] text='%s'\n", ci, cellTexts[ci].c_str());
    }

    // Build final HTML: replace each <td></td> placeholder with proper
    // <td${attrs}>${text}</td>, using stored colspan/rowspan from TableCell.
    std::string html = result.htmlStructure;
    const std::string search = "<td></td>";
    size_t pos = 0;

    for (int ci = 0; ci < numCells; ci++) {
        pos = html.find(search, pos);
        if (pos == std::string::npos) {
            fprintf(stderr, "  [WARN] Cell[%d]: cannot find <td></td> placeholder in HTML\n", ci);
            break;
        }

        // Build replacement tag with colspan/rowspan attributes
        std::string replacement = "<td";
        if (ci < (int)result.cells.size()) {
            if (result.cells[ci].colspan > 1)
                replacement += " colspan='" + std::to_string(result.cells[ci].colspan) + "'";
            if (result.cells[ci].rowspan > 1)
                replacement += " rowspan='" + std::to_string(result.cells[ci].rowspan) + "'";
        }
        replacement += ">";
        if (!cellTexts[ci].empty())
            replacement += cellTexts[ci];
        replacement += "</td>";

        html.replace(pos, search.size(), replacement);
        pos += replacement.size();
    }

    // Auto-complete: ensure rectangular table by padding rows with missing cells
    html = autoCompleteTableHtml(html);

    // Add row/column sizes from cell bounding boxes
    html = addTableSizes(html, result.cells);

    result.htmlStructure = html;
    fprintf(stderr, "[TableNet] fillCellTexts done (auto-completed), HTML size=%d\n", (int)html.size());
}
