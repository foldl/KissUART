KissUART
=====

A serial port terminal (for Windows) based on KISS principle.

Build
---------

To build a stand alone executable:

`g++ -Wall -s -o .\uart.exe uart_main.c uart_win32.c`

To build an executable that could act as an Erlang port:

`g++ -Wall -s -o .\uart_port.exe uart_port.c uart_win32.c`

Usage
-----

```
UART port options:
         -port      <integer>                     (mandatory)
         -baud      <integer>
         -databits  <integer>
         -stopbits  <integer>
         -parity    none | even | odd | mark | space
Common options:
         -help/-?                                 show this
         -cr        cr | lf | crlf | lfcr         default: cr
         -input     string | char

Note: string: based on 'gets' (default), can
              use UP/DOWN to access input history
      char  : based on 'getch' (low level)
```

Tip on ^Z
-----

When string mode (default) is used, ^Z<Enter> could save ^Z into the output buffer, and another <Enter> is needed to
write it to COM port.


