# -*- coding: utf-8 -*-
"""Rebuild RapidOcrOnnxStatic.lib"""
import os
import subprocess

VCVARS = r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
PROJ_DIR = r"c:\Users\zhuyue\Desktop\RapidOcrEmbed\RapidOcrEmbed\win-CLIB-CPU-x64-small"
VCXPROJ = os.path.join(PROJ_DIR, "RapidOcrOnnxStatic.vcxproj")

# Get MSVC environment
r = subprocess.run(f'"{VCVARS}" >nul 2>nul && set', shell=True, capture_output=True, text=True)
if r.returncode != 0:
    print("Failed to activate vcvars64")
    exit(1)

env = os.environ.copy()
for line in r.stdout.splitlines():
    if "=" in line:
        k, _, v = line.partition("=")
        env[k] = v

# Rebuild static library
print("Rebuilding RapidOcrOnnxStatic.lib...")
r = subprocess.run(
    f'msbuild "{VCXPROJ}" /p:Configuration=Release /t:Rebuild /m',
    shell=True, cwd=PROJ_DIR, env=env
)
if r.returncode != 0:
    print("Build failed")
    exit(1)

print("Build success!")

# Copy to Qt project (same filename)
SRC_LIB = os.path.join(PROJ_DIR, "Release", "RapidOcrOnnxStatic.lib")
DEST_LIB = r"c:\Users\zhuyue\Desktop\RapidOcrEmbed\ocr-qt-cpp\libs\RapidOcrOnnxStatic.lib"
import shutil
shutil.copy2(SRC_LIB, DEST_LIB)
print(f"Copied to: {DEST_LIB}")