# OcrQtCpp

基于 Qt 和 RapidOCR 的跨平台 OCR 应用，支持 GUI、CLI 和 HTTP 接口。

## 项目简介

OcrQtCpp 是一个功能完整的 OCR 识别工具，集成了百度飞桨的 PP-OCRv6 模型和 Umi-OCR 的 TBPU 排版算法，提供:
- **多模式**: GUI 图形界面、命令行工具、HTTP 服务器、截图识别
- **表格识别**: SLANet 深度学习模型 / img2table 纯 OpenCV 两种模式
- **智能排版**: 8 种排版策略，保留文档原始阅读顺序
- **TextClick**: 基于 OCR 的文字点击自动化，可用于 UI 测试和自动化操作
- **单文件部署**: 编译产物为单个 ~86MB 的可执行文件，无需额外依赖，支持进一步量化减少体积。

## 版权声明

**OcrQtCpp Project**
- Copyright (c) 2024 OcrQtCpp Project Team
- License: MIT License
- GitHub: [https://github.com/your-username/ocr-qt-cpp](https://github.com/your-username/ocr-qt-cpp)

**TBPU Text Block Processing Layout Code**
- Source Project: Umi-OCR
- Author: hiroi-sora
- Repository: [https://github.com/hiroi-sora/Umi-OCR](https://github.com/hiroi-sora/Umi-OCR)
- Copyright (c) 2023 hiroi-sora
- License: MIT License

**img2table C++ Port (Table Detection)**
- Source Project: xavctn/img2table
- Author: xavctn
- Repository: [https://github.com/xavctn/img2table](https://github.com/xavctn/img2table)
- Copyright (c) 2023 xavctn
- License: Apache License 2.0
- 说明: 本项目中的 C++ 版本为 Python 版本的重写，保留原版 Apache 2.0 许可证。

**MinerU Table Layout Algorithm**
- Source Project: MinerU
- Author: OpenDataLab
- Repository: [https://github.com/opendatalab/MinerU](https://github.com/opendatalab/MinerU)
- Copyright (c) 2024 OpenDataLab
- License: AGPL-3.0 License
- 说明: 表格排版算法参考 MinerU 项目的表格识别与排版策略。

**k2pdfopt Sorting Algorithm Reference**
- Source Project: k2pdfopt
- Author: willus.com
- Repository: [http://willus.com/k2pdfopt/](http://willus.com/k2pdfopt/)
- 说明: TBPU 排版算法参考了 k2pdfopt 项目的排序算法。

## 开源协议

本项目整体采用 **MIT 许可证**，允许自由使用、修改和分发，无论商业或非商业用途。

### 第三方代码说明

项目内 `TbpuLayout.cpp` / `TbpuLayout.h` 为移植自 Umi-OCR 的 TBPU 排版算法代码，同样遵循 MIT 协议：
- 项目：Umi-OCR
- 作者：hiroi-sora
- 版权 Copyright (c) 2023 hiroi-sora
- 仓库地址：[https://github.com/hiroi-sora/Umi-OCR](https://github.com/hiroi-sora/Umi-OCR)
- 说明：源码文件内已完整保留原版 MIT 版权与许可声明，详见根目录 NOTICE 文件。

项目内表格检测功能参考了以下开源项目：
- **img2table**: C++ 重写的表格边界检测算法，原始 Python 版本来自 xavctn/img2table
  - 许可证：Apache License 2.0
  - 仓库地址：[https://github.com/xavctn/img2table](https://github.com/xavctn/img2table)
- **MinerU**: 表格排版算法参考 OpenDataLab 的 MinerU 项目
  - 许可证：AGPL-3.0
  - 仓库地址：[https://github.com/opendatalab/MinerU](https://github.com/opendatalab/MinerU)

## 功能特性

- **多模式支持**: GUI 界面、命令行工具、HTTP 服务器
- **表格识别**: 三种模式可选
  - **SLANet (ONNX模型)**: 基于深度学习的表格结构识别，支持有线框和无线框表格
  - **img2table (纯OpenCV)**: 基于边缘检测的表格提取，无需模型，仅识别有线框表格
  - **关闭**: 不进行表格识别
  - 表格排版算法参考 MinerU (https://github.com/opendatalab/MinerU)
- **排版算法**: 集成 Umi-OCR 的 TBPU (Text Block Processing Unit) 排版算法
- **8种排版策略**:
  - 多栏-自然段 (推荐文档)
  - 多栏-每行换行
  - 多栏-无换行 (连续文本)
  - 单栏-自然段
  - 单栏-每行换行
  - 单栏-无换行 (连续文本)
  - 单栏-代码段 (保留缩进)
  - 不处理
- **坐标模式**: 显示文本块坐标、置信度、角度信息
- **快捷键支持**: 全局快捷键快速截图识别
- **自动保存/复制**: 自动保存识别结果到文件或复制到剪贴板
- **网页界面**: 内置 HTTP 服务器，提供网页端 OCR 服务
- **TextClick**: 基于 OCR 的文字点击自动化功能

## 编译运行

### 依赖

- Qt 5.15.2+ (静态编译)
- MSVC 2019+ (Windows)
- RapidOcrEmbed (预编译静态库)

### 编译命令

```bash
python 1.build_msvc.py --clean
```

## 使用说明

### 运行模式

```
OcrQtCpp.exe [选项] [图片路径或文件夹...]
```

#### 1. GUI 图形界面模式 (默认)
```bash
OcrQtCpp.exe
```
- 打开图形界面，支持截图/文件识别
- 右下角地球图标可开启 HTTP 服务
- 支持排版模式切换和坐标显示
- **单实例限制**: GUI 模式下禁止多开，避免资源冲突

#### 2. 命令行模式
```bash
OcrQtCpp.exe --cli <路径...>
```
- 识别图片，输出 JSON 后退出
- 支持多文件和文件夹递归扫描
- **多实例支持**: 允许同时运行多个 CLI 任务

#### 3. 截图模式
```bash
OcrQtCpp.exe --shot              # 弹出截图框框选区域
OcrQtCpp.exe --shot --region 100,100,800,600  # 指定区域 (x,y,w,h)
```
- 弹出截图框框选屏幕区域，识别后输出 JSON 退出
- 支持 `--region` 指定截图区域，无需弹窗
- **多实例支持**: 允许同时运行多个截图任务

#### 4. HTTP 服务模式
```bash
OcrQtCpp.exe --server          # 默认端口 8080
OcrQtCpp.exe --port 9000       # 指定端口
```
- 启动 HTTP 服务，无界面运行 (headless)
- 浏览器访问 http://localhost:端口 即可使用网页端界面
- **多实例支持**: 允许启动多个不同端口的服务

### 通用选项

```
-h, --help            显示帮助信息
--coords              坐标模式: 输出带坐标的文本块
--layout              排版模式: TBPU 排版算法, 按人类阅读顺序排序文本块
--layout-strategy <策略> 排版策略 (默认 single_line):
                        none        - 不处理 (仅补换行)
                        multi_para  - 多栏-自然段 (推荐文档)
                        multi_line  - 多栏-每行换行
                        multi_none  - 多栏-无换行 (连续文本)
                        single_para - 单栏-自然段
                        single_line - 单栏-每行换行
                        single_none - 单栏-无换行 (连续文本)
                        single_code - 单栏-代码段 (保留缩进)
--table[=模式]       表格识别模式:
                        不加值或 --table=1  SLANet 模型 (需 ONNX)
                        --table=2          img2table 纯 OpenCV (无模型, 仅有线框表格)
--pdf[=路径]         生成可搜索 PDF:
                        --pdf              输出到同名 .pdf 文件
                        --pdf=out.pdf      指定输出路径
```

### 命令行示例

```bash
# 单文件识别
OcrQtCpp.exe --cli 图片.png

# 多文件识别
OcrQtCpp.exe --cli 图片1.png 图片2.jpg

# 文件夹递归识别
OcrQtCpp.exe --cli ./images/

# 坐标模式
OcrQtCpp.exe --cli --coords 图片.png

# 排版模式
OcrQtCpp.exe --cli --layout --layout-strategy single_para 图片.png

# 截图识别
OcrQtCpp.exe --shot

# 指定区域截图识别
OcrQtCpp.exe --shot --region 100,100,800,600

# 生成可搜索 PDF
OcrQtCpp.exe --cli --pdf 图片.png

# 表格识别 (SLANet 模型)
OcrQtCpp.exe --cli --table 图片.png

# 表格识别 (img2table 纯 OpenCV, 无需模型)
OcrQtCpp.exe --cli --table=2 图片.png
```

### HTTP 服务使用

#### 网页界面
访问 http://localhost:8080 即可使用网页端界面，支持：
- 拖拽/点击上传图片
- 多文件识别
- 实时显示识别结果
- 排版模式切换

#### API 接口

```bash
# 上传单个图片
curl -F "file=@图片.png" http://localhost:8080/ocr

# 上传多个图片
curl -F "file=@a.png" -F "file=@b.jpg" http://localhost:8080/ocr

# 坐标模式
curl -F "file=@图片.png" "http://localhost:8080/ocr?mode=coords"

# 排版模式
curl -F "file=@文档.png" "http://localhost:8080/ocr?layout=1&layoutStrategy=single_line"

# 原始图片二进制
curl --data-binary @图片.png http://localhost:8080/ocr-raw

# 触发服务器端截图
curl -X POST http://localhost:8080/screenshot

# 表格识别 (SLANet)
curl -F "file=@表格.png" "http://localhost:8080/ocr?table=1"

# 表格识别 (img2table)
curl -F "file=@表格.png" "http://localhost:8080/ocr?table=2"
```

#### 返回格式

```json
// 普通模式
{"code":0,"text":"识别出的全部文本","blockCount":3}

// 坐标模式
{"code":0,"text":"全部文本","blockCount":3,"blocks":[
  {"text":"第一块","score":0.88,
   "box":[x1,y1,x2,y2,x3,y3,x4,y4],"angle":0,"angleScore":0.99,
   "end":"\n"}
]}

// 表格模式
{"code":0,"type":"table","html":"<table style='...'>...</table>","text":"表格文字","structureScore":0.95}
```

### TextClick 文字点击模式

基于 OCR 的文字点击自动化功能：

```bash
# 获取区域文本
OcrQtCpp.exe --tc -get

# 单击文字位置
OcrQtCpp.exe --tc -click "确定"

# 双击文字位置
OcrQtCpp.exe --tc -double "提交"

# 右击文字位置
OcrQtCpp.exe --tc -right "菜单"

# 移动鼠标到文字位置
OcrQtCpp.exe --tc -move "按钮"

# 检查是否包含文字
OcrQtCpp.exe --tc -check "登录"

# 获取文字坐标
OcrQtCpp.exe --tc -pos "标题"

# 列出所有文字块
OcrQtCpp.exe --tc -list
```

#### 附加参数

```
-n <数字>/-第几个       第几个匹配 (默认 1)
-r <x,y,w,h>/-区域      屏幕区域 (默认全屏)
-loc <位置>/-位置       center/topleft/topright/bottomleft/bottomright
                        中心/左上/右上/左下/右下 (默认 center)
-output <路径>/-输出    保存带框标注的图片
-image <路径>           识别指定图片 (不截屏)
```

#### HTTP 接口

```bash
curl -X POST http://localhost:8080/textclick \
  -H "Content-Type: application/json" \
  -d '{"action":"click","text":"确定"}'
```

## 项目结构

```
ocr-qt-cpp/
├── MainWindow.cpp/h      # GUI 主窗口
├── OCRWrapper.cpp/h      # OCR 封装
├── TbpuLayout.cpp/h      # TBPU 排版算法 (移植自 Umi-OCR，含原版版权声明)
├── CliRunner.cpp/h       # 命令行工具
├── OcrHttpServer.cpp/h   # HTTP 服务器
├── TextClickCli.cpp/h    # TextClick 命令行
├── TextClickEngine.cpp/h # TextClick 核心引擎
├── webui.h               # 网页端界面
├── 1.build_msvc.py       # 编译脚本
├── LICENSE               # 许可证 (包含项目自身与 Umi-OCR 的版权声明)
├── NOTICE                # 第三方代码声明
└── README.md             # 项目文档
```

## 致谢

- 感谢百度飞桨提供的 PP-OCRv6 模型
- 感谢 hiroi-sora 开发的 Umi-OCR TBPU 排版算法
- 感谢 Qt 框架提供的跨平台支持

## 贡献

欢迎提交 Issue 和 Pull Request！

## 联系方式

如有问题或建议，请通过以下方式联系：
- GitHub Issues: [https://github.com/your-username/ocr-qt-cpp/issues](https://github.com/your-username/ocr-qt-cpp/issues)
- 邮件: your-email@example.com
