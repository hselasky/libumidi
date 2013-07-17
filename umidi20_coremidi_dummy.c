/*-
 * Copyright (c) 2013 Hans Petter Selasky <hselasky@FreeBSD.org>
 * All rights reserved.
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

#include "umidi20.h"

const char **
umidi20_coremidi_alloc_inputs(void)
{
	return (NULL);
}

const char **
umidi20_coremidi_alloc_outputs(void)
{
	return (NULL);
}

void
umidi20_coremidi_free_inputs(const char **ports)
{
}

void
umidi20_coremidi_free_outputs(const char **ports)
{
}

int
umidi20_coremidi_rx_open(uint8_t n, const char *name)
{
	return (-1);
}

int
umidi20_coremidi_tx_open(uint8_t n, const char *name)
{
	return (-1);
}

int
umidi20_coremidi_rx_close(uint8_t n)
{
	return (-1);
}

int
umidi20_coremidi_tx_close(uint8_t n)
{
	return (-1);
}

int
umidi20_coremidi_init(const char *name)
{
	return (-2);
}
