@echo off

IF "%1"=="PORT" (
del /F .\uart_port.exe
g++ -Wall -o .\uart_port.exe uart_port.c uart_win32.c
goto :EOF
)

IF "%1"=="DLL" (
del /F .\uart.dll
g++ -D MAKE_DLL -O3 -Wall -c uart_win32.c 
gcc -shared -s -o .\uart.dll uart_win32.o
goto :EOF
)

IF "%1"=="EXE" (
del /F .\uart.exe
g++ -Wall -s -o .\uart.exe uart_main.c uart_win32.c 
goto :EOF
)

echo usage:
echo     build.bat PORT
echo     build.bat DLL
echo     build.bat EXE

:EOF
