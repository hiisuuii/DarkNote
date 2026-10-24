@echo off
cd /d "%~dp0"
wix build -arch x64 -ext WixToolset.UI.wixext DarkNote.wxs -o DarkNote-Setup.msi || exit /b 1
