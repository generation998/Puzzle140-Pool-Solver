@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set HOST=%~1
set GPU=0
if "%HOST%"=="" set HOST=72.62.76.118
if not "%~2"=="" set GPU=%~2

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

echo Worker: %EXE%
echo Pool:   %HOST%:17403   gpu %GPU%
"%EXE%" -pool %HOST%:17403 -gpuId %GPU% -worker %COMPUTERNAME%
endlocal
