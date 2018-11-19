# KissUART

A serial port utility (for Windows) based on KISS principle.

This can be built into a command line tool, an Erlang port, or a DLL as needed.

# Build

## A stand alone executable

`g++ -Wall -s -o .\uart.exe uart_main.c uart_win32.c`

## An Erlang port

`g++ -Wall -s -o .\uart_port.exe uart_port.c uart_win32.c`

## A DLL

`g++ -Wall -D MAKE_DLL -shared -s -o .\uart.dll uart_win32.c`

# Usage

## A stand alone executable

```
UART port options:
         -port      <integer>                     (mandatory)
         -baud      <integer>
         -databits  <integer>
         -stopbits  <integer>
         -parity    none | even | odd | mark | space
Common options:
         -help/-?                                 show this
         -hex       use hex display
         -cr        cr | lf | crlf | lfcr         default: cr
         -input     string | char

Note: string: based on 'gets' (default),
              can use UP/DOWN to access input history
      char  : based on 'getch' (low level)
```

## An Erlang port

This Erlang port uses the exactly the same command line options as the stand alone executable, except that all common
options are obviously not avaliable.

## A DLL

Only 4 APIs are exported by this DLL. Below is Pascal (Delphi/Lazarus) code for reference.

```Pascal
type
 
  TUartObj = Pointer;
  TCommCloseReason = (ccShutdown, ccError);
  TOnCommRead = procedure (Param: Pointer; const P: PByte; const L: Integer);
  TOnCommClose = procedure (Param: Pointer; const Reason: TCommCloseReason);

function UartOpen(Uart: TUartObj;
                  const PortNumber: Integer;
                  const Baud: Integer;
                  const Parity: PChar;
                  const DataBits: Integer;
                  const StopBits: Integer;
                  OnCommRead: TOnCommRead;
                  CommReadParam: Pointer;
                  OnCommClose: TOnCommClose;
                  CommCloseParam: Pointer): TUartObj; stdcall; external 'uart.dll' name 'uart_open@40';

procedure UartSend(Uart: TUartObj;
                   const Buf: PByte;
                   const L: Integer); stdcall; external 'uart.dll' name 'uart_send@12';

procedure UartShutdown(Uart: TUartObj); stdcall; external 'uart.dll' name 'uart_shutdown@4';

function GetUartObjSize: Integer; stdcall; external 'uart.dll' name 'get_uart_obj_size@0';

```

A Tip on ^Z
-----

When string mode (default) is used, ^Z<Enter> could save ^Z into the output buffer, and another <Enter> is needed to
write it to COM port.


