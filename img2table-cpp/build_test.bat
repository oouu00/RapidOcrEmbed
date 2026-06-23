@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "C:\Users\zhuyue\Desktop\RapidOcrEmbed\RapidOcrEmbed\img2table-cpp"
if not exist build_test mkdir build_test
cl.exe /EHsc /std:c++17 /MT /I include /I "C:\Users\zhuyue\Desktop\RapidOcrEmbed\RapidOcrEmbed\opencv-static\windows-x64\include" /Fe:build_test\table_test.exe src\table\cells.cpp src\table\clustering.cpp src\table\creation.cpp src\table\extractor.cpp src\table\html_gen.cpp src\table\lines.cpp src\table\metrics.cpp src\table\normalization.cpp src\table\threshold.cpp test\main.cpp /link /LIBPATH:"C:\Users\zhuyue\Desktop\RapidOcrEmbed\RapidOcrEmbed\opencv-static\windows-x64\x64\vc16\staticlib" opencv_core460.lib opencv_imgproc460.lib opencv_imgcodecs460.lib IlmImf.lib libjpeg-turbo.lib libopenjp2.lib libpng.lib libtiff.lib libwebp.lib zlib.lib
echo EXIT_CODE=%ERRORLEVEL%
