// http://msdn.microsoft.com/en-us/library/ms810467.aspx
//
#include <stdio.h>
#include <time.h>
#include <stdint.h>

#include "uart_win32.h"

int port_dbg_print(const char *s, ...);

#define dbg_print dummy // port_dbg_print // dummy //printf

#define MIN(a, b) ((a) > (b) ? (b) : (a))

#ifdef MAKE_DLL

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, PVOID pvReserved)
{
    srand(time(NULL));

    return TRUE;
}

#endif

void dummy(...)
{
}

static int finalize(p_uart_obj uart)
{
    CloseHandle(uart->h_comm);
    for (int i = 0; i < ev_last; i++)
        CloseHandle(uart->events[i]);
    DeleteCriticalSection(&uart->cs);
    return 0;
}

static int fatal(p_uart_obj uart, const char *msg)
{
    dbg_print("fatal: %s\n", msg);
    finalize(uart);
    return -1;
}

static bool comm_read2(p_uart_obj uart, char b)
{
    COMSTAT comStat;
    DWORD   dwErrors;
    bool first = true;

    uart->comm_read_buf[0] = b;

    // Get and clear current errors on the port.
    if (!ClearCommError(uart->h_comm, &dwErrors, &comStat) || (comStat.cbInQue == 0))
    {
        uart->on_comm_read(uart->comm_read_param, uart->comm_read_buf, 1);
        return true;
    }

    dbg_print("comStat.cbInQue = %d, \n", (int)comStat.cbInQue);

    DWORD to_read;
    DWORD read;

    uart->comm_read_buf[0] = b;
    to_read = MIN(COMM_READ_BUF_SIZE - 1, comStat.cbInQue);
    while (to_read > 0)
    {
        if (!ReadFile(uart->h_comm, first ? uart->comm_read_buf + 1 : uart->comm_read_buf,
                      to_read, &read, &uart->o_read))
        {
            if (GetLastError() == ERROR_IO_PENDING)
            {
                if (WaitForSingleObject(uart->o_read.hEvent, INFINITE) != WAIT_OBJECT_0)
                {
                    dbg_print("ReadFile wait error\n");
                    return false;
                }
                continue;
            }
            else
            {
                dbg_print("ReadFile ERROR_IO_PENDING\n");
                return false;
            }
        }
        if (to_read != read)
        {
            dbg_print("to_read != read: %ld, %ld\n", to_read, read);
            return false;
        }
        uart->on_comm_read(uart->comm_read_param, uart->comm_read_buf, first ? to_read + 1 : to_read);
        comStat.cbInQue -= to_read;
        to_read = MIN(COMM_READ_BUF_SIZE, comStat.cbInQue);
        first = false;
    }

    return true;
}

static bool comm_read(p_uart_obj uart)
{
    COMSTAT comStat;
    DWORD   dwErrors;

    // Get and clear current errors on the port.
    if (!ClearCommError(uart->h_comm, &dwErrors, &comStat))
    {
        dbg_print("error: ClearCommError\n");
        return false;
    }

    dbg_print("comStat.cbInQue = %d, \n", (int)comStat.cbInQue);

    if (comStat.cbInQue == 0)
    {
        dbg_print("warn: comStat.cbInQue == 0\n");
        return true;
    }

    DWORD to_read;
    DWORD read;

    static int wait = 0;
    if (wait) return true;
    wait = 1;

    to_read = MIN(COMM_READ_BUF_SIZE, comStat.cbInQue);
    while (to_read > 0)
    {
        ResetEvent(uart->o_read.hEvent);
        if (!ReadFile(uart->h_comm, uart->comm_read_buf, to_read, &read, &uart->o_read))
        {
            if (GetLastError() == ERROR_IO_PENDING)
            {
                if (WaitForSingleObject(uart->o_read.hEvent, 500) != WAIT_OBJECT_0)
                {
                    dbg_print("ReadFile wait error\n");
                    return false;
                }
            }
            else
            {
                dbg_print("ReadFile ERROR_IO_PENDING\n");
                return false;
            }
        }

        if (read == 0) continue;

        uart->on_comm_read(uart->comm_read_param, uart->comm_read_buf, to_read);
        comStat.cbInQue -= to_read;
        to_read = MIN(COMM_READ_BUF_SIZE, comStat.cbInQue);
    }

    wait = 0;

    return true;
}

