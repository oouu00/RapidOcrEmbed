# -*- coding: utf-8 -*-
r"""
OcrQtCpp MSVC 全静态构建脚本
产物: 单文件 OcrQtCpp.exe (OCR + ORT + OpenCV + 模型全部链接进去)

用法: python build_msvc.py [--clean]
前提:
  - 上级 RapidOcrEmbed 已编译出 RapidOcrOnnxStatic.lib
  - 已安装 MSVC 2019 (v142)
  - 已安装 Qt5 静态库 (C:\Qt\5.15.2\msvc2019-x64-static)

工作流:
  1. 生成 libs.pri (库列表, 已排除 libjpeg-turbo 以避免与 Qt JPEG 冲突)
  2. 在 vcvars64.bat 环境里用 qmake + nmake 编译

⚠️ 注意: libs.pri 由 gen_libs_pri.py 自动生成, 已排除 libjpeg-turbo.lib
   如果需要恢复 OpenCV JPEG 能力, 参见 rename_jpeg_symbols.py
"""
import os
import sys
import subprocess

VCVARS = r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
QMAKE = r"C:\Qt\5.15.2\msvc2019-x64-static\bin\qmake.exe"
PRO_DIR = os.path.dirname(os.path.abspath(__file__))
# OCR 静态库放在本目录 (RapidOcrOnnxStatic.lib)
OCR_LIB = os.path.join(PRO_DIR, "libs", "RapidOcrOnnxStatic.lib")


def kill_running_exe():
    """结束正在运行的 OcrQtCpp.exe 进程"""
    r = subprocess.run('tasklist /FI "IMAGENAME eq OcrQtCpp.exe" /NH', shell=True, capture_output=True, text=True)
    if "OcrQtCpp.exe" in r.stdout:
        print("[INFO] 检测到 OcrQtCpp.exe 正在运行，正在结束...")
        subprocess.run('taskkill /F /IM OcrQtCpp.exe', shell=True, capture_output=True)
        print("       已结束")


def get_msvc_env():
    """通过 vcvars64.bat 获取完整的 MSVC 编译环境变量"""
    print(f"\n[1/4] 激活 MSVC 环境: {VCVARS}")
    r = subprocess.run(f'"{VCVARS}" >nul 2>nul && set', shell=True, capture_output=True, text=True)
    if r.returncode != 0:
        print("[ERROR] vcvars64 激活失败")
        print(r.stderr)
        sys.exit(1)
    env = os.environ.copy()
    for line in r.stdout.splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            env[k] = v
    cl_check = subprocess.run("where cl", shell=True, capture_output=True, text=True, env=env)
    if cl_check.returncode != 0:
        print("[ERROR] vcvars64 激活后仍找不到 cl.exe")
        sys.exit(1)
    print(f"    cl.exe: {cl_check.stdout.strip().splitlines()[0]}")
    return env


def run_shell(cmd_str, cwd, env):
    """用 shell 跑命令, 实时打印输出, 返回 CompletedProcess"""
    print(f"\n$ {cmd_str}")
    return subprocess.run(cmd_str, cwd=cwd, env=env, shell=True)


