#pragma once

#include <libserialport.h>

#define S(x) (x), (sizeof(x) - 1)
#define serial_write(port, str) serial_write_(port, S(str))

void serial_write_(struct sp_port *port, const char *str, size_t len);
void xmodem_read(struct sp_port *port, unsigned long addr,
		unsigned long count, FILE *f);
void xmodem_write(struct sp_port *port, unsigned long addr, FILE *f);
