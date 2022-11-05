//
#include <stdio.h>
#include <conio.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include "uart_win32.h"

#define dbg_printf(...) //printf

static bool hex = false;
static bool timestamp = false;
static int print_counter = 0;

static void print_time(void)
{
    SYSTEMTIME wtm;
    GetLocalTime(&wtm);
    printf("[%04d-%2d-%02d %02d:%02d:%02d.%03d] ",
           wtm.wYear,
           wtm.wMonth,
           wtm.wDay,
           wtm.wHour,
           wtm.wMinute,
           wtm.wSecond,
           wtm.wMilliseconds);
}

static void on_comm_read(uart_obj *uart, const char *buf, const int l)
{
    if (l < 1) return;

    if (hex)
    {
        unsigned char *s = (unsigned char *)buf;
        for (int i = 0; i < l; i++)
        {
            printf("%02X ", s[i]);
            print_counter++;
            if (print_counter % 32 == 0)
            {
                printf("\n");
                if (timestamp) print_time();
            }
        }
    }
    else
    {
        static char s[20 * 1024];
        if (timestamp)
        {
            static bool new_line = true;
            int j = -1;
            int i = 0;
            for (i = 0; i < l; i++)
            {
                if (('\r' == buf[i]) || ('\n' == buf[i]))
                {
                    if ((j >= 0) && (i > j))
                    {
                        memcpy(s, buf + j, i - j);
                        s[i - j + 0] = '\n';
                        s[i - j + 1] = '\0';
                        if (new_line)
                            print_time();
                        printf(s);
                        new_line = true;
                    }
                    else;
                    j = i + 1;
                }
                else
                {
                    if (j < 0) j = i;
                }
                continue;
            }

            if ((j >= 0) && (i > j))
            {
                memcpy(s, buf + j, i - j);
                s[i - j + 0] = '\0';
                if (new_line)
                {
                    print_time();
                    new_line = false;
                }
                printf(s);
            }
        }
        else
        {
            int t = (int)sizeof(s) - 1 < l ? sizeof(s) - 1 : l;
            memcpy(s, buf, t);

            s[l] = '\0';
            printf(s);
        }
    }
}

static void on_comm_close(uart_obj *uart, const enum_comm_close reason)
{
    dbg_printf("COM closed: %s\n", reason == cc_shutdown ? "shutdown" : "error");
    exit(0);
}

void help()
{
    printf("UART util command line options:\n");
    printf("UART port options:\n");
    printf("\t -port      <integer>                     (mandatory)\n");
    printf("\t -baud      <integer>\n");
    printf("\t -databits  <integer>\n");
    printf("\t -stopbits  <integer>\n");
    printf("\t -parity    none | even | odd | mark | space\n");
    printf("Common options:\n");
    printf("\t -help/-?                                 show this\n");
    printf("\t -hex       use hex display\n");
    printf("\t -timestamp display time stamp for output default: OFF\n");
    printf("\t -async_io  use win32 async IO operations default: OFF\n");
    printf("\t -cr        cr | lf | crlf | lfcr         default: cr\n");
    printf("\t -input     string | char \n"
           "\n"
           "Note: string: based on 'gets' (default),\n"
           "              can use UP/DOWN to access input history\n"
           "      char  : based on 'getch' (low level)");
}

static uart_obj uart;
static char     cr[3] = {'\r', '\0'};
static bool     use_getch = false;

void interact_direct();
void interact_str();
void interact_hex();
BOOL ctrl_handler(DWORD fdwCtrlType);

