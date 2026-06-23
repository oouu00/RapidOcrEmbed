@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set PYINC=C:\Users\zhuyue\AppData\Local\Programs\Python\Python312\Include
set PYLIB=C:\Users\zhuyue\AppData\Local\Programs\Python\Python312\libs\python312.lib
set NPINC=C:\Users\zhuyue\AppData\Local\Programs\Python\Python312\Lib\site-packages\numpy\_core\include
set BASE=C:\Users\zhuyue\Desktop\RapidOcrEmbed\RapidOcrEmbed\img2table-cpp\pyban\img2table-main\src\img2table

cd /d "%BASE%\tables\bordered\cells"
echo === Building _identification.pyd ===
cl.exe /LD /EHsc /I "%PYINC%" /I "%NPINC%" /Fe:_identification.cp312-win_amd64.pyd _identification.c /link "%PYLIB%" /EXPORT:PyInit__identification
echo EXIT=%ERRORLEVEL%

cd /d "%BASE%\tables"
echo === Building _metrics.pyd ===
cl.exe /LD /EHsc /I "%PYINC%" /I "%NPINC%" /Fe:_metrics.cp312-win_amd64.pyd _metrics.c /link "%PYLIB%" /EXPORT:PyInit__metrics
echo EXIT=%ERRORLEVEL%

cd /d "%BASE%\tables\borderless\layout"
echo === Building _text_lines.pyd ===
cl.exe /LD /EHsc /I "%PYINC%" /I "%NPINC%" /Fe:_text_lines.cp312-win_amd64.pyd _text_lines.c /link "%PYLIB%" /EXPORT:PyInit__text_lines
echo EXIT=%ERRORLEVEL%