static bool comm_write(p_uart_obj uart, bool &pending)
{
    bool r = true;
    DWORD to_write;
    DWORD write;

    if (pending) goto ret;

    EnterCriticalSection(&uart->cs);

    to_write = uart->write_buf_used;
    if (to_write <= 0) goto ret;
    memcpy(uart->comm_write_send_buf, uart->comm_write_buf, to_write);
    uart->write_buf_used = 0;

    dbg_print("sending %d bytes...\n", (int)to_write);
    if (!WriteFile(uart->h_comm, uart->comm_write_send_buf, to_write, &write, &uart->o_write))
    {
        pending = GetLastError() == ERROR_IO_PENDING;
        r = pending;
    }
    else;

ret:
    LeaveCriticalSection(&uart->cs);
    return r;
}

static bool handle_comm_event(uart_obj *uart, const DWORD event)
{
    // A line-status error occurred. Line-status errors are CE_FRAME, CE_OVERRUN, and CE_RXPARITY.
    if (event & EV_ERR)
    {
        DWORD errors;
        dbg_print("event: EV_ERR\n");
        if (ClearCommError(uart->h_comm, &errors, NULL))
        {
#define check_error(err) if (errors & err) dbg_print("error cleared: " #err "\n")

            check_error(CE_BREAK);
            check_error(CE_FRAME);
            check_error(CE_IOE);
            check_error(CE_MODE);
            check_error(CE_OVERRUN);
            check_error(CE_RXOVER);
            check_error(CE_RXPARITY);
            check_error(CE_TXFULL);
        }
        else
            goto error;
    }

    // The RLSD (receive-line-signal-detect) signal changed state.
    if (event & EV_RLSD)
    {
        dbg_print("event: EV_RLSD\n");
        // goto error;
    }

    // A character was received and placed in the input buffer.
    if (event & EV_RXCHAR)
    {
        dbg_print("event: EV_RXCHAR\n");
        if (!comm_read(uart))
            goto error;
    }

    // A break was detected on input.
    if (event & EV_BREAK)
    {
        dbg_print("event: EV_BREAK\n");
    }

    // The CTS (clear-to-send) signal changed state.
    if (event & EV_CTS)
    {
        dbg_print("event: EV_CTS\n");
    }

    // The DSR (data-set-ready) signal changed state.
    if (event & EV_DSR)
    {
        dbg_print("event: EV_DSR\n");
    }

    // A ring indicator was detected.
    if (event & EV_RING)
    {
        dbg_print("event: EV_RING\n");
    }

    // The event character was received and placed in the input buffer
    //if (event & EV_RXFLAG)
    //{
    //    dbg_print("unexpected event: EV_RXFLAG\n");
    //    goto error;
    //}

    // The last character in the output buffer was sent.
    if (event & EV_TXEMPTY)
    {
        dbg_print("event: EV_TXEMPTY\n");
    }

    return true;

error:
    return false;
}

static DWORD WINAPI uart_rx_loop(uart_obj* uart)
{
    DWORD len = 0;
    char b = 0;

    while (ReadFile(uart->h_comm, &b, 1, &len, NULL))
    {
        if (len)
            comm_read2(uart, b);
        else
            Sleep(10);
    }

    return 0;
}

static bool wait_comm_event(uart_obj *uart, DWORD &event, bool &pending)
{
    if (!uart->async_io) return true;

    if (pending) return true;

    while (true)
    {
        if (!WaitCommEvent(uart->h_comm, &event, &uart->o_event))
        {
            pending = GetLastError() == ERROR_IO_PENDING;
            dbg_print("WaitCommEvent pending: %d\n", pending);
            return pending;
        }
        else
        {
            if (!handle_comm_event(uart, event))
                return false;
        }
    }
}

static DWORD WINAPI uart_thread(uart_obj* uart)
{
    // Misc. variables
    DWORD transfered = 0;

    bool  shutdown = false;
    DWORD event = 0;
    bool  event_pending = false;
    bool  write_pending = false;
    DWORD r = 0;
    enum_comm_close reason = cc_shutdown;

    if (!wait_comm_event(uart, event, event_pending))
        goto error;

    while (!shutdown)
    {
        DWORD Event = WaitForMultipleObjects(sizeof(uart->events) / sizeof(uart->events[0]),
            uart->events, FALSE, INFINITE);
        dbg_print("Event = %d\n", (int)Event - WAIT_OBJECT_0);
        switch (Event)
        {
        case WAIT_OBJECT_0 + ev_shutdown:
            shutdown = true;
            break;
        case WAIT_OBJECT_0 + ev_comm_event:
            // read event
            event_pending = false;
            if (!GetOverlappedResult(uart->h_comm, &uart->o_event, &transfered, FALSE))
            {
                dbg_print("error: GetOverlappedResult\n");
                goto error;
            }
            ResetEvent(uart->events[ev_comm_event]);
            if (!handle_comm_event(uart, event))
                goto error;
            if (!wait_comm_event(uart, event, event_pending))
                goto error;
            break;
        case WAIT_OBJECT_0 + ev_comm_write:
            write_pending = false;
            ResetEvent(uart->events[ev_comm_write]);
            if (!comm_write(uart, write_pending))
                goto error;
            break;
        case WAIT_OBJECT_0 + ev_write:
            if (!comm_write(uart, write_pending))
                goto error;
            break;
        default:
            goto error;
        }

    }

    goto clean_up;

error:
    r = -1;
    reason = cc_error;

clean_up:

    finalize(uart);
    if (NULL != uart->on_comm_close)
        uart->on_comm_close(uart->comm_close_param, reason);

    return r;
}

