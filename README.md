# RapidOcrEmbed + OcrQtCpp

> 单文件 PP-OCRv6 OCR DLL（模型内嵌）+ 基于 Qt 的 OCR 应用（GUI / CLI / HTTP / 截图 / TextClick）

[English](#english) | [中文](#中文)

<a id="english"></a>

---

## English

### Overview

This repository contains two related projects:

| Project | Description | License |
|---------|-------------|---------|
| **OcrQtCpp** | Qt-based OCR application — GUI, CLI, HTTP server, screenshot, TextClick | MIT |
| **RapidOcrEmbed** | Single-file OCR DLL with embedded PP-OCRv6 models (used by OcrQtCpp) | Apache 2.0 |

---

## OcrQtCpp — Main Application

A feature-rich OCR tool built on Qt 5.15.2, integrating PP-OCRv6 and TBPU layout algorithm.

### Features

#### Multi-Mode Support

| Mode | Command | Description |
|------|---------|-------------|
| **GUI** | `OcrQtCpp.exe` | Graphical interface with drag-and-drop, screenshot, real-time results |
| **CLI** | `OcrQtCpp.exe --cli <paths...>` | Command-line batch recognition, outputs JSON |
| **Screenshot** | `OcrQtCpp.exe --shot` | Capture screen region, recognize and output JSON |
| **HTTP Server** | `OcrQtCpp.exe --server --port 8080` | Headless HTTP service with web UI |
| **TextClick** | `OcrQtCpp.exe --tc -click "text"` | OCR-based text click automation |

#### Table Recognition

Two selectable modes:

| Mode | Command | Algorithm | Use Case |
|------|---------|-----------|----------|
| **SLANet Plus** | `--table=1` | Deep learning model | Bordered & unbordered tables, merged cells |
| **img2table** | `--table=2` | Pure OpenCV (Hough lines) | Bordered tables only, no model needed |

Table text matching uses MinerU-style IoU algorithm. Output includes HTML table with text in cells.

#### 9 Layout Strategies

| Strategy | Name | Description |
|----------|------|-------------|
| `multi_para` | Multi-column paragraphs | Recommended for documents |
| `multi_line` | Multi-column line-by-line | Preserve line breaks |
| `multi_none` | Multi-column continuous | No line breaks |
| `single_para` | Single-column paragraphs | Simple documents |
| `single_line` | Single-column line-by-line | Preserve line breaks |
| `single_none` | Single-column continuous | No line breaks |
| `single_code` | Single-column code block | Preserve indentation |
| `single_vertical` | Ancient vertical text | Top-to-bottom, right-to-left |
| `none` | No processing | Raw output |

#### TextClick Automation

OCR-based text click for UI testing and automation:

```bash
OcrQtCpp.exe --tc -get                    # Get region text
OcrQtCpp.exe --tc -click "确定"            # Single click
OcrQtCpp.exe --tc -double "提交"           # Double click
OcrQtCpp.exe --tc -right "菜单"            # Right click
OcrQtCpp.exe --tc -move "按钮"             # Move mouse
OcrQtCpp.exe --tc -check "登录"            # Check if text exists
OcrQtCpp.exe --tc -pos "标题"              # Get text coordinates
OcrQtCpp.exe --tc -posall                  # Get all text coordinates
OcrQtCpp.exe --tc -list                    # List all text blocks
```

#### HTTP API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/ocr` | POST | Upload image(s), returns JSON with text |
| `/ocr-raw` | POST | Send raw image binary |
| `/ocr-batch` | POST | Multi-file batch recognition (NDJSON) |
| `/ocr-pdf` | POST | Returns searchable PDF binary |
| `/screenshot` | POST | Trigger server-side screenshot |
| `/textclick` | POST | TextClick automation |
| `/copy-clipboard` | POST | Copy HTML to system clipboard |
| `GET /api/config` | GET | Get current OCR parameters |
| `POST /api/config` | POST | Modify OCR parameters |

**Example:**
```bash
# Basic OCR
curl -F "file=@image.png" http://localhost:8080/ocr

# Table recognition (SLANet Plus)
curl -F "file=@table.png" "http://localhost:8080/ocr?table=1"

# Table recognition (img2table, pure OpenCV)
curl -F "file=@table.png" "http://localhost:8080/ocr?table=2"

# TextClick
curl -X POST http://localhost:8080/textclick \
  -H "Content-Type: application/json" \
  -d '{"action":"click","text":"确定"}'
```

#### Additional Features

- **Coordinate mode** (`--coords`): Display text block coordinates, confidence, angle
- **Searchable PDF** (`--pdf`): Generate PDF with text layer from images
- **PDF page selection** (`--pages`): Specify pages for PDF input
- **Clipboard copy** (`--clipboard`): Copy recognition results to clipboard
- **Rotation correction** (`--rotate`): Enable/disable text orientation correction
- **Chinese path support**: Full UTF-8 / wide-character path handling

### Usage Examples

```bash
# Single file
OcrQtCpp.exe --cli image.png

# Multiple files
OcrQtCpp.exe --cli image1.png image2.jpg

# Recursive folder scan
OcrQtCpp.exe --cli ./images/

# Coordinate mode
OcrQtCpp.exe --cli --coords image.png

# Layout mode
OcrQtCpp.exe --cli --layout --layout-strategy single_para image.png

# Screenshot with region
OcrQtCpp.exe --shot --region 100,100,800,600

# Generate searchable PDF
OcrQtCpp.exe --cli --pdf image.png

# HTTP server on custom port
OcrQtCpp.exe --server --port 9000
```

### Build

**Requirements:**
- **Visual Studio 2019** — [Download](https://pan.baidu.com/s/1KvbzItJbrHqf51KVIiBPFw) 提取码: `849s`
- **Qt 5.15.2** (static build)
- **RapidOcrEmbed** (prebuilt DLL)

```bash
# Extract Qt static libs to C:\
7z x "Qt5静态库放c盘根目录.7z.001" -oC:\

# Extract OcrQtCpp libs
cd ocr-qt-cpp
7z x "放到ocr-qt-cpp的libs.rar" -olibs

# Build
python build_msvc.py --clean

# Output: release/OcrQtCpp.exe (~86MB)
```

Full documentation: [`ocr-qt-cpp/README.md`](ocr-qt-cpp/README.md)

---

## RapidOcrEmbed — OCR Core Library

A derivative of [RapidAI/RapidOcrOnnx](https://github.com/RapidAI/RapidOcrOnnx), upgraded to **PaddleOCR PP-OCRv6** with embedded-model build mode (single-file DLL, no external `.onnx` needed).

### What's new vs upstream

| Area | Upstream | RapidOcrEmbed |
|------|----------|---------------|
| Models | PP-OCRv2/v3 | **PP-OCRv6** (det + rec) + **PP-LCNet** classifier |
| Orientation | 0°/180° | **PP-LCNet_x1_0_textline_ori** |
| Distribution | Separate .onnx + .dll | **Single DLL** (models embedded) |
| Windows path | ANSI only | **UTF-8 / wide-char** (Chinese paths) |
| Build | Dynamic deps | **Statically linked** ONNX Runtime + OpenCV |
| C API | Basic | Extended: 33 exported functions |
| Table recognition | — | **SLANet Plus** + **img2table** + MinerU IoU matching |
| Inference | ONNX Runtime | ONNX Runtime + **MNN** (optional) |
| Variants | — | tiny / small / medium |

### C API Reference

33 exported functions (`extern "C"`, `Ocr*` prefix). Full signatures: [`include/OcrLiteCApi.h`](RapidOcrEmbed/include/OcrLiteCApi.h)

**Initialization:**

| Function | Purpose |
|----------|---------|
| `OcrInit(det, cls, rec, keys, nThreads)` | Load models from disk |
| `OcrInitEmbedded(nThreads)` | Use embedded models (no file paths) |
| `OcrInitTable(det, cls, rec, keys, tableModel, nThreads)` | Init table model from disk |
| `OcrInitTableEmbedded(nThreads)` | Init OCR + SLANet Plus (embedded) |

**Recognition:**

| Function | Purpose |
|----------|---------|
| `OcrDetect(handle, dir, name, param)` | Detect via file path |
| `OcrDetectMem(handle, bytes, size, param)` | Detect via in-memory bytes |
| `OcrDetectEx(handle, dir, name, param)` | Detect with per-block results |
| `OcrDetectMemEx(handle, bytes, size, param)` | Detect mem with per-block results |
| `OcrGetLen(handle)` | Get result string length |
| `OcrGetResult(handle, buf, len)` | Get result text into buffer |
| `OcrGetResultMem(handle, buf)` | Get result text (DLL allocated) |

**Per-block queries:**

| Function | Purpose |
|----------|---------|
| `OcrGetBlockCount(handle)` | Number of text blocks |
| `OcrGetBlockText(handle, index, buf, len)` | Text of block N |
| `OcrGetBlockScore(handle, index)` | Confidence of block N |
| `OcrGetBlockBox(handle, index, buf)` | Bounding box (8 ints: 4 points x,y) |
| `OcrGetBlockAngle(handle, index)` | Angle index of block N |
| `OcrGetBlockAngleScore(handle, index)` | Angle confidence of block N |
| `OcrGetBlockCharScores(handle, index, buf, len)` | Per-character scores |

**Layout strategy:**

| Function | Purpose |
|----------|---------|
| `OcrSetLayoutStrategy(handle, name)` | Set layout strategy by name |
| `OcrGetLayoutStrategy(handle, buf, len)` | Get current strategy name |
| `OcrGetLayoutStrategyCount()` | Number of available strategies |
| `OcrGetLayoutStrategyInfo(index, name, nameLen, desc, descLen, shortName, shortNameLen)` | Get strategy info by index |

**Table recognition:**

| Function | Purpose |
|----------|---------|
| `OcrSetTableMode(handle, mode)` | Set table mode (0=SLANet Plus, 1=img2table) |
| `OcrDetectTable(handle, dir, name, param)` | Table detect via file |
| `OcrDetectTableMem(handle, bytes, size, param)` | Table detect via memory |
| `OcrGetTableLen(handle)` | Table result length |
| `OcrGetTableResult(handle, buf, len)` | Table HTML result |
| `OcrGetTableResultMem(handle, buf)` | Table HTML (DLL allocated) |
| `OcrGetTableStructureScore(handle)` | Structure confidence |
| `OcrGetTableOcrText(handle, buf, len)` | OCR text (no cell association) |
| `OcrGetTableCellCount(handle)` | Number of table cells |
| `OcrGetTableCell(handle, index, x1, y1, x2, y2)` | Cell bounding box |

**Cleanup:**

| Function | Purpose |
|----------|---------|
| `OcrDestroy(handle)` | Release handle |

### OCR_PARAM Struct

```c
typedef struct __ocr_param {
    int   padding;         // default 50
    int   maxSideLen;      // default 1024 (longest side after resize)
    float boxScoreThresh;  // default 0.6 (text box confidence threshold)
    float boxThresh;       // default 0.3 (binarization threshold)
    float unClipRatio;     // default 2.0 (text box expansion ratio)
    int   doAngle;         // 1 = run orientation classifier (default 1)
} OCR_PARAM;
```

### Build from Source

```bash
# 1. Extract static deps
cd RapidOcrEmbed
7z x "压缩包解压后到当前目录/onnxruntime-static.rar" -o.
7z x "压缩包解压后到当前目录/opencv-static.rar" -o.

# 2. (Optional) Generate embedded model headers
python "模型转二进制/embed_models.py"

# 3. Build all variants
python build.py
# → output/RapidOcrOnnx_{tiny,small,medium}.{dll,lib}
```

CMake options: `OCR_OUTPUT` (BIN/CLIB/JNI), `OCR_ONNX` (CPU/CUDA), `OCR_EMBEDDED_MODELS` (ON/OFF), `OCR_BUILD_CRT` (True/False), `OCR_BENCHMARK` (ON/OFF).

### Python Example

```python
import ctypes

dll = ctypes.CDLL("RapidOcrOnnx_small.dll")
dll.OcrInitEmbedded.restype = ctypes.c_void_p
handle = dll.OcrInitEmbedded(1)

class OCR_PARAM(ctypes.Structure):
    _fields_ = [
        ("padding", ctypes.c_int), ("maxSideLen", ctypes.c_int),
        ("boxScoreThresh", ctypes.c_float), ("boxThresh", ctypes.c_float),
        ("unClipRatio", ctypes.c_float), ("doAngle", ctypes.c_int),
    ]

param = OCR_PARAM(50, 1024, 0.6, 0.3, 2.0, 1)
with open("test.jpg", "rb") as f:
    img = f.read()
img_buf = (ctypes.c_ubyte * len(img))(*img)
dll.OcrDetectMem(handle, img_buf, len(img), ctypes.byref(param))
length = dll.OcrGetLen(handle)
buf = ctypes.create_string_buffer(length + 1)
dll.OcrGetResult(handle, buf, length + 1)
print(buf.value.decode("utf-8"))
dll.OcrDestroy(handle)
```

Full documentation: [`RapidOcrEmbed/README.md`](RapidOcrEmbed/README.md)

---

<a id="中文"></a>

# 中文说明

## 概览

本仓库包含两个相关项目：

| 项目 | 说明 | 许可证 |
|------|------|--------|
| **OcrQtCpp** | 基于 Qt 的 OCR 应用（GUI / CLI / HTTP / 截图 / TextClick） | MIT |
| **RapidOcrEmbed** | 单文件 OCR DLL，内嵌 PP-OCRv6 模型（OcrQtCpp 底层库） | Apache 2.0 |

---

## OcrQtCpp — 主程序

基于 Qt 5.15.2 构建的功能完整的 OCR 识别工具，集成 PP-OCRv6 和 TBPU 排版算法。

### 功能特性

#### 多模式支持

| 模式 | 命令 | 说明 |
|------|------|------|
| **GUI** | `OcrQtCpp.exe` | 图形界面，支持拖拽、截图、实时显示结果 |
| **CLI** | `OcrQtCpp.exe --cli <路径...>` | 命令行批量识别，输出 JSON |
| **截图** | `OcrQtCpp.exe --shot` | 截取屏幕区域，识别后输出 JSON |
| **HTTP 服务** | `OcrQtCpp.exe --server --port 8080` | 无界面 HTTP 服务，含网页端 UI |
| **TextClick** | `OcrQtCpp.exe --tc -click "文字"` | 基于 OCR 的文字点击自动化 |

#### 表格识别

两种可选模式：

| 模式 | 命令 | 算法 | 适用场景 |
|------|------|------|----------|
| **SLANet Plus** | `--table=1` | 深度学习模型 | 有线框/无线框表格、合并单元格 |
| **img2table** | `--table=2` | 纯 OpenCV (霍夫线检测) | 仅支持有线框表格，无需模型 |

表格文本匹配使用 MinerU 风格 IoU 算法。输出包含含文本的 HTML 表格。

#### 9 种排版策略

| 策略名 | 说明 |
|--------|------|
| `multi_para` | 多栏-自然段（推荐文档） |
| `multi_line` | 多栏-每行换行 |
| `multi_none` | 多栏-无换行（连续文本） |
| `single_para` | 单栏-自然段 |
| `single_line` | 单栏-每行换行 |
| `single_none` | 单栏-无换行（连续文本） |
| `single_code` | 单栏-代码段（保留缩进） |
| `single_vertical` | 古籍竖排（从右到左，从上到下） |
| `none` | 不处理 |

#### TextClick 文字点击自动化

```bash
OcrQtCpp.exe --tc -get                    # 获取区域文本
OcrQtCpp.exe --tc -click "确定"            # 单击
OcrQtCpp.exe --tc -double "提交"           # 双击
OcrQtCpp.exe --tc -right "菜单"            # 右击
OcrQtCpp.exe --tc -move "按钮"             # 移动鼠标
OcrQtCpp.exe --tc -check "登录"            # 检查文字是否存在
OcrQtCpp.exe --tc -pos "标题"              # 获取文字坐标
OcrQtCpp.exe --tc -posall                  # 获取所有文字坐标
OcrQtCpp.exe --tc -list                    # 列出所有文字块
```

#### HTTP API 接口

| 端点 | 方法 | 说明 |
|------|------|------|
| `/ocr` | POST | 上传图片，返回 JSON 文本 |
| `/ocr-raw` | POST | 发送原始图片二进制数据 |
| `/ocr-batch` | POST | 多文件批量识别（NDJSON） |
| `/ocr-pdf` | POST | 返回可搜索 PDF 二进制 |
| `/screenshot` | POST | 触发服务器端截图 |
| `/textclick` | POST | TextClick 自动化 |
| `/copy-clipboard` | POST | 复制 HTML 到系统剪贴板 |
| `GET /api/config` | GET | 获取当前 OCR 参数 |
| `POST /api/config` | POST | 修改 OCR 参数 |

**示例：**
```bash
# 基础 OCR
curl -F "file=@image.png" http://localhost:8080/ocr

# 表格识别（SLANet Plus）
curl -F "file=@table.png" "http://localhost:8080/ocr?table=1"

# 表格识别（img2table 纯 OpenCV）
curl -F "file=@table.png" "http://localhost:8080/ocr?table=2"

# TextClick
curl -X POST http://localhost:8080/textclick \
  -H "Content-Type: application/json" \
  -d '{"action":"click","text":"确定"}'
```

#### 其他功能

- **坐标模式**（`--coords`）：显示文本块坐标、置信度、角度
- **可搜索 PDF**（`--pdf`）：从图片生成带文字层的 PDF
- **PDF 页码选择**（`--pages`）：指定 PDF 输入的页码
- **剪贴板复制**（`--clipboard`）：复制识别结果到剪贴板
- **旋转矫正**（`--rotate`）：启用/禁用文本方向校正
- **中文路径支持**：全面 UTF-8 / 宽字符路径处理

### 使用示例

```bash
# 单文件识别
OcrQtCpp.exe --cli image.png

# 多文件识别
OcrQtCpp.exe --cli image1.png image2.jpg

# 文件夹递归识别
OcrQtCpp.exe --cli ./images/

# 坐标模式
OcrQtCpp.exe --cli --coords image.png

# 排版模式
OcrQtCpp.exe --cli --layout --layout-strategy single_para image.png

# 指定区域截图
OcrQtCpp.exe --shot --region 100,100,800,600

# 生成可搜索 PDF
OcrQtCpp.exe --cli --pdf image.png

# HTTP 服务指定端口
OcrQtCpp.exe --server --port 9000
```

### 编译

**环境要求：**
- **Visual Studio 2019** — [下载地址](https://pan.baidu.com/s/1KvbzItJbrHqf51KVIiBPFw) 提取码: `849s`
- **Qt 5.15.2**（静态编译）
- **RapidOcrEmbed**（预编译 DLL）

```bash
# 解压 Qt 静态库
7z x "Qt5静态库放c盘根目录.7z.001" -oC:\

# 解压 OcrQtCpp 依赖库
cd ocr-qt-cpp
7z x "放到ocr-qt-cpp的libs.rar" -olibs

# 编译
python build_msvc.py --clean

# 输出：release/OcrQtCpp.exe (~86MB)
```

详细文档：[`ocr-qt-cpp/README.md`](ocr-qt-cpp/README.md)

---

## RapidOcrEmbed — OCR 核心库

基于 [RapidAI/RapidOcrOnnx](https://github.com/RapidAI/RapidOcrOnnx) 派生，升级到 PaddleOCR PP-OCRv6，新增模型内嵌编译模式（单一 DLL、无需外部 .onnx）。

### 相比上游的改动

| 维度 | 上游 | 本项目 |
|------|------|--------|
| 模型 | PP-OCRv2/v3 | **PP-OCRv6** (det+rec) + **PP-LCNet** 分类器 |
| 方向分类 | 0°/180° | **PP-LCNet_x1_0_textline_ori** |
| 分发方式 | DLL + 外部 .onnx | **单文件 DLL**（模型内嵌） |
| Windows 路径 | 仅 ANSI | **UTF-8 / 宽字符**，支持中文 |
| 链接方式 | 动态依赖 | **静态链接**，CRT /MT |
| C API | 基础 | 扩展：33 个导出函数 |
| 表格识别 | — | **SLANet Plus** + **img2table** + MinerU IoU 匹配 |
| 推理引擎 | ONNX Runtime | ONNX Runtime + **MNN**（可选） |
| 模型规格 | — | tiny / small / medium 三档 |

### C API 接口

33 个导出函数（`extern "C"`，`Ocr*` 前缀）。完整签名见 [`include/OcrLiteCApi.h`](RapidOcrEmbed/include/OcrLiteCApi.h)

**初始化：**

| 函数 | 作用 |
|------|------|
| `OcrInit(det, cls, rec, keys, nThreads)` | 从磁盘加载模型创建句柄 |
| `OcrInitEmbedded(nThreads)` | 使用内嵌模型创建句柄 |
| `OcrInitTable(det, cls, rec, keys, tableModel, nThreads)` | 从磁盘初始化表格模型 |
| `OcrInitTableEmbedded(nThreads)` | 初始化 OCR + SLANet Plus（内嵌） |

**识别：**

| 函数 | 作用 |
|------|------|
| `OcrDetect(handle, dir, name, param)` | 按文件路径识别 |
| `OcrDetectMem(handle, bytes, size, param)` | 按内存图片字节识别 |
| `OcrDetectEx(handle, dir, name, param)` | 识别并保留逐文本块结果 |
| `OcrDetectMemEx(handle, bytes, size, param)` | 内存图片识别并保留逐块结果 |
| `OcrGetLen(handle)` | 获取结果文本长度 |
| `OcrGetResult(handle, buf, len)` | 获取结果文本到缓冲区 |
| `OcrGetResultMem(handle, buf)` | 获取结果文本（DLL 内部分配） |

**逐文本块查询：**

| 函数 | 作用 |
|------|------|
| `OcrGetBlockCount(handle)` | 文本块数量 |
| `OcrGetBlockText(handle, index, buf, len)` | 第 N 块文本内容 |
| `OcrGetBlockScore(handle, index)` | 第 N 块置信度 |
| `OcrGetBlockBox(handle, index, buf)` | 第 N 块坐标框（8 个整数） |
| `OcrGetBlockAngle(handle, index)` | 第 N 块角度索引 |
| `OcrGetBlockAngleScore(handle, index)` | 第 N 块角度置信度 |
| `OcrGetBlockCharScores(handle, index, buf, len)` | 第 N 块每个字符置信度 |

**排版策略：**

| 函数 | 作用 |
|------|------|
| `OcrSetLayoutStrategy(handle, name)` | 按名称设置排版策略 |
| `OcrGetLayoutStrategy(handle, buf, len)` | 获取当前策略名 |
| `OcrGetLayoutStrategyCount()` | 可用策略数量 |
| `OcrGetLayoutStrategyInfo(index, ...)` | 按索引获取策略信息 |

**表格识别：**

| 函数 | 作用 |
|------|------|
| `OcrSetTableMode(handle, mode)` | 设置表格模式（0=SLANet Plus, 1=img2table） |
| `OcrDetectTable(handle, dir, name, param)` | 文件路径方式检测表格 |
| `OcrDetectTableMem(handle, bytes, size, param)` | 内存图片方式检测表格 |
| `OcrGetTableLen(handle)` | 表格结果长度 |
| `OcrGetTableResult(handle, buf, len)` | 表格 HTML 结果 |
| `OcrGetTableResultMem(handle, buf)` | 表格 HTML（DLL 内部分配） |
| `OcrGetTableStructureScore(handle)` | 结构识别置信度 |
| `OcrGetTableOcrText(handle, buf, len)` | 拼接的 OCR 文本 |
| `OcrGetTableCellCount(handle)` | 表格单元格数量 |
| `OcrGetTableCell(handle, index, ...)` | 单元格坐标 |

**释放：**

| 函数 | 作用 |
|------|------|
| `OcrDestroy(handle)` | 释放句柄 |

### OCR_PARAM 结构体

```c
typedef struct __ocr_param {
    int   padding;         // 默认 50，图像边缘填充
    int   maxSideLen;      // 默认 1024，最大边长
    float boxScoreThresh;  // 默认 0.6，文本框置信度阈值
    float boxThresh;       // 默认 0.3，二值化阈值
    float unClipRatio;     // 默认 2.0，文本框扩展比例
    int   doAngle;         // 1 = 启用方向分类（默认 1）
} OCR_PARAM;
```

### 从源码编译

```bash
# 1. 解压静态依赖
cd RapidOcrEmbed
7z x "压缩包解压后到当前目录/onnxruntime-static.rar" -o.
7z x "压缩包解压后到当前目录/opencv-static.rar" -o.

# 2.（可选）生成内嵌模型头文件
python "模型转二进制/embed_models.py"

# 3. 编译三个版本
python build.py
# → output/RapidOcrOnnx_{tiny,small,medium}.{dll,lib}
```

CMake 选项：`OCR_OUTPUT` (BIN/CLIB/JNI)、`OCR_ONNX` (CPU/CUDA)、`OCR_EMBEDDED_MODELS` (ON/OFF)、`OCR_BUILD_CRT` (True/False)、`OCR_BENCHMARK` (ON/OFF)。

### Python 示例

```python
import ctypes

dll = ctypes.CDLL("RapidOcrOnnx_small.dll")
dll.OcrInitEmbedded.restype = ctypes.c_void_p
handle = dll.OcrInitEmbedded(1)

class OCR_PARAM(ctypes.Structure):
    _fields_ = [
        ("padding", ctypes.c_int), ("maxSideLen", ctypes.c_int),
        ("boxScoreThresh", ctypes.c_float), ("boxThresh", ctypes.c_float),
        ("unClipRatio", ctypes.c_float), ("doAngle", ctypes.c_int),
    ]

param = OCR_PARAM(50, 1024, 0.6, 0.3, 2.0, 1)
with open("test.jpg", "rb") as f:
    img = f.read()
img_buf = (ctypes.c_ubyte * len(img))(*img)
dll.OcrDetectMem(handle, img_buf, len(img), ctypes.byref(param))
length = dll.OcrGetLen(handle)
buf = ctypes.create_string_buffer(length + 1)
dll.OcrGetResult(handle, buf, length + 1)
print(buf.value.decode("utf-8"))
dll.OcrDestroy(handle)
```

详细文档：[`RapidOcrEmbed/README.md`](RapidOcrEmbed/README.md)

---

## Open Source Libraries (开源库依赖)

### Core OCR Engine (核心 OCR 引擎)

| Library | Purpose | License | URL |
|---------|---------|---------|-----|
| **PaddleOCR PP-OCRv6** | Text detection + recognition models | PaddlePaddle | https://github.com/PaddlePaddle/PaddleOCR |
| **PP-LCNet** | Text orientation classifier (0°/180°) | PaddlePaddle | https://github.com/PaddlePaddle/PaddleOCR |
| **RapidOcrOnnx** | C++ PP-OCR runtime (derivative base) | Apache 2.0 | https://github.com/RapidAI/RapidOcrOnnx |

### Inference Engines (推理引擎)

| Library | Purpose | License | URL |
|---------|---------|---------|-----|
| **ONNX Runtime** | Default inference backend | MIT | https://github.com/microsoft/onnxruntime |
| **MNN** | Lightweight alternative inference engine | Apache 2.0 | https://github.com/alibaba/MNN |

### Image Processing (图像处理)

| Library | Purpose | License | URL |
|---------|---------|---------|-----|
| **OpenCV** | Image I/O, resize, color conversion | Apache 2.0 | https://github.com/opencv/opencv |

### Table Recognition (表格识别)

| Library | Purpose | License | URL |
|---------|---------|---------|-----|
| **SLANet Plus** | Deep learning table structure recognition | PaddlePaddle | https://github.com/PaddlePaddle/PaddleOCR |
| **img2table** | Pure OpenCV bordered table detection (C++ port) | Apache 2.0 | https://github.com/xavctn/img2table |
| **MinerU** | Table structure matching (IoU + distance, algorithm reference only) | AGPL-3.0 | https://github.com/opendatalab/MinerU |
| **Clipper library** | Polygon clipping for table processing | Boost | https://sourceforge.net/p/polyclipping/ |

### Layout & Formatting (排版算法)

| Library | Purpose | License | URL |
|---------|---------|---------|-----|
| **Umi-OCR TBPU** | Text Block Processing Unit layout algorithm | MIT | https://github.com/hiroi-sora/Umi-OCR |
| **k2pdfopt** | Sorting algorithm reference for TBPU | — | http://willus.com/k2pdfopt/ |

### UI Framework (界面框架)

| Library | Purpose | License | URL |
|---------|---------|---------|-----|
| **Qt 5.15.2** | Cross-platform GUI framework (static build) | LGPLv3 | https://www.qt.io/ |

### PDF Processing (PDF 处理)

| Library | Purpose | License | URL |
|---------|---------|---------|-----|
| **MuPDF** | PDF rendering and text extraction | AGPL-3.0 | https://mupdf.com/ |
| **openjpeg** | JPEG 2000 codec for PDF | BSD-2 | https://github.com/uclouvain/openjpeg |
| **jbig2dec** | JBIG2 codec for PDF | AGPL-3.0 | https://ghostscript.com/releases/jbig2dec.html |
| **mujs** | JavaScript interpreter for PDF | AGPL-3.0 | https://mupdf.com/ |

### Spreadsheet Generation (表格生成)

| Library | Purpose | License | URL |
|---------|---------|---------|-----|
| **libxlsxwriter** | XLSX spreadsheet generation | BSD-2 | https://github.com/jmcnamara/libxlsxwriter |

### Font & Text (字体与文本)

| Library | Purpose | License | URL |
|---------|---------|---------|-----|
| **freetype** | Font rendering | FreeType | https://freetype.org/ |
| **harfbuzz** | Text shaping engine | MIT | https://github.com/harfbuzz/harfbuzz |

### Compression & Utilities (压缩与工具)

| Library | Purpose | License | URL |
|---------|---------|---------|-----|
| **zlib** | General compression | zlib | https://zlib.net/ |
| **brotli** | Google compression algorithm | MIT | https://github.com/google/brotli |
| **lcms2** | Color management | MIT | https://www.littlecms.com/ |

---

## Directory Structure

```
RapidOcrEmbed/
├── RapidOcrEmbed/              # OCR 核心库 (Apache 2.0)
│   ├── CMakeLists.txt
│   ├── build.py
│   ├── include/                # Public headers
│   │   ├── OcrLite.h
│   │   ├── OcrLiteCApi.h       # C API (33 functions)
│   │   ├── ModelLoader.h
│   │   └── models/             # Auto-generated (git-ignored)
│   ├── src/                    # Implementation
│   │   ├── OcrLite.cpp         # Core OCR pipeline
│   │   ├── DbNet.cpp           # Text detection
│   │   ├── CrnnNet.cpp         # Text recognition
│   │   ├── AngleNet.cpp        # Orientation classification
│   │   ├── TableNet.cpp        # SLANet Plus table recognition
│   │   ├── OcrLiteCApi.cpp     # C API exports
│   │   └── ModelLoader.cpp
│   ├── img2table-cpp/          # Table detection (C++ port)
│   ├── 模型转二进制/            # ONNX → C header converter
│   ├── 压缩包解压后到当前目录/  # Static deps (extract here)
│   │   ├── onnxruntime-static.rar
│   │   ├── opencv-static.rar
│   │   └── MNN_lib.rar
│   ├── README.md
│   ├── LICENSE                 # Apache 2.0
│   └── NOTICE
├── ocr-qt-cpp/                 # Qt OCR 应用 (MIT)
│   ├── OcrQtCpp.pro
│   ├── MainWindow.cpp/h        # GUI main window
│   ├── OCRWrapper.cpp/h        # OCR wrapper
│   ├── TbpuLayout.cpp/h        # TBPU layout (from Umi-OCR)
│   ├── CliRunner.cpp/h         # CLI tool
│   ├── OcrHttpServer.cpp/h     # HTTP server
│   ├── TextClickCli.cpp/h      # TextClick CLI
│   ├── TextClickEngine.cpp/h   # TextClick core engine
│   ├── webui.h                 # Embedded web UI
│   ├── libs/                   # Static libs (extract here)
│   │   └── RapidOcrOnnxStatil.lib
│   ├── 放到ocr-qt-cpp的libs.rar
│   ├── README.md
│   ├── LICENSE                 # MIT
│   └── NOTICE
├── LICENSES/                   # Third-party license texts
│   ├── LGPLv3.txt
│   ├── GPLv3.txt
│   ├── Apache-2.0.txt
│   ├── MIT.txt
│   ├── AGPL-3.0.txt
│   └── PaddlePaddle.txt
├── Qt5静态库放c盘根目录.7z.001  # Qt5 static libs (extract to C:\)
├── Qt5静态库放c盘根目录.7z.002  # Part 2
├── README.md
├── DEP_BUILD.md                # Dependency build guide
├── LICENSE                     # See License section
└── NOTICE                      # Attribution
```

## Decompression Instructions (解压说明)

为减少仓库上传体积，大型依赖以压缩包形式存储。

### RapidOcrEmbed 静态依赖

| 压缩包 | 解压目标 | 说明 |
|--------|----------|------|
| `RapidOcrEmbed/压缩包解压后到当前目录/onnxruntime-static.rar` | `RapidOcrEmbed/onnxruntime-static/` | ONNX Runtime 静态库 |
| `RapidOcrEmbed/压缩包解压后到当前目录/opencv-static.rar` | `RapidOcrEmbed/opencv-static/` | OpenCV 静态库 |
| `RapidOcrEmbed/压缩包解压后到当前目录/MNN_lib.rar` | `RapidOcrEmbed/` | MNN 推理引擎（可选） |

### OcrQtCpp 静态库

| 压缩包 | 解压目标 | 说明 |
|--------|----------|------|
| `ocr-qt-cpp/放到ocr-qt-cpp的libs.rar` | `ocr-qt-cpp/libs/` | OcrQtCpp 编译所需的静态库 |

> `ocr-qt-cpp/libs/` 中已包含预编译好的 `RapidOcrOnnxStatil.lib`，可直接使用。

### Qt5 静态库

| 压缩包 | 解压目标 | 说明 |
|--------|----------|------|
| `Qt5静态库放c盘根目录.7z.001` + `.002` | `C:\Qt5静态库\` | Qt 5.15.2 静态编译库 |

> Qt5 静态库需要放到 C 盘根目录，编译脚本默认从此路径读取。
> 本项目附带预编译 Qt 静态库仅为方便编译，用户可自行从 Qt 官网下载源码重新编译替换。

---

## License

| Component | License |
|-----------|---------|
| OcrQtCpp (Qt application) | [MIT License](ocr-qt-cpp/LICENSE) |
| RapidOcrEmbed (DLL library) | [Apache License 2.0](RapidOcrEmbed/LICENSE) |
| PaddleOCR PP-OCRv6 / PP-LCNet | PaddlePaddle License |
| TBPU Layout (from Umi-OCR) | MIT License |
| img2table (C++ port) | Apache License 2.0 |
| MinerU (algorithm reference only) | AGPL-3.0 |
| ONNX Runtime | MIT License |
| MNN | Apache License 2.0 |
| OpenCV | Apache License 2.0 |
| Qt 5.15.2 | LGPLv3 |

## Acknowledgements

- [RapidAI/RapidOcrOnnx](https://github.com/RapidAI/RapidOcrOnnx) — upstream OCR runtime
- [PaddlePaddle/PaddleOCR](https://github.com/PaddlePaddle/PaddleOCR) — PP-OCRv6 models
- [hiroi-sora/Umi-OCR](https://github.com/hiroi-sora/Umi-OCR) — TBPU layout algorithm
- [xavctn/img2table](https://github.com/xavctn/img2table) — table detection algorithm
- [opendatalab/MinerU](https://github.com/opendatalab/MinerU) — table structure matching
- [Microsoft ONNX Runtime](https://github.com/microsoft/onnxruntime) — inference engine
- [Alibaba MNN](https://github.com/alibaba/MNN) — lightweight inference engine
- [OpenCV](https://opencv.org/) — image processing
- [Qt](https://www.qt.io/) — cross-platform UI framework

## Contributing

Issues and Pull Requests are welcome!
