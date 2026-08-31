@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set HOST=%~1
if "%HOST%"=="" set HOST=72.62.76.118

set EXE=
if exist "%~dp0tools\VanitySearchKang3.exe" set EXE=%~dp0tools\VanitySearchKang3.exe
if not defined EXE if exist "%~dp0..\VanitySearch-Bitcrack-kangaroo\x64\Release\VanitySearchKang3.exe" set EXE=%~dp0..\VanitySearch-Bitcrack-kangaroo\x64\Release\VanitySearchKang3.exe
if not defined EXE if exist "%~dp0..\VanitySearch-Bitcrack-kangaroo\x64\Release\VanitySearch.exe" set EXE=%~dp0..\VanitySearch-Bitcrack-kangaroo\x64\Release\VanitySearch.exe

if not defined EXE (
  echo Rebuild the kangaroo tree with -pool support:
  echo   msbuild ..\VanitySearch-Bitcrack-kangaroo\VanitySearch.vcxproj /p:Configuration=Release /p:Platform=x64 /p:TargetName=VanitySearchKang3
  echo Then copy VanitySearchKang3.exe into tools\ or leave it in x64\Release.
  exit /b 1
)

if not "%~2"=="" (
  echo Worker: %EXE%
  echo Pool:   %HOST%:17403   gpu %~2
  "%EXE%" -pool %HOST%:17403 -gpuId %~2 -worker %COMPUTERNAME%-gpu%~2
  exit /b %ERRORLEVEL%
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0JoinWorker.ps1" -HostName "%HOST%" -Exe "%EXE%"
endlocal
