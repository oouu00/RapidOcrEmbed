# 依赖库编译说明 (Dependency Build Guide)

本文档说明如何获取和替换本项目的静态依赖库，以及如何从源码编译整个项目。

---

## 目录

- [编译环境](#编译环境)
- [Qt 5.15.2 静态库](#qt-5152-静态库)
- [ONNX Runtime 静态库](#onnx-runtime-静态库)
- [OpenCV 静态库](#opencv-静态库)
- [MNN 推理引擎（可选）](#mnn-推理引擎可选)
- [完整编译流程](#完整编译流程)

---

## 编译环境

| 工具 | 版本要求 | 说明 |
|------|---------|------|
| **Visual Studio** | 2019 (v142) 或更高 | 需要 C++ 工作负载 |
| **CMake** | ≥ 3.17 | 构建系统 |
| **Python** | 3.8+ | 需要安装 `pyyaml` 模块 |
| **Windows SDK** | 10.0+ | 随 VS 安装 |

VS2019 下载地址：https://pan.baidu.com/s/1KvbzItJbrHqf51KVIiBPFw 提取码: `849s`

---

## Qt 5.15.2 静态库

### 官方源码下载

- Qt 官网：https://www.qt.io/download-qt-installer
- Qt 5.15.2 源码：https://download.qt.io/official_releases/qt/5.15/5.15.2/single/

### 自行编译 Qt 静态库

```bash
# 1. 解压 Qt 源码
# 2. 打开 Qt 5.15.2 MSVC 2019 x64 命令提示符
# 3. 配置静态编译
configure -static -release -opensource -confirm-license -nomake examples -nomake tests

# 4. 编译
nmake

# 5. 编译完成后，静态库位于：
#    qt-everywhere-src-5.15.2/qtbase/lib/
```

### 替换本地 Qt 静态库

本项目附带的 Qt 静态库压缩包（`Qt5静态库放c盘根目录.7z.001` + `.002`）仅为方便编译。若需自行编译替换：

1. 从 Qt 官网下载源码并按上述步骤编译
2. 将编译产物复制到 `C:\Qt5静态库\` 目录（与压缩包解压路径一致）
3. 修改 `ocr-qt-cpp/ocr-qt-cpp.pro` 中的 `QT_BASE_PATH` 指向你的编译目录
4. 重新编译 OcrQtCpp

### LGPL 合规说明

Qt 5.15.2 采用 LGPLv3 许可证。本项目完整开源源码，用户可自行下载 Qt 源码、编译替换静态库，满足 LGPL "用户可修改并重链接程序"的核心义务。完整 LGPL 协议文本见 `LICENSES/LGPLv3.txt`。

---

## ONNX Runtime 静态库

### 官方源码下载

- GitHub：https://github.com/microsoft/onnxruntime
- Release 页面：https://github.com/microsoft/onnxruntime/releases
- 推荐版本：v1.16.x 或更高

### 自行编译静态库

```bash
# 1. 克隆源码
git clone --recursive https://github.com/microsoft/onnxruntime.git
cd onnxruntime

# 2. 编译静态库 (Windows x64)
./build.bat --config Release --use_static_lib --build_shared_lib --enable_ms_experimental_static_cxx \
  --cmake_generator "Visual Studio 16 2019" --build_wheel

# 3. 输出位于：
#    build/Windows/Release/Release/onnxruntime.lib
```

### 替换静态库

将编译产物放入 `RapidOcrEmbed/onnxruntime-static/` 目录：

```
RapidOcrEmbed/onnxruntime-static/
├── include/          # ONNX Runtime 头文件
└── lib/
    └── onnxruntime.lib  # 静态库
```

---

## OpenCV 静态库

### 官方源码下载

- GitHub：https://github.com/opencv/opencv
- Release 页面：https://github.com/opencv/opencv/releases
- 推荐版本：4.8.x 或更高

### 自行编译静态库

```bash
# 1. 克隆源码
git clone https://github.com/opencv/opencv.git
cd opencv

# 2. CMake 配置 (静态编译)
cmake -B build -G "Visual Studio 16 2019" -A x64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DBUILD_opencv_world=ON ^
  -DBUILD_opencv_core=ON ^
  -DBUILD_opencv_imgproc=ON ^
  -DBUILD_opencv_imgcodecs=ON ^
  -DBUILD_opencv_highgui=OFF ^
  -DBUILD_opencv_video=OFF ^
  -DBUILD_opencv_videoio=OFF ^
  -DBUILD_opencv_calib3d=OFF ^
  -DBUILD_opencv_features2d=OFF ^
  -DBUILD_opencv_flann=OFF ^
  -DBUILD_opencv_ml=OFF ^
  -DBUILD_opencv_objdetect=OFF ^
  -DBUILD_opencv_photo=OFF ^
  -DBUILD_opencv_stitching=OFF ^
  -DBUILD_opencv_dnn=OFF ^
  -DBUILD_opencv_python=OFF ^
  -DBUILD_opencv_java=OFF ^
  -DBUILD_opencv_tests=OFF ^
  -DBUILD_opencv_perf_tests=OFF ^
  -DBUILD_opencv_apps=OFF

# 3. 编译
cmake --build build --config Release

# 4. 输出位于：
#    build/lib/Release/opencv_world480.lib
```

### 替换静态库

将编译产物放入 `RapidOcrEmbed/opencv-static/` 目录：

```
RapidOcrEmbed/opencv-static/
├── include/          # OpenCV 头文件
└── lib/
    └── opencv_world480.lib  # 静态库
```

---

## MNN 推理引擎（可选）

MNN 是阿里巴巴开源的轻量级推理引擎，作为 ONNX Runtime 的备选方案。

### 官方源码下载

- GitHub：https://github.com/alibaba/MNN
- Release 页面：https://github.com/alibaba/MNN/releases

### 替换静态库

将压缩包 `RapidOcrEmbed/压缩包解压后到当前目录/MNN_lib.rar` 解压到 `RapidOcrEmbed/` 目录即可。

---

## 完整编译流程

### 1. RapidOcrEmbed（OCR 核心库）

```bash
# 解压静态依赖
cd RapidOcrEmbed
7z x "压缩包解压后到当前目录/onnxruntime-static.rar" -o.
7z x "压缩包解压后到当前目录/opencv-static.rar" -o.

# （可选）生成内嵌模型头文件
python "模型转二进制/embed_models.py"

# 编译 tiny/small/medium 三个版本
python build.py

# 输出：
#   output/RapidOcrOnnx_tiny.dll
#   output/RapidOcrOnnx_small.dll
#   output/RapidOcrOnnx_medium.dll
#   output/RapidOcrOnnxStatil.lib (静态库)
```

### 2. OcrQtCpp（Qt 应用）

```bash
# 解压 Qt 静态库（如果尚未解压）
7z x "Qt5静态库放c盘根目录.7z.001" -oC:\

# 解压 OcrQtCpp 依赖库
cd ocr-qt-cpp
7z x "放到ocr-qt-cpp的libs.rar" -olibs

# 确保 libs/ 目录中包含：
#   - RapidOcrOnnxStatil.lib (来自 RapidOcrEmbed 编译输出)
#   - Qt 静态库 (来自压缩包)

# 编译
python build_msvc.py --clean

# 输出：
#   release/OcrQtCpp.exe
```

### 3. CMake 选项参考（RapidOcrEmbed）

| 选项 | 取值 | 默认 | 说明 |
|------|------|------|------|
| `OCR_OUTPUT` | `BIN` / `CLIB` / `JNI` | `BIN` | 可执行程序 / DLL+LIB / Java 绑定 |
| `OCR_ONNX` | `CPU` / `CUDA` | `CPU` | 推理后端 |
| `OCR_EMBEDDED_MODELS` | `ON` / `OFF` | `OFF` | 模型内嵌到二进制 |
| `OCR_BUILD_CRT` | `True` / `False` | — | 静态 CRT `/MT` |
| `OCR_BENCHMARK` | `ON` / `OFF` | `ON` | 编译 benchmark 工具 |

---

## 附录：第三方库源码地址汇总

| 库 | 源码地址 | 许可证 |
|----|---------|--------|
| Qt 5.15.2 | https://download.qt.io/official_releases/qt/5.15/5.15.2/single/ | LGPLv3 |
| ONNX Runtime | https://github.com/microsoft/onnxruntime | MIT |
| OpenCV | https://github.com/opencv/opencv | Apache 2.0 |
| MNN | https://github.com/alibaba/MNN | Apache 2.0 |
| PaddleOCR | https://github.com/PaddlePaddle/PaddleOCR | PaddlePaddle |
