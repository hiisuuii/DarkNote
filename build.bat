@echo off
windres darknote.rc -O coff -o darknote.res || exit /b 1
gcc -municode -mwindows -Os -s -Wall -Wextra -Wno-missing-field-initializers -ffunction-sections -fdata-sections ^
    -fno-asynchronous-unwind-tables -nostdlib -Wl,--gc-sections -Wl,-e,Entry ^
    darknote.c darknote.res -o DarkNote.exe ^
    -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -luser32 -lkernel32 -ladvapi32 -lgcc || exit /b 1