EXPORT_DLL int uart_config(uart_obj *uart,
            int  baud,          // baudrate
            const char *parity, // parity    "none", "even", "odd", "mark", and "space"
            int  databits,      // databits
            int  stopbits)
{
    DCB dcb;
    memset(&dcb, 0, sizeof(DCB));
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(uart->h_comm, &dcb))
    {
        fatal(uart, "GetCommState()");
        return 1;
    }

    static char sdcb[500] = {'\0'};
    char s[100];
    if (baud > 0)
    {
        sprintf(s, "baud=%d ", baud);
        strcat(sdcb, s);
    }
    if (databits > 0)
    {
        sprintf(s, "data=%d ", databits);
        strcat(sdcb, s);
    }
    if (stopbits > 0)
    {
        sprintf(s, "stop=%d ", stopbits);
        strcat(sdcb, s);
    }
    if (strlen(parity) > 0)
    {
        char temp[3] = {0};
        char c = parity[0];
        if ((c >= 'a') && (c <= 'z')) c = c - 'a' + 'A';
        temp[0] = c;
        sprintf(s, "parity=%s ", temp);
        strcat(sdcb, s);
    }
    dbg_print("dcb = %s\n", sdcb);

    if (!BuildCommDCBA(sdcb, &dcb))
    {
        fatal(uart, "BuildCommDCB()");
        return 2;
    }

    dbg_print("fOutxCtsFlow = %d\n", (int)dcb.fOutxCtsFlow);
    dbg_print("fOutxDsrFlow = %d\n", (int)dcb.fOutxDsrFlow);
    dbg_print("fRtsControl = %d\n", (int)dcb.fRtsControl);
    dbg_print("fDtrControl = %d\n", (int)dcb.fDtrControl);
    dbg_print("fDsrSensitivity = %d\n", (int)dcb.fDsrSensitivity);
    dbg_print("fOutX = %d\n", (int)dcb.fOutX);
    dbg_print("BaudRate = %d\n", (int)dcb.BaudRate);
    dbg_print("fParity = %d\n", (int)dcb.fParity);
    dbg_print("fAbortOnError = %d\n", (int)dcb.fAbortOnError);
    dbg_print("XonLim = %d\n", (int)dcb.XonLim);
    dbg_print("XoffLim = %d\n", (int)dcb.XoffLim);

    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fDtrControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fBinary = TRUE;
    dcb.fDsrSensitivity = FALSE;

    //dcb.XonLim = 0;
    //dcb.XoffLim = 0;
    //dcb.ByteSize = 8;

    #define BUF_SIZE    10240
    SetupComm(uart->h_comm, BUF_SIZE, BUF_SIZE);
    dcb.XonLim = BUF_SIZE / 4;
    dcb.XoffLim = BUF_SIZE / 4;

    if (!SetCommState(uart->h_comm, &dcb))
    {
        fatal(uart, "SetCommState()");
        return 3;
    }

    // flush the port
    PurgeComm(uart->h_comm, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);
    return 0;
}