int main(const int argc, const char *args[])
{
    int port = -1;
    int baud = -1;
    char parity[20] = {'\0'};
    int  databits = -1;
    int  stopbits = -1;
    bool async_io = false;

#define check_param_arg() do { if (i >= argc - 1) { fprintf(stderr, "arg missing for: %s\n", args[i]); help(); return -1; } } while (0)

#define load_i_param(param) \
    if (strcmp(args[i], "-"#param) == 0)   \
    {   check_param_arg(); param = atoi(args[i + 1]); i += 2; }

#define load_b_param(param) \
    if (strcmp(args[i], "-"#param) == 0)   \
    {   param = true; i++; }

    if (argc < 2)
    {
        help();
        return -1;
    }

    int i = 1;
    while (i < argc)
    {
        load_i_param(port)
        else load_i_param(baud)
        else load_i_param(databits)
        else load_i_param(stopbits)
        else load_b_param(hex)
        else load_b_param(async_io)
        else load_b_param(timestamp)
        else if ((strcmp(args[i], "-?") == 0) || (strcmp(args[i], "-help") == 0))
        {
            help();
            return -1;
        }
        else if (strcmp(args[i], "-cr") == 0)
        {
            check_param_arg();
            if (strcmp(args[i + 1], "cr") == 0) strcpy(cr, "\r");
            else if (strcmp(args[i + 1], "lf") == 0) strcpy(cr, "\n");
            else if (strcmp(args[i + 1], "crlf") == 0) strcpy(cr, "\r\n");
            else if (strcmp(args[i + 1], "lfcr") == 0) strcpy(cr, "\r\r");
            i += 2;
        }
        else if (strcmp(args[i], "-input") == 0)
        {
            check_param_arg();
            if (strcmp(args[i + 1], "string") == 0) use_getch = false;
            else if (strcmp(args[i + 1], "char") == 0) use_getch = true;
            i += 2;
        }
        else if (strcmp(args[i], "-parity") == 0)
        {
            check_param_arg();
            strncpy(parity, args[i + 1], 19);
            i += 2;
        }
        else
        {
            fprintf(stderr, "unknown option: %s\n", args[i]);
            return -1;
        }
    }

    if (hex) use_getch = false;

    if (port < 0)
    {
        fprintf(stderr, "Port unspecified\n");
        return -1;
    }

    if (uart_open(&uart,
                  port,
                  baud,
                  parity,
                  databits,
                  stopbits,
                  f_on_comm_read(on_comm_read),
                  &uart,
                  f_on_comm_close(on_comm_close),
                  &uart,
                  async_io) == NULL)
    {
        fprintf(stderr, "Failed to open the specified port COM%d\n", port);
        return -1;
    }
    else
    {
        fprintf(stderr, "Port COM%d is opened. Input mode: %s\n", port, use_getch ? "CHAR" : "STRING");
        fprintf(stderr, "Use Ctrl+C to close port and exit.\n");
    }

    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrl_handler, TRUE))
        fprintf(stderr, "WARNING: SetConsoleCtrlHandler failed.\n");

    if (use_getch)
        interact_direct();
    else if (!hex)
        interact_str();
    else
        interact_hex();
    return 0;
}

void interact_str()
{
    char s[10240 + 1];
    while (true)
    {
        s[0] = '\0';
        gets(s);
        if (strlen(s) >= sizeof(s) - 1)
        {
            uart_shutdown(&uart);
            break;
        }

        strcat(s, cr);
        uart_send(&uart, s, strlen(s));
    }
}

int parsestr(char *s)
{
    int r = 0;
    while (*s != '\0')
    {
        char c = *s++;
        r *= 16;
        if (('0' <= c) && (c <= '9')) r += c - '0';
        else if (('A' <= c) && (c <= 'F')) r += c - 'A';
        else if (('a' <= c) && (c <= 'a')) r += c - 'a';
    }
    return r;
}

int hexstr(char *s, char *b, int max)
{
    char *p = s;
    int i = 0;

    // parse hex input
    while (*p != '\0')
    {
        char *start = p;
        if (*p == ' ')
        {
            p++;
            continue;
        }

        while ((*p != '\0') && (*p != ' ')) p++;
        if (*p == ' ') *p = '\0';
        b[i++] = parsestr(start);
        if (i > max) break;
        p++;
    }
    return i;
}

void interact_hex()
{
    static char s[10240 + 1];
    static char d[10240];
    while (true)
    {
        s[0] = '\0';
        gets(s);
        if (strlen(s) >= sizeof(s) - 1)
        {
            uart_shutdown(&uart);
            break;
        }
        print_counter = 0;
        int count = hexstr(s, d, sizeof(d) - 1);
        uart_send(&uart, d, count);
    }
}

void interact_direct()
{
    while (true)
    {
        char ch = _getch();

        // although it is said that getch can't be used to capture Ctrl+C
        if (ch == 3)
        {
            dbg_printf("Ctrl+C: breaking...\n");
            uart_shutdown(&uart);
            break;
        }

        if (ch == '\r')
            uart_send(&uart, cr, strlen(cr));
        else
            uart_send(&uart, &ch, 1);

        putchar(ch);
    }
}

BOOL ctrl_handler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
        case CTRL_C_EVENT:
            uart_shutdown(&uart);
            return FALSE;

        case CTRL_BREAK_EVENT:
            return FALSE;

        // console close/logoff/shutdown
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            uart_shutdown(&uart);
            return FALSE;

        default:
            return FALSE;
    }
}

