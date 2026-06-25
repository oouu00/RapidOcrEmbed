# -*- coding: utf-8 -*-
"""
RapidOcrMnn 编译脚本
- 静态 CRT (mt)
- 嵌入模型到 DLL (OCR: MNN格式, Table: ORT格式)
- CPU 版本
- Release 编译
- x64
- 支持指定版本编译: tiny, small, medium

用法:
    python build.py                    # 编译所有版本 (FP32)
    python build.py small              # 只编译small版本
    python build.py tiny medium        # 编译tiny和medium版本
    python build.py small --quant fp16 # small版本, MNN FP16量化
    python build.py --quant int8       # 所有版本, MNN INT8权重量化
    python build.py --quant none       # 不量化 (默认)
"""
import os
import subprocess
import sys
import shutil

# 模型版本配置
MODEL_VERSIONS = {
    "tiny": {
        "det": "PP-OCRv6_tiny_det_onnx/PP-OCRv6_tiny_det_onnx.onnx",
        "rec": "PP-OCRv6_tiny_rec_onnx/PP-OCRv6_tiny_rec_onnx.onnx",
        "rec_config": "PP-OCRv6_tiny_rec_onnx/PP-OCRv6_tiny_rec_onnx.yml",
    },
    "small": {
        "det": "PP-OCRv6_small_det_onnx/PP-OCRv6_small_det_onnx.onnx",
        "rec": "PP-OCRv6_small_rec_onnx/PP-OCRv6_small_rec_onnx.onnx",
        "rec_config": "PP-OCRv6_small_rec_onnx/PP-OCRv6_small_rec_onnx.yml",
    },
    "medium": {
        "det": "PP-OCRv6_medium_det_onnx/PP-OCRv6_medium_det_onnx.onnx",
        "rec": "PP-OCRv6_medium_rec_onnx/PP-OCRv6_medium_rec_onnx.onnx",
        "rec_config": "PP-OCRv6_medium_rec_onnx/PP-OCRv6_medium_rec_onnx.yml",
    },
}

# 共用的文本方向模型
CLS_MODEL = "PP-LCNet_x1_0_textline_ori_infer.onnx"
# 图片方向分类模型
IMG_ORI_MODEL = "ch_ppocr_mobile_v2.0_cls_train.onnx"
KEYS_FILE = "ppocr_keys_v6.txt"
# 表格识别模型 (保持ORT格式)
TABLE_MODEL = "SLANet_plus_onnx/SLANet_plus.onnx"
TABLE_CONFIG = "SLANet_plus_onnx/SLANet_plus.yml"
TABLE_KEYS_FILE = "slanet_keys.txt"

# MNN转换器路径 (可通过环境变量 MNN_CONVERTER 指定)
MNN_CONVERTER = os.environ.get("MNN_CONVERTER", "mnnconvert")

def find_mnn_converter():
    """查找mnnconvert工具"""
    # 1. 环境变量
    if os.path.isfile(MNN_CONVERTER):
        return MNN_CONVERTER
    # 2. MNN_lib 目录
    mnn_lib_exe = os.path.join(os.path.dirname(os.path.abspath(__file__)), "MNN_lib", "mnnconvert.exe")
    if os.path.isfile(mnn_lib_exe):
        return mnn_lib_exe
    # 3. PATH中查找
    for dir_path in os.environ.get("PATH", "").split(os.pathsep):
        exe = os.path.join(dir_path, "mnnconvert.exe")
        if os.path.isfile(exe):
            return exe
        exe = os.path.join(dir_path, "mnnconvert")
        if os.path.isfile(exe):
            return exe
    # 4. Python MNN包 (pip install mnn)
    try:
        import MNN.tools.mnnconvert
        return "python_mnn"
    except ImportError:
        pass
    return None