EXPORT_DLL uart_obj *uart_open(uart_obj *uart,
            const int portnr,
            int  baud,          // baudrate
            const char *parity, // parity    "none", "even", "odd", "mark", and "space"
            int  databits,      // databits
            int  stopbits,
            f_on_comm_read   on_comm_read,
            void            *comm_read_param,
            f_on_comm_close  on_comm_close,
            void            *comm_close_param,
            const bool       async_io)
{
    memset(uart, 0, sizeof(*uart));
    uart->on_comm_read = on_comm_read;
    uart->comm_read_param = comm_read_param;
    uart->comm_close_param = comm_close_param;
    uart->async_io = async_io;

    uart->events[ev_shutdown] = CreateEvent(NULL, FALSE, FALSE, NULL);
    uart->events[ev_comm_event] = CreateEvent(NULL, TRUE, FALSE, NULL);  // manual reset for OVERLAPPED
    uart->events[ev_comm_write] = CreateEvent(NULL, TRUE, FALSE, NULL);
    uart->events[ev_write] = CreateEvent(NULL, FALSE, FALSE, NULL);

    uart->o_event.hEvent = uart->events[ev_comm_event];
    uart->o_write.hEvent = uart->events[ev_comm_write];
    uart->o_read.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    sprintf(uart->comm, "\\\\.\\COM%d", portnr);

    // set the timeout values
    COMMTIMEOUTS timeout;
    timeout.ReadIntervalTimeout = MAXDWORD;
    timeout.ReadTotalTimeoutMultiplier = 0;
    timeout.ReadTotalTimeoutConstant = 0;
    timeout.WriteTotalTimeoutMultiplier = 10;
    timeout.WriteTotalTimeoutConstant = 1000;

    DWORD flags = FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
    if (async_io) flags |= FILE_FLAG_OVERLAPPED;

    // get a handle to the port
    uart->h_comm = CreateFileA(uart->comm,              // communication port string (COMX)
                         GENERIC_READ | GENERIC_WRITE,  // read/write types
                         0,                             // comm devices must be opened with exclusive access
                         NULL,                          // no security attributes
                         OPEN_EXISTING,                 // comm devices must use OPEN_EXISTING
                         flags,                         // Async I/O
                         0);                            // template must be 0 for comm devices

    if (uart->h_comm == INVALID_HANDLE_VALUE)
    {
        fatal(uart, "uart->h_comm == INVALID_HANDLE_VALUE");
        return NULL;
    }

    InitializeCriticalSection(&uart->cs);

    // configure
    if (!SetCommTimeouts(uart->h_comm, &timeout))
    {
        fatal(uart, "SetCommTimeouts()");
        return NULL;
    }

    // all events are monitered
    if (!SetCommMask(uart->h_comm, EV_BREAK | EV_CTS | EV_DSR | EV_ERR | EV_RING | EV_RLSD
                | EV_RXCHAR | EV_RXFLAG | EV_TXEMPTY))
    {
        fatal(uart, "SetCommMask()");
        return NULL;
    }

    DCB dcb;
    memset(&dcb, 0, sizeof(DCB));
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(uart->h_comm, &dcb))
    {
        fatal(uart, "GetCommState()");
        return NULL;
    }

    if (uart_config(uart, baud, parity, databits, stopbits) != 0)
        return NULL;

    // on_comm_close is enabled now
    uart->on_comm_close = on_comm_close;

    uart->h_thread = CreateThread(
      NULL,     // _In_opt_   LPSECURITY_ATTRIBUTES lpThreadAttributes,
      0,        // _In_       SIZE_T dwStackSize,
      LPTHREAD_START_ROUTINE(uart_thread),  // _In_       LPTHREAD_START_ROUTINE lpStartAddress,
      uart,     // _In_opt_   LPVOID lpParameter,
      0,        // _In_       DWORD dwCreationFlags,
      NULL      // _Out_opt_  LPDWORD lpThreadId
    );

    if (NULL == uart->h_thread)
    {
        fatal(uart, "CreateThread()");
        return NULL;
    }

    if (!async_io)
    {
        uart->h_comm_state = CreateThread(
            NULL,     // _In_opt_   LPSECURITY_ATTRIBUTES lpThreadAttributes,
            0,        // _In_       SIZE_T dwStackSize,
            LPTHREAD_START_ROUTINE(uart_rx_loop),  // _In_       LPTHREAD_START_ROUTINE lpStartAddress,
            uart,     // _In_opt_   LPVOID lpParameter,
            0,        // _In_       DWORD dwCreationFlags,
            NULL      // _Out_opt_  LPDWORD lpThreadId
        );
    }

    return uart;
}

EXPORT_DLL void uart_shutdown(uart_obj *uart)
{
    SetEvent(uart->events[ev_shutdown]);
#ifndef MAKE_DLL
    switch (WaitForSingleObject(uart->h_thread, 1000))
    {
    case WAIT_OBJECT_0:
        break;
    case WAIT_TIMEOUT:
        dbg_print("shutdown: WAIT_TIMEOUT\n");
        break;
    }
#endif
}

EXPORT_DLL void uart_send(uart_obj *uart, const char *buf, const int l)
{
#ifdef _DEBUG
    printf("send %d byte(s):", l);
    for (int i = 0; i < l; i++) printf(" %.2X", buf[i]);
    printf("\n");
#endif

    if (l < 1) return;

    EnterCriticalSection(&uart->cs);
    if (uart->write_buf_used + l <= COMM_WRITE_BUF_SIZE)
    {
        memcpy(uart->comm_write_buf + uart->write_buf_used, buf, l);
        uart->write_buf_used += l;
    }
    else
        dbg_print("comm_write_buf overflow\n");
    LeaveCriticalSection(&uart->cs);
    dbg_print("uart_send SetEvent\n");
    SetEvent(uart->events[ev_write]);
}

EXPORT_DLL int get_uart_obj_size()
{
    return sizeof(uart_obj);
}
