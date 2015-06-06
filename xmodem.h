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
	int (*send)(struct xmodem_ctx *ctx, const void *buf, size_t bytes);
	int (*recv)(struct xmodem_ctx *ctx,       void *buf, size_t bytes);

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

	/*
	 * states (send):
	 * - waiting_for_C
	 *   {
	 * - waiting_for_file_op (if -EAGAIN is returned while reading), VAR=pos_to_read
	 * - waiting_for_send    (if -EAGAIN is returned while sending), VAR=pos_to_send, total_len (encoded)
	 * - waiting_for_recv    (if -EAGAIN is returned while recv polling for ACK), !!TIMEOUT POSSIBLE
	 *   } VAR=ct
	 */

	/*
	 * states (recv):
	 * - waiting_for_send_C
	 *   {
	 * - waiting_for_recv (if -EAGAIN while recving), VAR=pos_to_read_into, !!TIMEOUT POSSIBLE
	 * - waiting_for_file_op (if -EAGAIN while writing file), VAR=pos_to_write_from
	 * - waiting_for_send (if -EAGAIN while acking)
	 *   } VAR=ct
	 */

	unsigned char buf[1 + 1 + 1 + 128 + 2];
};

/*
 * returns 0 if transfer is completed
 * returns 1 if the transfer is not yet completed
 * returns 2 if there was a timeout
 * returns <0 if there was an IO error (in send(), recv(), or file_op())
 */
int xmodem_send(struct xmodem_ctx *ctx);
int xmodem_recv(struct xmodem_ctx *ctx);