def model_to_header(input_path, output_path, key):
    """将模型文件转换为C头文件 (流式写入，低内存)"""
    var_name = f"{key}_model"
    if key == 'keys':
        var_name = "keys_data"

    chunk_size = 4096

    with open(output_path, 'w', encoding='utf-8') as f:
        if key == 'keys':
            with open(input_path, 'r', encoding='utf-8') as tf:
                text_content = tf.read()
            data = ("blank\n" + text_content).encode('utf-8')
            total = len(data)
            f.write(f'// Auto-generated from {os.path.basename(input_path)}\n')
            f.write(f'// Model size: {total} bytes\n\n')
            f.write(f'#ifndef __{var_name.upper()}_H__\n')
            f.write(f'#define __{var_name.upper()}_H__\n\n')
            f.write(f'const unsigned char {var_name}[] = {{\n')
            for i in range(0, total, 16):
                chunk = data[i:i+16]
                hex_values = ', '.join(f'0x{b:02x}' for b in chunk)
                f.write(f'    {hex_values},\n')
            f.write(f'}};\n\n')
            f.write(f'const size_t {var_name}_size = {total};\n\n')
            f.write(f'#endif //__{var_name.upper()}_H__\n')
        else:
            total = os.path.getsize(input_path)
            f.write(f'// Auto-generated from {os.path.basename(input_path)}\n')
            f.write(f'// Model size: {total} bytes\n\n')
            f.write(f'#ifndef __{var_name.upper()}_H__\n')
            f.write(f'#define __{var_name.upper()}_H__\n\n')
            f.write(f'const unsigned char {var_name}[] = {{\n')
            with open(input_path, 'rb') as rf:
                offset = 0
                while offset < total:
                    buf = rf.read(chunk_size)
                    if not buf:
                        break
                    for i in range(0, len(buf), 16):
                        line = buf[i:i+16]
                        hex_values = ', '.join(f'0x{b:02x}' for b in line)
                        f.write(f'    {hex_values},\n')
                    offset += len(buf)
            f.write(f'}};\n\n')
            f.write(f'const size_t {var_name}_size = {total};\n\n')
            f.write(f'#endif //__{var_name.upper()}_H__\n')

    print(f'Generated: {output_path} ({total / 1024:.1f} KB)')
    return total


def convert_to_mnn(onnx_path, converter_path, quant="none"):
    """将 ONNX 模型转换为 MNN 格式
    
    quant: 量化选项
        "none" - FP32 (默认)
        "fp16" - FP16 权重存储, 体积减半
        "int8" - INT8 权重量化, 体积缩减74%
    """
    suffix = {"none": "", "fp16": "_fp16", "int8": "_int8w"}
    mnn_path = os.path.splitext(onnx_path)[0] + suffix.get(quant, "") + '.mnn'
    if not os.path.exists(onnx_path):
        return onnx_path
    if os.path.exists(mnn_path) and os.path.getmtime(mnn_path) >= os.path.getmtime(onnx_path):
        print(f'  MNN cache hit: {os.path.basename(mnn_path)}')
        return mnn_path
    
    quant_label = {"none": "FP32", "fp16": "FP16", "int8": "INT8w"}.get(quant, quant)
    print(f'  Converting {os.path.basename(onnx_path)} -> MNN ({quant_label})...')
    
    # 复制到临时目录避免中文路径问题
    tmp_dir = os.path.join(os.environ.get("TEMP", os.path.dirname(onnx_path)), "mnn_convert")
    os.makedirs(tmp_dir, exist_ok=True)
    tmp_onnx = os.path.join(tmp_dir, os.path.basename(onnx_path))
    tmp_mnn = os.path.splitext(tmp_onnx)[0] + '.mnn'
    shutil.copy2(onnx_path, tmp_onnx)
    
    if converter_path == "python_mnn":
        cmd = [sys.executable, "-m", "MNN.tools.mnnconvert",
               "-f", "ONNX", "--modelFile", tmp_onnx, "--MNNModel", tmp_mnn, "--bizCode", "biz"]
    else:
        cmd = [converter_path, "-f", "ONNX", "--modelFile", tmp_onnx, "--MNNModel", tmp_mnn, "--bizCode", "biz"]
    
    if quant == "fp16":
        cmd.append("--fp16")
    elif quant == "int8":
        cmd.extend(["--weightQuantBits", "8"])
    
    result = subprocess.run(cmd, capture_output=True, text=True)
    if os.path.exists(tmp_mnn):
        shutil.copy2(tmp_mnn, mnn_path)
        onnx_mb = os.path.getsize(onnx_path) / 1024 / 1024
        mnn_mb = os.path.getsize(mnn_path) / 1024 / 1024
        print(f'  MNN: {onnx_mb:.1f}MB -> {mnn_mb:.1f}MB ({quant_label})')
        # 清理临时文件
        os.remove(tmp_onnx)
        os.remove(tmp_mnn)
        return mnn_path
    else:
        print(f'  MNN conversion failed: {result.stderr[:300]}')
        return onnx_path


