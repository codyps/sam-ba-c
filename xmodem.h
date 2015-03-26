#pragma once

#include <stddef.h>
#include <stdbool.h>

/*
 * Issues:
 *  - 1 arg magic functions
 *  - No way to tell which callback failed from return value (specify ranges of values? extra returns?)
 *  - Doesn't allow non-blocking (maybe)
 */

struct xmodem_ctx {
	int (*send)(struct xmodem_ctx *ctx, void *buf, size_t bytes);
	int (*recv)(struct xmodem_ctx *ctx, void *buf, size_t bytes);

	/*
	 * either read or write depending on how we are called
	 * <0 on error, otherwise # of bytes read or written
	 * Writes are required to be complete
	 * Reads may be short
	 * 0 = EOF
	 */
	int (*file_op)(struct xmodem_ctx *ctx, void *buf, size_t bytes);

	/* we can either keep this buffer, or keep an offset & use file_op_at() */
	unsigned char ct; /* current pkt count, should be initialized to 1 */
	bool has_acked;   /* should be initialized to false */
	unsigned char buf[128];
};

/*
 * returns 0 if transfer is completed
 * returns 1 if the transfer is not yet completed
 * returns 2 if there was a timeout
 * returns <0 if there was an IO error (in send(), recv(), or file_op())
 */
int xmodem_send(struct xmodem_ctx *ctx);
int xmodem_recv(struct xmodem_ctx *ctx);
