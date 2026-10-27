@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
cl /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /Fe:tests.exe /Fo:.\ RulesTests.cpp ..\Source\LongNight\Rules\LongNightRules.cpp > build.log 2>&1
if errorlevel 1 (
  type build.log
  exit /b 1
)
tests.exe > results.txt 2>&1
set "testResult=%errorlevel%"
type results.txt
exit /b %testResult%
