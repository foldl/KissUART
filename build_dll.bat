del /F .\uart.dll
g++ -D MAKE_DLL -O3 -Wall -c uart_win32.c 
gcc -shared -s -o .\uart.dll uart_win32.o

rem g++ -Wall -D MAKE_DLL -shared -s -o .\uart.dll uart_win32.c 
