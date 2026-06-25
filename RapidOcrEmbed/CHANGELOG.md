# Changelog — RapidOcrEmbed

All notable changes to this project are documented in this file.
This project (**RapidOcrEmbed**) is a derivative of [RapidAI/RapidOcrOnnx](https://github.com/RapidAI/RapidOcrOnnx).

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

## [1.0.0] - 2026

### Added
- **PP-OCRv6 model support** — detection (det) + recognition (rec) upgraded
  from the upstream PP-OCRv2/v3 to PaddleOCR **PP-OCRv6**.
- **Embedded-models build mode** (`OCR_EMBEDDED_MODELS=ON`):
  - New `build.py` and `模型转二进制/embed_models.py` convert `.onnx` models
    and the recognition dictionary into C headers under `include/models/`.
  - Models are compiled directly into the DLL/LIB → **single-file distribution**,
    no external model files needed at runtime.
  - New `ModelLoader` class with `createSessionFromMemory` /
    `loadKeysFromMemory`, and `OcrLite::initModels()` overload for embedded use.
- **PP-LCNet text-orientation classifier** (`PP-LCNet_x1_0_textline_ori`):
  - Input `[1,3,80,160]`, ImageNet normalization, output 0°/180°.
  - `AngleNet` rewritten to feed the new model and auto-rotate 180° text lines.
- **Extended C API** in `OcrLiteCApi.h`:
  - `OcrInitEmbedded` — create a handle with no model paths.
  - `OcrDetectMem` / `OcrDetectMemEx` — recognize from **in-memory image bytes**.
  - Block-level getters: `OcrGetBlockCount`, `OcrGetBlockText`,
    `OcrGetBlockScore`, `OcrGetBlockBox`, `OcrGetBlockCharScores`,
    `OcrGetBlockAngle`, `OcrGetBlockAngleScore`.
- **Three model sizes**: `tiny`, `small`, `medium`, selectable at build time.
- Pre-built Windows x64 DLLs (static CRT, statically linked ONNX Runtime + OpenCV)
  distributed via GitHub Releases.
- Python ctypes test scripts: `测试/test_embedded.py`, `测试/test_dll_full.py`.
- Reference Python inference script `模型转二进制/run_ocr.py` for debugging.
- This `README.md`, `NOTICE`, `CHANGELOG.md`, and `.gitignore`.

### Changed
- **Recognition dictionary**: now `ppocr_keys_v6.txt` with `blank` as the first
  line; `CrnnNet` no longer inserts `#` at the start or appends a trailing space.
- **Windows file I/O**: switched to UTF-8 / wide-character APIs
  (`CreateFileW`, `MultiByteToWideChar`, `_wfopen`) so **Chinese paths work**.
- **Build**: statically linked ONNX Runtime + OpenCV, static CRT (`/MT`) via
  `OcrCRTLinkage.cmake`; `add_compile_options(/permissive)` for ONNX Runtime 1.26
  header compatibility.
- Updated CMake target set: `BIN` / `CLIB` (DLL+LIB) / `JNI` outputs.

### Notes
- `onnxruntime-static/`, `opencv-static/`, `include/models/`, `output/`,
  and raw `.onnx` model files are intentionally excluded from git (size / licensing).
  Download them from Releases or generate locally.

[Unreleased]: ../../compare/HEAD...HEAD
[1.0.0]: ../../releases/tag/v1.0.0
