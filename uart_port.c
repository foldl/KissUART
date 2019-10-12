//
#include <stdio.h>
#include <fcntl.h>
#include "uart_win32.h"

#define command_write_to_uart       0
#define command_read_from_uart      1
#define command_dbg_msg             2
#define command_shutdown            3

#define MAX_COMM_PACK_SIZE 65536

#define dbg_printf port_dbg_print

typedef unsigned char byte;

int read_exact(byte *buf, int len)
{
  int i, got=0;

  do 
  {
    if ((i = read(0, buf + got, len - got)) <= 0)
      return i;
    got += i;
  } while (got < len);

  return len;
}

int write_exact(byte *buf, int len)
{
  int i, wrote = 0;

  do 
  {
    if ((i = write(1, buf + wrote, len - wrote)) <= 0)
      return i;
    wrote += i;
  } while (wrote < len);

  return len;
}

int read_cmd(byte *buf)
{
  int len;

  if (read_exact(buf, 2) != 2)
    return -1;
  len = (buf[0] << 8) | buf[1];
  return read_exact(buf, len);
}

int write_cmd(byte *buf, int len)
{
  byte li;

  li = (len >> 8) & 0xff;
  write_exact(&li, 1);
  
  li = len & 0xff;
  write_exact(&li, 1);

  return write_exact(buf, len);
}

struct uart_port_comm
{
    char t;
    int len;
    byte *b;
};

bool read_comm_cmd(uart_port_comm &r)
{
    static byte cmd_buf[MAX_COMM_PACK_SIZE];
    r.len = -1;
    int len = read_cmd(cmd_buf);
    if (len < 0)
        return false;
    
    r.t = cmd_buf[0];
    r.len = len - 1;
    r.b = cmd_buf + 1;
    r.b[r.len] = 0;
    return true;
}

bool send_comm_response(const int t, const byte *s, const int len)
{
    static byte out_buf[MAX_COMM_PACK_SIZE];
    if (1 + len > MAX_COMM_PACK_SIZE)
        return false;

    out_buf[0] = t;
    memcpy(out_buf + 1, s, len);

    return write_cmd(out_buf, len + 1) > 0; 
}

int port_dbg_print(const char *s, ...)
{
    char t[20 * 1024];
    t[0] = '\0';
    va_list va;
    va_start(va, s);
    vsprintf(t, s, va);
    va_end(va);
    send_comm_response(command_dbg_msg, (byte *)t, strlen(t));
    return 0;
}

static void on_comm_read(uart_obj *uart, byte *buf, const int l)
{
    send_comm_response(command_read_from_uart, buf, l);
}

static void on_comm_close(uart_obj *uart, const enum_comm_close reason)
{
    dbg_printf("COM closed: %s\n", reason == cc_shutdown ? "shutdown" : "error");
    exit(0);
}

int main(const int argc, const char *args[])
{   
    uart_obj uart;
    int port = -1;
    int baud = -1;
    char parity[20] = {'\0'};
    int  databits = -1;
    int  stopbits = -1;
    bool async_io = false;

    setmode(0, O_BINARY);
    setmode(1, O_BINARY);
    
#define load_i_param(param) \
    if (strcmp(args[i], "-"#param) == 0)   \
    {   param = atoi(args[i + 1]); i += 2; }

#define load_b_param(param) \
    if (strcmp(args[i], "-"#param) == 0)   \
    {   param = true; i++; }

    int i = 1;
    while (i < argc - 1)
    {
        load_i_param(port)
        else load_i_param(baud)
        else load_i_param(databits)
        else load_i_param(stopbits)
        else load_b_param(async_io)
        else if (strcmp(args[i], "-parity") == 0)
        {
            strncpy(parity, args[i + 1], 19);
            i += 2;
        }
        else
            i++;
    }

    if (port < 0)
    {
        dbg_printf("port unspecified\n");
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
        dbg_printf("failed to open the specified COM port\n");
        return -1;
    }

    while (true)
    {
        uart_port_comm c;
        if (!read_comm_cmd(c))
            break;
        switch (c.t)
        {
        case command_write_to_uart:
            uart_send(&uart, (char *)c.b, c.len);
            break;
        case command_shutdown:
            uart_shutdown(&uart);
            break;
        default:
            break;
        }
    }
}
