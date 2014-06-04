// http://msdn.microsoft.com/en-us/library/ms810467.aspx
//
#include <stdio.h>

#include "uart_win32.h"

#define dbg_print //printf

static int finalize(p_uart_obj uart)
{
    CloseHandle(uart->h_comm);
    CloseHandle(uart->events[ev_comm_event]);
    CloseHandle(uart->events[ev_comm_write]);
    CloseHandle(uart->events[ev_shutdown]);
    CloseHandle(uart->events[ev_write]);
    DeleteCriticalSection(&uart->cs);
    return 0;
}

static int fatal(p_uart_obj uart, const char *msg)
{
    dbg_print("fatal: %s\n", msg);
    finalize(uart);
    return -1;
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

    to_read = min(COMM_READ_BUF_SIZE, comStat.cbInQue);
    while (to_read > 0)
    {
        if (!ReadFile(uart->h_comm, uart->comm_read_buf, to_read, &read, &uart->o_write)) 
        {
            if (GetLastError() == ERROR_IO_PENDING)
            {
                if (WaitForSingleObject(uart->o_write.hEvent, INFINITE) != WAIT_OBJECT_0)
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
        if (to_read != read) 
        {
            dbg_print("to_read != read\n");
            return false;
        }
        uart->on_comm_read(uart->comm_read_param, uart->comm_read_buf, to_read);
        comStat.cbInQue -= to_read;
        to_read = min(COMM_READ_BUF_SIZE, comStat.cbInQue);
    }
    
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

bool check_comm_event(uart_obj *uart, const DWORD event)
{
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

    // A line-status error occurred. Line-status errors are CE_FRAME, CE_OVERRUN, and CE_RXPARITY.
    if (event & EV_ERR)
    {
        dbg_print("event: EV_ERR\n");
        goto error;
    }

    // A ring indicator was detected.
    if (event & EV_RING)
    {
        dbg_print("event: EV_RING\n");
    }

    // The RLSD (receive-line-signal-detect) signal changed state.
    if (event & EV_RLSD)
    {
        dbg_print("event: EV_RLSD\n");
        goto error;
    }

    // A character was received and placed in the input buffer.
    if (event & EV_RXCHAR)
    {
        dbg_print("event: EV_RXCHAR\n");
        if (!comm_read(uart))
            goto error;
    }

    // The event character was received and placed in the input buffer
    if (event & EV_RXFLAG)
    {
        dbg_print("unexpected event: EV_RXFLAG\n");
        goto error;
    }

    // The last character in the output buffer was sent.
    if (event & EV_TXEMPTY)
    {
        dbg_print("event: EV_TXEMPTY\n");
    }

    return true;

error:
    return false;    
}

bool wait_comm_event(uart_obj *uart, DWORD &event, bool &pending)
{
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
            if (!check_comm_event(uart, event))
                return false;
        }
    }
}

static DWORD WINAPI uart_thread(uart_obj *uart)
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
            if (!check_comm_event(uart, event))
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

uart_obj *uart_open(uart_obj *uart,
            const int portnr,
            int  baud,          // baudrate   
            const char *parity, // parity    "none", "even", "odd", "mark", and "space"
            int  databits,      // databits    
            int  stopbits,
            f_on_comm_read   on_comm_read,
            void            *comm_read_param,
            f_on_comm_close  on_comm_close,
            void            *comm_close_param) 
{
    memset(uart, 0, sizeof(*uart));
    uart->on_comm_read = on_comm_read;
    uart->comm_read_param = comm_read_param;
    uart->comm_close_param = comm_close_param;

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
    timeout.ReadIntervalTimeout = 100;   
    timeout.ReadTotalTimeoutMultiplier = 10;   
    timeout.ReadTotalTimeoutConstant = 1000;   
    timeout.WriteTotalTimeoutMultiplier = 10;   
    timeout.WriteTotalTimeoutConstant = 1000;

    // get a handle to the port   
    uart->h_comm = CreateFile(uart->comm,               // communication port string (COMX)   
                         GENERIC_READ | GENERIC_WRITE,  // read/write types   
                         0,                             // comm devices must be opened with exclusive access   
                         NULL,                          // no security attributes   
                         OPEN_EXISTING,                 // comm devices must use OPEN_EXISTING   
                         FILE_FLAG_OVERLAPPED,          // Async I/O   
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

    char sdcb[200] = {'\0'};
    char s[100];
    if (baud > 0)
    {
        sprintf(s, "baud=%d ", baud);
        strcat(sdcb, s);
    }
    if (databits > 0)
    {
        sprintf(s, "databits=%d ", databits);
        strcat(sdcb, s);
    }
    if (stopbits > 0)
    {
        sprintf(s, "stopbits=%d ", stopbits);
        strcat(sdcb, s);
    }
    if (strlen(parity) > 0)
        strcat(sdcb, parity);
    dbg_print("dcb = %s\n", sdcb);
    
    if (!BuildCommDCB(sdcb, &dcb))
    {
        fatal(uart, "BuildCommDCB()");
        return NULL;
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

    dcb.fOutxCtsFlow = TRUE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    if (!SetCommState(uart->h_comm, &dcb))
    {
        fatal(uart, "SetCommState()");
        return NULL;
    }     
    
    // flush the port   
    PurgeComm(uart->h_comm, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);   
   
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

    return uart;
}

void uart_shutdown(uart_obj *uart)
{
    SetEvent(uart->events[ev_shutdown]);
    WaitForSingleObject(uart->h_thread, INFINITE);
}
 
void uart_send(uart_obj *uart, const char *buf, const int l)
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


