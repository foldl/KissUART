# KissUART

A serial port utility (for Windows) based on K.I.S.S. principle.

This can be built into a command line tool, an Erlang port, or a DLL as needed.

# Build

Batch file using GCC is provided. It's would be simple to use another compiler
instead.

* A stand alone executable:  `build.bat EXE`

* An Erlang port: `build.bat PORT`

* A DLL: `build.bat DLL`

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
         -timestamp display time stamp for output default: OFF
         -async_io  use win32 async IO operations default: OFF
         -cr        cr | lf | crlf | lfcr         default: cr
         -input     string | char

Note: string: based on 'gets' (default),
              can use UP/DOWN to access input history
      char  : based on 'getch' (low level)
```

### A Tip on ^Z

When string mode (default) is used, ^Z<Enter> could save ^Z into the output buffer, and another <Enter> is needed to
write it to COM port.

## An Erlang port

This Erlang port uses the exactly the same command line options as the stand alone executable, except that some common
options are obviously not avaliable.

### Example: uart2tcp

`socat` is powerful Linux tool, which can let other programs access an uart port
just like a TCP port. On Windows, it is much easier to build our own wheel than
to search a similar tool. See [`uart2tcp.erl`](uart2tcp.erl).

## A DLL

Only 5 APIs are exported by this DLL. Below is Pascal (Delphi/Lazarus) code for reference.

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

function UartConfig(Uart: TUartObj;
                    const Baud: Integer;
                    const Parity: PChar;
                    const DataBits: Integer;
                    const StopBits: Integer): Integer; stdcall; external 'uart.dll' name 'uart_config@20';
```



