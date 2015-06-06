#include <libserialport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "shared.h"

/*
 * http://wiki.synchro.net/ref:xmodem
 *
 * This function calculates the CRC used by the XMODEM/CRC Protocol
 * The first argument is a pointer to the message block.
 * The second argument is the number of bytes in the message block.
 * The function returns an integer which contains the CRC.
 * The low order 16 bits are the coefficients of the CRC.
 */
static
unsigned crc_xmodem(char *ptr, unsigned count)
{
	int crc, i;

	crc = 0;
	while (count-- > 0) {
		crc = crc ^ (unsigned)*ptr++ << 8;
		for (i = 0; i < 8; ++i)
			if (crc & 0x8000)
				crc = crc << 1 ^ 0x1021;
			else
				crc = crc << 1;
	}
	return (crc & 0xFFFF);
}

void xmodem_write(struct sp_port *port, unsigned long addr, FILE *f)
{
	/* SOH + pkt_ct + ~byte_count + bytes + crc */
	char obuf[1 + 1 + 1 + 128 + 2];
	char *data = obuf + 3;

	snprintf(obuf, sizeof(obuf), "S%lX#", addr);
	serial_write(port, obuf);

	obuf[0] = '\x01'; /* SOH */

	char ibuf[1];
	/* wait for C */
	int r = sp_blocking_read(port, ibuf, 1, 0);
	if (r < 0) {
		fprintf(stderr, "Error reading from serial: %d\n",
				r);
		exit(EXIT_FAILURE);
	}

	if (*ibuf != 'C') {
		fprintf(stderr, "Recieved %c (%#x) instead of expected 'C'\n",
				ibuf[0], ibuf[0]);
		exit(EXIT_FAILURE);
	}

	/*
	   <soh>	   01H
	   <eot>	   04H
	   <ack>	   06H
	   <nak>	   15H
	   <can>	   18H
	   <C>		   43H
	*/
	unsigned char ct = 1;
	for (;;) {
		size_t b = fread(data, 1, 128, f);
		if (!b) {
			/* SEND EOT */
			obuf[0] = '\x04';
			sp_blocking_write(port, obuf, 1, 0);
			break;
		}

		obuf[1] = ct;
		obuf[2] = 255 - b;

		unsigned crc = crc_xmodem(data, b);
		data[b] = crc & 0xff;
		data[b+1] = crc >> 8;

		r = sp_blocking_write(port, obuf, data + b + 2 - obuf, 0);
		if (r < 0) {
			fprintf(stderr, "Error writing data: %d\n",
					r);
			exit(EXIT_FAILURE);
		}

		r = sp_blocking_read(port, ibuf, 1, 0);
		if (r < 0) {
			fprintf(stderr, "Error waiting for ack: %d\n",
					r);
			exit(EXIT_FAILURE);
		}

		if (*ibuf != '\x06') {
			fprintf(stderr, "Recieved %#02x instead of expected 0x06\n",
					*ibuf);
			exit(EXIT_FAILURE);
		}

		/* TODO: resend on NAK */
	}
}

void xmodem_read(struct sp_port *port, unsigned long addr,
		unsigned long count, FILE *f)
{
	/* SOH + pkt_ct + ~byte_count + bytes + crc */
	char buf[1 + 1 + 1 + 128 + 2];

	snprintf(buf, sizeof(buf), "R%lX,%lX#C", addr, count);
	serial_write(port, buf);

	for (;;) {
		int r = sp_blocking_read(port, buf, 1, 0);
		if (r < 0) {
			fprintf(stderr, "Error waiting for ack: %d\n",
					r);
			exit(EXIT_FAILURE);
		}

		switch (*buf) {
			case '\x01':
				/* SOH, new record incomming, next item is byte ct */
				break;
			case '\x04':
				/* EOT, done */
				break;
			default:
				/* Whoops, looks like a desync */
				fprintf(stderr, "Recieved %#02x when not expected\n",
						*buf);
				exit(EXIT_FAILURE);
		}
	}
}

