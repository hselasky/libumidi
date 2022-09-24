/*-
 * Copyright (c) 2022 Hans Petter Selasky <hselasky@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <err.h>

#include "umidi20.h"

#define	UMIDI20_PIPE_MAX 1024		/* bytes */

struct umidi20_pipe {
	uint8_t	data[UMIDI20_PIPE_MAX];
	size_t	consumer;
	size_t	total;
	umidi20_pipe_callback_t *fn;
};

static pthread_mutex_t umidi20_pipe_mtx;

void
umidi20_pipe_init(void)
{
	pthread_mutex_init(&umidi20_pipe_mtx, NULL);
}

void
umidi20_pipe_alloc(struct umidi20_pipe **pipe, umidi20_pipe_callback_t *fn)
{
	struct umidi20_pipe *temp;

	temp = calloc(1, sizeof(*temp));
	temp->fn = fn;

	pthread_mutex_lock(&umidi20_pipe_mtx);
	*pipe = temp;
	pthread_mutex_unlock(&umidi20_pipe_mtx);
}

void
umidi20_pipe_free(struct umidi20_pipe **pipe)
{
	struct umidi20_pipe *temp;

	pthread_mutex_lock(&umidi20_pipe_mtx);
	temp = *pipe;
	*pipe = NULL;
	pthread_mutex_unlock(&umidi20_pipe_mtx);
	free(temp);
}

ssize_t
umidi20_pipe_read_data(struct umidi20_pipe **pp, uint8_t *dst, size_t num)
{
	struct umidi20_pipe *pipe;
	uint8_t *old;
	size_t fwd;

	pthread_mutex_lock(&umidi20_pipe_mtx);

	pipe = *pp;
	if (pipe == NULL) {
		old = dst + 1;
		goto done;
	}

	old = dst;
	fwd = UMIDI20_PIPE_MAX - pipe->consumer;

	/* check for maximum amount of data that can be removed */
	if (num > pipe->total)
		num = pipe->total;

	/* copy samples from ring-buffer */
	while (num != 0) {
		if (fwd > num)
			fwd = num;
		memcpy(dst, pipe->data + pipe->consumer, sizeof(pipe->data[0]) * fwd);
		dst += fwd;
		num -= fwd;
		pipe->consumer += fwd;
		pipe->total -= fwd;
		if (pipe->consumer == UMIDI20_PIPE_MAX) {
			pipe->consumer = 0;
			fwd = UMIDI20_PIPE_MAX;
		} else {
			break;
		}
	}
done:
	pthread_mutex_unlock(&umidi20_pipe_mtx);
	return (dst - old);
}

ssize_t
umidi20_pipe_write_data(struct umidi20_pipe **pp, const uint8_t *src, size_t num)
{
	struct umidi20_pipe *pipe;
	umidi20_pipe_callback_t *fn;
	ssize_t retval;
	size_t producer;
	size_t fwd;
	size_t max;

	pthread_mutex_lock(&umidi20_pipe_mtx);
	pipe = *pp;
	if (pipe == NULL) {
		fn = NULL;
		retval = -1;
		goto done;
	}
	fn = pipe->fn;
	producer = (pipe->consumer + pipe->total) % UMIDI20_PIPE_MAX;
	fwd = UMIDI20_PIPE_MAX - producer;
	max = UMIDI20_PIPE_MAX - pipe->total;

	if (num > max)
		num = max;

	retval = num;

	/* copy samples to ring-buffer */
	while (num != 0) {
		if (fwd > num)
			fwd = num;
		if (fwd != 0) {
			memcpy(pipe->data + producer, src, sizeof(pipe->data[0]) * fwd);

			/* update last sample */
			src += fwd;
			num -= fwd;
			pipe->total += fwd;
			producer += fwd;
		}
		if (producer == UMIDI20_PIPE_MAX) {
			producer = 0;
			fwd = UMIDI20_PIPE_MAX;
		} else {
			break;
		}
	}
done:
	pthread_mutex_unlock(&umidi20_pipe_mtx);

	if (fn != NULL)
		(fn) ();

	return (retval);
}