def main():
    do_clean = "--clean" in sys.argv

    # ---------------- 结束运行中的进程 ----------------
    kill_running_exe()

    # ---------------- 删除 release 文件夹 ----------------
    import shutil
    release_dir = os.path.join(PRO_DIR, "release")
    if os.path.isdir(release_dir):
        print(f"\n[INFO] 删除 release 文件夹: {release_dir}")
        shutil.rmtree(release_dir, ignore_errors=True)
        print("       已删除")

    # ---------------- 前置检查 ----------------
    for path, desc in [(VCVARS, "vcvars64.bat"), (QMAKE, "qmake.exe"), (OCR_LIB, "RapidOcrOnnxStatic.lib")]:
        if not os.path.exists(path):
            print(f"[ERROR] 找不到 {desc}: {path}")
            if desc == "RapidOcrOnnxStatic.lib":
                print("        请先在 ../RapidOcrEmbed 运行 build.py 编译静态库,")
                print("        然后复制 RapidOcrOnnxStatic.lib 到本目录")
            sys.exit(1)

    # 显示堆栈大小设置说明
    print("\n[INFO] 堆栈大小设置:")
    print("    OcrQtCpp.pro 中已设置 /STACK:4194304 (4MB)")
    print("    这将避免排版算法和 PDF 处理的堆栈溢出问题")
    print("    默认堆栈大小为 1MB，可能不足以处理复杂的排版算法")

    if do_clean:
        print("\n[clean] 清理旧产物")
        import shutil
        for p in ["release", "debug"]:
            full = os.path.join(PRO_DIR, p)
            if os.path.isdir(full):
                shutil.rmtree(full, ignore_errors=True)
        for fn in [".qmake.stash", "Makefile.Release", "Makefile.Debug", "libs.pri",
                   "ocrqtcpp_plugin_import.cpp"]:
            full = os.path.join(PRO_DIR, fn)
            if os.path.isfile(full):
                os.remove(full)

    # ---------------- 生成 libs.pri ----------------
    print("\n[2/4] 生成 libs.pri...")
    r = run_shell(f'python gen_libs_pri.py', PRO_DIR, None)
    if r.returncode != 0:
        print("[ERROR] gen_libs_pri 失败")
        sys.exit(1)

    env = get_msvc_env()

    # ---------------- qmake ----------------
    print(f"\n[3/4] qmake (Qt 静态库: {QMAKE})")
    r = run_shell(f'"{QMAKE}" OcrQtCpp.pro -spec win32-msvc "CONFIG += release"', PRO_DIR, env)
    if r.returncode != 0:
        print("[ERROR] qmake 失败")
        sys.exit(1)

    # ---------------- 手动修改 Makefile.Release 添加堆栈大小 ----------------
    print("\n[INFO] 手动修改 Makefile.Release 添加堆栈大小设置...")
    makefile_path = os.path.join(PRO_DIR, "Makefile.Release")
    if os.path.exists(makefile_path):
        with open(makefile_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # 查找 LFLAGS 行并添加 STACK 设置
        if '/STACK:' not in content:
            # 在 LFLAGS 行中添加 /STACK:4194304
            lines = content.split('\n')
            modified_lines = []
            for line in lines:
                if line.startswith('LFLAGS        ='):
                    # 在 LFLAGS 行末尾添加 /STACK:4194304
                    line = line.rstrip() + ' /STACK:4194304'
                    print(f"    修改 LFLAGS 行: {line[:100]}...")
                modified_lines.append(line)
            
            content = '\n'.join(modified_lines)
            with open(makefile_path, 'w', encoding='utf-8') as f:
                f.write(content)
            print("    Makefile.Release 已修改，添加了 /STACK:4194304 (4MB 堆栈)")
        else:
            print("    Makefile.Release 已包含 STACK 设置，无需修改")
    else:
        print("[ERROR] Makefile.Release 不存在")
        sys.exit(1)

    # ---------------- nmake ----------------
    print("\n[4/4] nmake (编译 + 链接)")
    # Makefile.Release 默认目标 first->all->release\OcrQtCpp.exe
    r = run_shell("nmake /NOLOGO -f Makefile.Release", PRO_DIR, env)
    if r.returncode != 0:
        print(f"\n[ERROR] nmake 失败 (exit {r.returncode})")
        sys.exit(r.returncode)

    # ---------------- 校验产物 ----------------
    exe = os.path.join(PRO_DIR, "release", "OcrQtCpp.exe")
    if os.path.exists(exe):
        size = os.path.getsize(exe) / 1024 / 1024
        print(f"\n[OK] 构建成功: {exe} ({size:.2f} MB)")
    else:
        print(f"\n[ERROR] 未找到产物: {exe}")
        sys.exit(1)


if __name__ == "__main__":
    main()