def convert_to_ort(onnx_path):
    """将 ONNX 模型转换为 ORT 格式（仅用于表格模型）"""
    ort_path = os.path.splitext(onnx_path)[0] + '.ort'
    if not os.path.exists(onnx_path):
        return onnx_path
    if os.path.exists(ort_path) and os.path.getmtime(ort_path) >= os.path.getmtime(onnx_path):
        print(f'  ORT cache hit: {os.path.basename(ort_path)}')
        return ort_path
    print(f'  Converting {os.path.basename(onnx_path)} -> ORT format...')
    result = subprocess.run([
        sys.executable, '-m', 'onnxruntime.tools.convert_onnx_models_to_ort',
        onnx_path, '--optimization_style', 'Fixed',
        '--output_dir', os.path.dirname(onnx_path)
    ], capture_output=True, text=True)
    if result.returncode == 0 and os.path.exists(ort_path):
        onnx_mb = os.path.getsize(onnx_path) / 1024 / 1024
        ort_mb = os.path.getsize(ort_path) / 1024 / 1024
        print(f'  ORT: {onnx_mb:.1f}MB -> {ort_mb:.1f}MB')
        return ort_path
    else:
        print(f'  ORT conversion failed, using ONNX: {result.stderr[:200]}')
        return onnx_path


