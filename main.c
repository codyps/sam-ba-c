#include <libserialport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <limits.h>
#include <errno.h>
#include <error.h>

#define _cleanup_(x) __attribute__((cleanup(x)))

#define DEFINE_TRIVIAL_CLEANUP_FUNC(type, func)                 \
        static inline void func##p(type *p) {                   \
                if (*p)                                         \
                        func(*p);                               \
        }                                                       \
        struct __useless_struct_to_allow_trailing_semicolon__

DEFINE_TRIVIAL_CLEANUP_FUNC(FILE*, fclose);
DEFINE_TRIVIAL_CLEANUP_FUNC(struct sp_port*, sp_close);

#define _cleanup_fclose_ _cleanup_(fclosep)
#define _cleanup_sp_close_ _cleanup_(sp_closep)

static FILE *
xfopen(const char *path, const char *mode)
{
	FILE *f = fopen(path, mode);
	if (!f) {
		error(EXIT_FAILURE, errno, "Could not open file '%s' with mode '%s'\n", path, mode);
	}
	return f;
}

#define S(x) (x), (sizeof(x) - 1)
#define serial_write(port, str) serial_write_(port, S(str))

static void
serial_write_(struct sp_port *port, const char *str, size_t len)
{
	int r = sp_blocking_write(port, str, len, 0);
	if (r < 0) {
		fprintf(stderr, "Error writing data: %d\n",
				r);
		exit(EXIT_FAILURE);
	}
}

/*
 * http://wiki.synchro.net/ref:xmodem
 *
 * This function calculates the CRC used by the XMODEM/CRC Protocol
 * The first argument is a pointer to the message block.
 * The second argument is the number of bytes in the message block.
 * The function returns an integer which contains the CRC.
 * The low order 16 bits are the coefficients of the CRC.
 */
static unsigned
crc_xmodem(char *ptr, unsigned count)
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

static void
xmodem_write(struct sp_port *port, unsigned long addr, FILE *f)
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

static void
xmodem_read(struct sp_port *port, unsigned long addr,
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

static void
usage_(const char *prgm, int e)
{
	FILE *o;
	if (e)
		o = stderr;
	else
		o = stdout;

	fprintf(o,
"usage: %s <sam-ba serial port> <action> <action args>...\n"
"actions:\n"
"	write <address> <file>\n"
"	read  <address> <length> <file>\n"
"	go    <address>\n"
		, prgm ? prgm : "sam-ba");

	exit(e);
}

static int
serial_read_to(struct sp_port *port, char *buf, size_t buf_size, int c)
{
	int r;
	struct sp_event_set *event_set;
	r = sp_new_event_set(&event_set);
	if (r < 0) {
		fprintf(stderr, "Error creating event set: %d\n",
				r);
		exit(EXIT_FAILURE);
	}

	r = sp_add_port_events(event_set, port, SP_EVENT_RX_READY);
	if (r < 0) {
		fprintf(stderr, "Error adding rx_ready event: %d\n",
				r);
		exit(EXIT_FAILURE);
	}

	size_t pos = 0;
	for (;;) {
		r = sp_wait(event_set, 0);
		if (r < 0) {
			fprintf(stderr, "Error waiting for events: %d\n",
					r);
			exit(EXIT_FAILURE);
		}

		r = sp_nonblocking_read(port, buf + pos, buf_size - pos);
		if (r < 0) {
			fprintf(stderr, "Error reading from port; %d\n",
					r);
			exit(EXIT_FAILURE);
		}

		/* scan for newline */
		char *found = memchr(buf + pos, c, r);
		pos += r;

		if (!found)
			continue;

		sp_free_event_set(event_set);
		return found - buf;
	}
}

#define usage(e) usage_(argc ? argv[0] : NULL, e)

int
main(int argc, char **argv)
{
	if (argc < 3)
		usage(1);

	_cleanup_sp_close_ struct sp_port *port = NULL;
	int r = sp_get_port_by_name(argv[1], &port);
	if (r < 0) {
		fprintf(stderr, "Error getting port %s, %d\n",
				argv[1], r);
		exit(EXIT_FAILURE);
	}

	r = sp_open(port, SP_MODE_READ | SP_MODE_WRITE);
	if (r < 0) {
		fprintf(stderr, "Error opening port %s: %d\n",
				argv[1], r);
		exit(EXIT_FAILURE);
	}

	r = sp_set_baudrate(port, 115200);
	if (r < 0 ) {
		fprintf(stderr, "Error setting baud: %d\n",
				r);
		exit(EXIT_FAILURE);
	}

	r = sp_set_bits(port, 8);
	if (r < 0) {
		fprintf(stderr, "Error setting bits: %d\n",
				r);
		exit(EXIT_FAILURE);
	}

	r = sp_set_parity(port, SP_PARITY_NONE);
	if (r < 0) {
		fprintf(stderr, "Error setting parity: %d\n",
				r);
		exit(EXIT_FAILURE);
	}

	serial_write(port, "N#");
	char buf[1024];
	serial_read_to(port, buf, sizeof(buf), '\n');

	int i;
	for (i = 2; i < argc; i++) {
		const char *cmd =argv[i];
		switch(*cmd) {
			case 'v': {
				serial_write(port, "V#");
				size_t l = serial_read_to(port, buf, sizeof(buf), '\n');
				printf("Version: %.*s\n", (int)l, buf);
			} break;
			case 'g': {
				if (argc - i < 2) {
					fprintf(stderr, "Not enough args to go\n");
					usage(EXIT_FAILURE);
				}

				long long a = strtoll(argv[i+1], NULL, 0);
				snprintf(buf, sizeof(buf), "g%llX#", a);
				serial_write(port, buf);
				i += 1;
			} break;
			case 'w': {
				/* write file */
				if (argc - i < 2) {
					fprintf(stderr, "Not enough args to write\n");
					usage(EXIT_FAILURE);
				}
				long long a = strtoll(argv[i+1], NULL, 0);
				_cleanup_fclose_ FILE *f = xfopen(argv[i+2], "r");
				xmodem_write(port, a, f);
				fclose(f);
				i += 2;
			} break;
			case 'r': {
				/* read file */
				/* write file */
				if (argc - i < 3) {
					fprintf(stderr, "Not enough args to read\n");
					usage(EXIT_FAILURE);
				}
				long long a = strtoll(argv[i+1], NULL, 0);
				long long l = strtoll(argv[i+2], NULL, 0);
				_cleanup_fclose_ FILE *f = xfopen(argv[i+3], "w");
				xmodem_read(port, a, l, f);
				i += 3;
			} break;
			default:
				fprintf(stderr, "Unknown command %s\n", cmd);
				exit(EXIT_FAILURE);
		}
	}
	return 0;
}
