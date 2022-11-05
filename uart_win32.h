#ifndef _uart_win32_h
#define _uart_win32_h

#include <windows.h>

#define COMM_READ_BUF_SIZE      (2 * 1024)
#define COMM_WRITE_BUF_SIZE     (10 * 1024)

#ifdef MAKE_DLL
#ifdef __cplusplus
#define EXPORT_DLL extern "C" __declspec(dllexport) __stdcall
#else
#define EXPORT_DLL __declspec(dllexport) __stdcall
#endif
#define CB_CALL __stdcall
#else
#define EXPORT_DLL
#define CB_CALL
#endif

#if  _MSC_VER > 1800
#define gets gets_s
#endif

typedef enum
{
    cc_shutdown,
    cc_error
} enum_comm_close;

typedef enum
{
    ev_shutdown,
    ev_comm_event,
    ev_comm_write,
    ev_write,
    ev_last
} enum_events;

typedef CB_CALL void (*f_on_comm_read)(void *param, const char *p, const int l);
typedef CB_CALL void (*f_on_comm_close)(void *param, const enum_comm_close reason);

typedef struct _uart_obj
{
    char            comm[256];
    bool            async_io;
    HANDLE          h_comm;
    HANDLE          h_thread;
    HANDLE          h_comm_state;
    OVERLAPPED      o_event;
    OVERLAPPED      o_write;
    OVERLAPPED      o_read;
    HANDLE          events[ev_last];
    f_on_comm_read   on_comm_read;
    void           * comm_read_param;
    f_on_comm_close  on_comm_close;
    void            *comm_close_param;

    CRITICAL_SECTION cs;
    char            comm_write_buf[COMM_WRITE_BUF_SIZE];
    char            comm_write_send_buf[COMM_WRITE_BUF_SIZE];
    DWORD           write_buf_used;

    char            comm_read_buf[COMM_READ_BUF_SIZE];
} uart_obj, *p_uart_obj;

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
            const bool       async_io);

EXPORT_DLL void uart_send(uart_obj *uart, const char *buf, const int l);

EXPORT_DLL void uart_shutdown(uart_obj *uart);

EXPORT_DLL int get_uart_obj_size(void);

EXPORT_DLL int uart_config(uart_obj *uart,
            int  baud,
            const char *parity,
            int  databits,
            int  stopbits);

#endif