def generate_embedded_models(version, models_dir, output_dir, quant="none"):
    """生成嵌入式模型头文件
    
    quant: MNN量化选项 "none" | "fp16" | "int8"
    """
    import yaml
    
    config = MODEL_VERSIONS[version]
    
    # 清理输出目录
    if os.path.exists(output_dir):
        for f in os.listdir(output_dir):
            if f.endswith('.h'):
                os.remove(os.path.join(output_dir, f))
    
    os.makedirs(output_dir, exist_ok=True)
    
    total_size = 0
    
    # 查找MNN转换器
    converter_path = find_mnn_converter()
    if not converter_path:
        print("ERROR: mnnconvert not found! Set MNN_CONVERTER env var or add to PATH.")
        sys.exit(1)
    print(f'Using MNN converter: {converter_path}')
    
    # det模型 (MNN格式)
    det_path = os.path.join(models_dir, config["det"])
    if os.path.exists(det_path):
        det_path = convert_to_mnn(det_path, converter_path, quant)
        size = model_to_header(det_path, os.path.join(output_dir, "model_det.h"), "det")
        total_size += size
    
    # cls模型 (MNN格式)
    cls_path = os.path.join(models_dir, CLS_MODEL)
    if os.path.exists(cls_path):
        cls_path = convert_to_mnn(cls_path, converter_path, quant)
        size = model_to_header(cls_path, os.path.join(output_dir, "model_cls.h"), "cls")
        total_size += size
    
    # 图片方向分类模型 (MNN格式)
    img_ori_path = os.path.join(models_dir, IMG_ORI_MODEL)
    if os.path.exists(img_ori_path):
        img_ori_path = convert_to_mnn(img_ori_path, converter_path, quant)
        size = model_to_header(img_ori_path, os.path.join(output_dir, "model_img_ori.h"), "img_ori")
        total_size += size
    
    # rec模型 (MNN格式)
    rec_path = os.path.join(models_dir, config["rec"])
    if os.path.exists(rec_path):
        rec_path = convert_to_mnn(rec_path, converter_path, quant)
        size = model_to_header(rec_path, os.path.join(output_dir, "model_rec.h"), "rec")
        total_size += size
    
    # keys文件 - 强制从配置文件重新生成
    keys_path = os.path.join(models_dir, KEYS_FILE)
    rec_config_path = os.path.join(models_dir, config["rec_config"])
    if os.path.exists(rec_config_path):
        with open(rec_config_path, 'r', encoding='utf-8') as f:
            rec_config = yaml.safe_load(f)
        character_dict = rec_config['PostProcess']['character_dict']
        with open(keys_path, 'w', encoding='utf-8') as f:
            for char in character_dict:
                f.write(char + '\n')
        print(f'Generated keys file: {keys_path}')
        size = model_to_header(keys_path, os.path.join(output_dir, "model_keys.h"), "keys")
        total_size += size
    
    # 表格识别模型 (ONNX格式)
    table_model_path = os.path.join(models_dir, TABLE_MODEL)
    if os.path.exists(table_model_path):
        size = model_to_header(table_model_path, os.path.join(output_dir, "model_table.h"), "table")
        total_size += size
        print(f'Table model embedded (ONNX): {table_model_path}')
    
    # 表格字典文件 - 从配置文件生成
    table_config_path = os.path.join(models_dir, TABLE_CONFIG)
    table_keys_path = os.path.join(models_dir, TABLE_KEYS_FILE)
    if os.path.exists(table_config_path):
        with open(table_config_path, 'r', encoding='utf-8') as f:
            table_config = yaml.safe_load(f)
        table_dict = list(table_config['PostProcess']['character_dict'])
        if table_config['PostProcess'].get('merge_no_span_structure', True):
            if "<td></td>" not in table_dict:
                table_dict.append("<td></td>")
            if "<td>" in table_dict:
                table_dict.remove("<td>")
        with open(table_keys_path, 'w', encoding='utf-8') as f:
            for char in table_dict:
                f.write(char + '\n')
        print(f'Generated table keys file: {table_keys_path}')
        size = model_to_header(table_keys_path, os.path.join(output_dir, "model_table_keys.h"), "table_keys")
        total_size += size
    
    # 生成embedded_models.h
    models_header = os.path.join(output_dir, "embedded_models.h")
    with open(models_header, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated embedded models header\n')
        f.write(f'// PP-OCRv6 {version} models: OCR(MNN) + Table(ORT)\n')
        f.write(f'// Include this header to use embedded models\n\n')
        f.write('#ifndef _EMBEDDED_MODELS_H_\n')
        f.write('#define _EMBEDDED_MODELS_H_\n\n')
        f.write('#include "model_det.h"\n')
        f.write('#include "model_cls.h"\n')
        f.write('#include "model_img_ori.h"\n')
        f.write('#include "model_rec.h"\n')
        f.write('#include "model_keys.h"\n')
        f.write('#include "model_table.h"\n')
        f.write('#include "model_table_keys.h"\n\n')
        f.write('#endif //_EMBEDDED_MODELS_H_\n')
    
    print(f'Total embedded size: {total_size / 1024 / 1024:.2f} MB')
    return total_size


def build_version(version, base_dir, models_dir, quant="none"):
    """编译指定版本
    
    quant: MNN量化选项 "none" | "fp16" | "int8"
    """
    
    # 编译参数
    BUILD_TYPE = "Release"
    BUILD_OUTPUT = "CLIB"
    MT_ENABLED = "True"
    ONNX_TYPE = "CPU"
    EMBEDDED_MODELS = "ON"
    BUILD_CMAKE_T = "v142"  # VS 2019
    BUILD_CMAKE_A = "x64"
    
    # 生成嵌入式模型头文件
    output_dir = os.path.join(base_dir, "include", "models")
    quant_label = {"none": "FP32", "fp16": "FP16", "int8": "INT8w"}.get(quant, quant)
    print(f"\n生成 {version} 版本嵌入式模型 (MNN {quant_label})...")
    generate_embedded_models(version, models_dir, output_dir, quant)
    
    # 创建编译目录
    build_dir = os.path.join(base_dir, f"win-{BUILD_OUTPUT}-{ONNX_TYPE}-{BUILD_CMAKE_A}-{version}")
    os.makedirs(build_dir, exist_ok=True)

    # 清理失效的 CMake 缓存
    cache_file = os.path.join(build_dir, "CMakeCache.txt")
    if os.path.exists(cache_file):
        try:
            with open(cache_file, 'r', encoding='utf-8', errors='ignore') as f:
                cache_text = f.read()
            expected_source = base_dir.replace('\\', '/').lower()
            if expected_source not in cache_text.lower():
                print(f"检测到 CMake 缓存路径失效, 清理 {build_dir} 重新配置...")
                shutil.rmtree(build_dir, ignore_errors=True)
                os.makedirs(build_dir, exist_ok=True)
        except Exception:
            pass
    
    print("\n" + "=" * 60)
    print(f"RapidOcrMnn {version} 编译配置")
    print("=" * 60)
    print(f"编译类型: {BUILD_TYPE}")
    print(f"输出类型: {BUILD_OUTPUT} (DLL)")
    print(f"CRT类型: 静态 (mt)")
    print(f"推理引擎: MNN (OCR) + ORT (Table)")
    print(f"嵌入式模型: {EMBEDDED_MODELS}")
    print(f"目标平台: {BUILD_CMAKE_A}")
    print("=" * 60)
    
    # CMake 配置
    cmake_cmd = [
        "cmake",
        "-T", f"{BUILD_CMAKE_T},host=x64",
        "-A", BUILD_CMAKE_A,
        f"-DCMAKE_INSTALL_PREFIX=install",
        f"-DCMAKE_BUILD_TYPE={BUILD_TYPE}",
        f"-DOCR_OUTPUT={BUILD_OUTPUT}",
        f"-DOCR_BUILD_CRT={MT_ENABLED}",
        f"-DOCR_ONNX={ONNX_TYPE}",
        f"-DOCR_EMBEDDED_MODELS={EMBEDDED_MODELS}",
        ".."
    ]
    
    print("\n执行 CMake 配置...")
    result = subprocess.run(cmake_cmd, cwd=build_dir, capture_output=False)
    if result.returncode != 0:
        print("CMake 配置失败!")
        return False
    
    # 编译
    num_processors = os.cpu_count() or 4
    # 大模型单线程编译避免OOM
    if version == "medium" and quant == "none":
        num_processors = 1
        print(f"  [INFO] medium FP32 uses single-thread compilation to avoid OOM")
    build_cmd = [
        "cmake",
        "--build", ".",
        "--config", BUILD_TYPE,
        "-j", str(num_processors)
    ]
    
    print(f"\n执行编译 (使用 {num_processors} 个处理器)...")
    result = subprocess.run(build_cmd, cwd=build_dir, capture_output=False)
    if result.returncode != 0:
        print("编译失败!")
        return False
    
    # 安装
    install_cmd = [
        "cmake",
        "--build", ".",
        "--config", BUILD_TYPE,
        "--target", "install"
    ]
    
    print("\n执行安装...")
    result = subprocess.run(install_cmd, cwd=build_dir, capture_output=False)
    if result.returncode != 0:
        print("安装失败!")
        return False
    
    # 复制输出文件到output目录
    output_dir = os.path.join(base_dir, "output")
    os.makedirs(output_dir, exist_ok=True)
    
    dll_src = os.path.join(build_dir, "Release", "RapidOcrOnnxStatic.dll")
    lib_src = os.path.join(build_dir, "Release", "RapidOcrOnnxStatic.lib")
    
    dll_dst = os.path.join(output_dir, f"RapidOcrOnnx_{version}_{quant}.dll")
    lib_dst = os.path.join(output_dir, f"RapidOcrOnnx_{version}_{quant}.lib")
    
    if os.path.exists(dll_src):
        shutil.copy2(dll_src, dll_dst)
        dll_size = os.path.getsize(dll_dst) / 1024 / 1024
        print(f"DLL: {dll_dst} ({dll_size:.2f} MB)")
    
    if os.path.exists(lib_src):
        shutil.copy2(lib_src, lib_dst)
        lib_size = os.path.getsize(lib_dst) / 1024 / 1024
        print(f"LIB: {lib_dst} ({lib_size:.2f} MB)")
    
    return True


def interactive_menu():
    """交互式菜单: 双击后输入1-9序号选择模型+量化组合"""
    menu = [
        ("tiny",   "none"), ("tiny",   "fp16"), ("tiny",   "int8"),
        ("small",  "none"), ("small",  "fp16"), ("small",  "int8"),
        ("medium", "none"), ("medium", "fp16"), ("medium", "int8"),
    ]
    quant_label = {"none": "FP32", "fp16": "FP16", "int8": "INT8w"}

    print("=" * 60)
    print("  RapidOcrMnn PP-OCRv6 编译脚本")
    print("  OCR: MNN | Table: ORT")
    print("=" * 60)
    print()
    print("  序号  模型     量化")
    print("  ─────────────────────")
    for i, (v, q) in enumerate(menu, 1):
        mark = "  <-- 默认" if i == 4 else ""
        print(f"   {i}.   {v:<8} {quant_label[q]}{mark}")
    print()
    print("  直接回车默认: small + FP32")
    print("=" * 60)

    default_idx = 3  # 0-based, small+FP32

    try:
        raw = input("\n请输入序号 (1-9): ").strip()
    except (EOFError, KeyboardInterrupt):
        raw = ""

    if not raw:
        idx = default_idx
    elif raw.isdigit() and 1 <= int(raw) <= 9:
        idx = int(raw) - 1
    else:
        print(f"  无效输入, 使用默认: small + FP32")
        idx = default_idx

    version, quant = menu[idx]
    print(f"\n  已选择: {version} + {quant_label[quant]}")
    return version, quant


def main():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    models_dir = os.path.join(base_dir, "模型转二进制", "models")

    if len(sys.argv) > 1:
        quant = "none"
        versions_to_build = []
        args = sys.argv[1:]
        i = 0
        while i < len(args):
            if args[i] == "--quant" and i + 1 < len(args):
                quant_arg = args[i + 1].lower()
                if quant_arg in ("none", "fp16", "int8"):
                    quant = quant_arg
                else:
                    print(f"错误: 无效的量化选项 '{args[i+1]}'，可选: none, fp16, int8")
                    sys.exit(1)
                i += 2
            elif args[i].lower() in MODEL_VERSIONS:
                versions_to_build.append(args[i].lower())
                i += 1
            else:
                print(f"警告: 未知参数 '{args[i]}'，可选: tiny, small, medium, --quant none|fp16|int8")
                i += 1
        if not versions_to_build:
            versions_to_build = list(MODEL_VERSIONS.keys())
    else:
        version, quant = interactive_menu()
        versions_to_build = [version]

    quant_label = {"none": "FP32 (不量化)", "fp16": "FP16 (体积减半)", "int8": "INT8w (体积缩减74%)"}.get(quant, quant)

    print("\n" + "=" * 60)
    print(f"编译版本: {', '.join(versions_to_build)}")
    print(f"MNN量化:  {quant_label}")
    print(f"模型目录: {models_dir}")
    print(f"输出目录: {os.path.join(base_dir, 'output')}")
    print("=" * 60)

    if not os.path.exists(models_dir):
        print(f"错误: 模型目录不存在: {models_dir}")
        sys.exit(1)

    converter = find_mnn_converter()
    if not converter:
        print("警告: 未找到mnnconvert工具!")
        print("可选方案:")
        print("  1. pip install mnn")
        print("  2. 设置环境变量 MNN_CONVERTER 指向mnnconvert.exe")
        print("  3. 将mnnconvert.exe添加到PATH")
    else:
        print(f"MNN转换器: {converter}")

    success_count = 0
    for version in versions_to_build:
        print(f"\n{'='*60}")
        print(f"编译 {version} 版本")
        print("=" * 60)

        if build_version(version, base_dir, models_dir, quant):
            success_count += 1
            print(f"{version} 版本编译成功!")
        else:
            print(f"{version} 版本编译失败!")

    print("\n" + "=" * 60)
    print(f"编译完成: {success_count}/{len(versions_to_build)} 版本成功")
    print("=" * 60)

    output_dir = os.path.join(base_dir, "output")
    if os.path.exists(output_dir):
        print("\n输出文件:")
        for f in sorted(os.listdir(output_dir)):
            path = os.path.join(output_dir, f)
            size = os.path.getsize(path) / 1024 / 1024
            print(f"  {f}: {size:.2f} MB")


if __name__ == "__main__":
    main()
