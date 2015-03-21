#include <libserialport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <limits.h>

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
int crc_xmodem(char *ptr, int count)
{
	int crc, i;

	crc = 0;
	while (--count >= 0) {
		crc = crc ^ (int)*ptr++ << 8;
		for (i = 0; i < 8; ++i)
			if (crc & 0x8000)
				crc = crc << 1 ^ 0x1021;
			else
				crc = crc << 1;
	}
	return (crc & 0xFFFF);
}

static void usage_(const char *prgm, int e)
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

static int serial_read_to(struct sp_port *port, char *buf, size_t buf_size, int c)
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

static void serial_write_(struct sp_port *port, const char *str, size_t len)
{
	int r = sp_blocking_write(port, str, len, 0);
	if (r < 0) {
		fprintf(stderr, "Error writing data: %d\n",
				r);
		exit(EXIT_FAILURE);
	}
}
#define S(x) (x), (sizeof(x) - 1)
#define serial_write(port, str) serial_write_(port, S(str))

#define usage(e) usage_(argc ? argv[0] : NULL, e)

int main(int argc, char **argv)
{
	if (argc < 3)
		usage(1);

	struct sp_port *port;
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
	size_t l = serial_read_to(port, buf, sizeof(buf), '\n');

	int i;
	for (i = 2; i < argc; i++) {
		const char *cmd =argv[i];
		switch(*cmd) {
			case 'v':
				serial_write(port, "V#");
				l = serial_read_to(port, buf, sizeof(buf), '\n');
				printf("Version: %.*s\n", (int)l, buf);
				break;
			case 'w':
				/* write file */
				break;
			case 'r':
				/* read file */
				break;
			default:
				fprintf(stderr, "Unknown command %s\n", cmd);
				exit(EXIT_FAILURE);
		}

	}

	return 0;
}
