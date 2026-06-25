# Release Notes — v1.0.0 (Draft)

> 这是可直接复制粘贴到 GitHub **Releases → Draft a new release** 页面的内容。
> 仓库名 / 标签 / 标题按下方建议填写。

---

## 📋 在 GitHub Release 页面填写的内容

**Tag / 标签**: `v1.0.0`
**Release title / 标题**: `RapidOcrEmbed v1.0.0 — 单文件 PP-OCRv6 OCR DLL（模型内嵌）`
**Describe this release / 描述**: （复制下方整段正文）

---

## 正文（复制以下内容）

# 🚀 RapidOcrEmbed v1.0.0

> **单文件 PP-OCRv6 OCR DLL** — 模型内嵌，开箱即用，无需任何外部 `.onnx` 文件。

基于 [RapidAI/RapidOcrOnnx](https://github.com/RapidAI/RapidOcrOnnx) 派生，将 OCR 流水线升级到 **PaddleOCR PP-OCRv6**，并新增**模型内嵌**编译模式：检测/分类/识别模型与字典被直接编进 DLL，**一个 DLL 文件即可完成全部 OCR 任务**（hence the name *Embed*）。

许可证沿用上游 **Apache License 2.0**。

---

## ✨ 核心特性

| 特性 | 说明 |
|------|------|
| 🧠 **PP-OCRv6 模型** | 检测 (det) + 识别 (rec) 全面升级到 PaddleOCR PP-OCRv6 |
| 📐 **PP-LCNet 方向分类器** | `PP-LCNet_x1_0_textline_ori`，输入 `[1,3,80,160]`，自动识别 0°/180° 倒置文本 |
| 📦 **单文件 DLL** | 模型 + 字典全部内嵌进 DLL，**无任何外部依赖文件** |
| 🔗 **静态链接** | ONNX Runtime + OpenCV 全静态链接，CRT `/MT`，可独立分发 |
| 🪶 **三种规格** | `tiny` / `small` / `medium`，体积与精度自由取舍 |
| 🇨🇳 **中文路径支持** | UTF-8 / 宽字符路径处理，完美兼容中文路径 |
| 🔌 **丰富 C API** | 文件路径 / 内存字节 双输入；逐文本块查询（坐标、置信度、角度） |

---

## 📦 下载（Downloads）

> **直接使用者**：只需下载**一个 DLL**，放到你的程序同级目录即可，无需其它文件。

### 推荐下载

| 文件 | 大小 | 适用场景 |
|------|------|----------|
| **`RapidOcrOnnx_small.dll`** | ~57 MB | ⭐ **推荐** — 精度/速度/体积的最佳平衡 |
| `RapidOcrOnnx_tiny.dll` | ~33 MB | 对体积敏感、可接受较低精度的场景 |
| `RapidOcrOnnx_medium.dll` | ~159 MB | 追求最高精度、不在乎体积的场景 |

> ℹ️ DLL 文件名仍为 `RapidOcrOnnx_*.dll`（保留上游 ABI 命名，便于兼容）。
> 项目名 `RapidOcrEmbed` 仅用于仓库/文档展示。如需重命名为 `RapidOcrEmbed_*.dll`，可参考 CHANGELOG 自行编译。

### 从源码构建者

如需自行编译，下载 **`RapidOcrEmbed-deps-win-x64.zip`**（依赖包），解压后放到项目根目录的 `onnxruntime-static/` 和 `opencv-static/`，再运行 `python build.py`。

---

## 🚀 快速开始

### 1. Python 调用示例

```python
import ctypes

# 加载 DLL（模型已内嵌，无需任何外部文件）
dll = ctypes.CDLL("RapidOcrOnnx_small.dll")
dll.OcrInitEmbedded.restype = ctypes.c_void_p
handle = dll.OcrInitEmbedded(1)          # 1 = 打开调试日志

# 读取图片为字节
with open("test.jpg", "rb") as f:
    img_bytes = f.read()

# 识别参数
class OCR_PARAM(ctypes.Structure):
    _fields_ = [
        ("padding", ctypes.c_int), ("maxSideLen", ctypes.c_int),
        ("boxScoreThresh", ctypes.c_float), ("boxThresh", ctypes.c_float),
        ("unClipRatio", ctypes.c_float), ("doAngle", ctypes.c_int),
        ("mostAngle", ctypes.c_int),
    ]
param = OCR_PARAM(50, 960, 0.5, 0.3, 1.5, 1, 1)

# 识别（内存字节传入）
img_buf = (ctypes.c_ubyte * len(img_bytes))(*img_bytes)
dll.OcrDetectMem(handle, img_buf, len(img_bytes), ctypes.byref(param))

# 读取结果
length = dll.OcrGetLen(handle)
buf = ctypes.create_string_buffer(length + 1)
dll.OcrGetResult(handle, buf, length + 1)
print(buf.value.decode("utf-8"))

# 销毁
dll.OcrDestroy(handle)
```

### 2. 任意可调用 DLL 的语言

C# / Java (JNI) / Delphi / VB / Rust 等均可调用。完整 C API 见仓库 [`include/OcrLiteCApi.h`](https://github.com/***********/RapidOcrEmbed/blob/main/include/OcrLiteCApi.h)。

---

## 🔌 C API 主要接口

| 函数 | 作用 |
|------|------|
| `OcrInitEmbedded(nThreads)` | 使用**内嵌模型**创建句柄（无需文件路径） |
| `OcrInit(det, cls, rec, keys, nThreads)` | 从磁盘加载模型创建句柄 |
| `OcrDetect(handle, dir, name, param)` | 按**文件路径**识别 |
| `OcrDetectMem(handle, bytes, size, param)` | 按**内存字节**识别 |
| `OcrGetLen / OcrGetResult` | 读取拼接后的纯文本结果 |
| `OcrDetectEx / OcrDetectMemEx` | 识别并保留**逐文本块**结果 |
| `OcrGetBlockCount` / `OcrGetBlockText` / `OcrGetBlockScore` / `OcrGetBlockBox` / `OcrGetBlockAngle` / `OcrGetBlockCharScores` | 查询每个文本块详细信息 |
| `OcrDestroy(handle)` | 释放句柄 |

---

## 🆚 相比上游 RapidOcrOnnx 的改动

| 维度 | 上游 (RapidOcrOnnx) | 本项目 (RapidOcrEmbed) |
|------|---------------------|------------------------|
| 模型 | PP-OCRv2/v3 | **PP-OCRv6** (det+rec) + PP-LCNet 分类器 |
| 方向分类 | 旧分类器 | PP-LCNet_x1_0_textline_ori，[1,3,80,160] 输入 |
| 分发方式 | DLL + 外部 `.onnx` 文件 | **单文件 DLL**（模型内嵌） |
| 识别字典 | `ppocr_keys` 加 `#`/空格 | `ppocr_keys_v6.txt`，首行 `blank` |
| Windows 路径 | 仅 ANSI | **UTF-8 / 宽字符**，支持中文 |
| 链接方式 | 动态依赖 | **静态链接**，CRT `/MT` |
| C API | 基础 | 扩展：`OcrInitEmbedded`、`OcrDetectMem`、逐块查询 |
| 模型规格 | — | tiny / small / medium 三档 |

---

## 📊 推荐参数

```c
OCR_PARAM param = {
    50,     // padding        — 边缘填充像素
    960,    // maxSideLen     — 长边缩放上限（越大越精，越慢）
    0.5f,   // boxScoreThresh — 文本框得分阈值
    0.3f,   // boxThresh      — 二值化阈值
    1.5f,   // unClipRatio    — 文本框扩张比例
    1,      // doAngle        — 1=启用方向分类
    1,      // mostAngle      — 1=...
};
```

- **速度优先**：`maxSideLen=736`，`small` 模型
- **精度优先**：`maxSideLen=1280`，`medium` 模型
- **倒置文本多**：务必 `doAngle=1`

---

## 🙏 致谢

- **[RapidAI/RapidOcrOnnx](https://github.com/RapidAI/RapidOcrOnnx)** — 上游 C++ PP-OCR 运行时，本项目的基础。
- **[PaddlePaddle/PaddleOCR](https://github.com/PaddlePaddle/PaddleOCR)** — PP-OCRv6 与 PP-LCNet 模型。
- **[Microsoft/onnxruntime](https://github.com/microsoft/onnxruntime)** — ONNX 推理引擎。
- **[OpenCV](https://opencv.org/)** — 图像处理。

完整第三方组件清单见 [`NOTICE`](https://github.com/***********/RapidOcrEmbed/blob/main/NOTICE)。

---

## ⚠️ 已知限制

- 目前仅提供 **Windows x64** 预编译 DLL（Linux/macOS 需自行编译）。
- DLL 文件名仍为 `RapidOcrOnnx_*.dll`（保留上游兼容性，非品牌名）。
- 仅测试 **CPU** 推理（`OCR_ONNX=CPU`）；CUDA 模式代码保留但未充分验证。

## 📝 反馈

发现问题或建议请提交 [Issue](https://github.com/***********/RapidOcrEmbed/issues)。

---

## 📎 Attachments（要上传到这个 Release 的文件）

在 GitHub Release 页面"Attach binaries by dropping them here"处上传：

1. **`RapidOcrOnnx_small.dll`** (57 MB) — ⭐ 主推
2. **`RapidOcrOnnx_tiny.dll`** (33 MB)
3. **`RapidOcrOnnx_medium.dll`** (159 MB)
4. （可选）`RapidOcrEmbed-deps-win-x64.zip` — 静态依赖包（供他人自行编译）
