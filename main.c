#include <libserialport.h>
#include <stdio.h>
#include <stdlib.h>

static void usage_(const char *prgm, int e)
{
	FILE *o;
	if (e)
		o = stderr;
	else
		o = stdout;

	fprintf(o,
"usage: %s <sam-ba serial port> <action> <action args>...\n",
		prgm ? prgm : "sam-ba");

	exit(e);
}
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


	return 0;
}
