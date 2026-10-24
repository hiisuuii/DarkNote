@echo off
cd /d "%~dp0"
wix build -arch x64 -ext WixToolset.UI.wixext DarkNote.wxs -o DarkNote-1.0.0.msi || exit /b 1
