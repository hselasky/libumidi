/*-
 * Copyright (c) 2011 Hans Petter Selasky <hselasky@FreeBSD.org>
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

/*
 * A few parts of this file has been copied from Edward Tomasz
 * Napierala's jack-keyboard sources.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <err.h>
#include <sysexits.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include "umidi20.h"

struct umidi20_parse {
	uint8_t *temp_cmd;
	uint8_t	temp_0[4];
	uint8_t	temp_1[4];
	uint8_t	state;
#define	UMIDI20_ST_UNKNOWN   0		/* scan for command */
#define	UMIDI20_ST_1PARAM    1
#define	UMIDI20_ST_2PARAM_1  2
#define	UMIDI20_ST_2PARAM_2  3
#define	UMIDI20_ST_SYSEX_0   4
#define	UMIDI20_ST_SYSEX_1   5
#define	UMIDI20_ST_SYSEX_2   6
};

struct umidi20_jack {
	jack_port_t *output_port;
	jack_port_t *input_port;
	int	read_fd[2];
	int	write_fd[2];
	char   *read_name;
	char   *write_name;
	struct umidi20_parse parse;
};

static pthread_mutex_t umidi20_jack_mtx;
static jack_client_t *umidi20_jack_client;
static struct umidi20_jack umidi20_jack[UMIDI20_N_DEVICES];
static int umidi20_jack_init_done;
static const char *umidi20_jack_name;

#ifdef HAVE_DEBUG
#define	DPRINTF(fmt, ...) \
    printf("%s:%d: " fmt, __FUNCTION__, __LINE__,## __VA_ARGS__)
#else
#define	DPRINTF(fmt, ...) do { } while (0)
#endif

static void
umidi20_jack_lock(void)
{
	pthread_mutex_lock(&umidi20_jack_mtx);
}

static void
umidi20_jack_unlock(void)
{
	pthread_mutex_unlock(&umidi20_jack_mtx);
}

static void
umidi20_jack_write(struct umidi20_jack *puj, jack_nframes_t nframes)
{
	int error;
	int events;
	int i;
	void *buf;
	jack_midi_event_t event;

	if (puj->input_port == NULL)
		return;

	if (jack_port_connected(puj->input_port) < 1) {
		int fd;

		umidi20_jack_lock();
		fd = puj->write_fd[1];
		puj->write_fd[1] = -1;
		umidi20_jack_unlock();
		if (fd > -1) {
			DPRINTF("Disconnect\n");
			close(fd);
		}
	}
	buf = jack_port_get_buffer(puj->input_port, nframes);
	if (buf == NULL) {
		DPRINTF("jack_port_get_buffer() failed, "
		    "cannot receive anything.\n");
		return;
	}
#ifdef JACK_MIDI_NEEDS_NFRAMES
	events = jack_midi_get_event_count(buf, nframes);
#else
	events = jack_midi_get_event_count(buf);
#endif

	for (i = 0; i < events; i++) {
#ifdef JACK_MIDI_NEEDS_NFRAMES
		error = jack_midi_event_get(&event, buf, i, nframes);
#else
		error = jack_midi_event_get(&event, buf, i);
#endif
		if (error) {
			DPRINTF("jack_midi_event_get() failed, lost MIDI event.\n");
			continue;
		}
		umidi20_jack_lock();
		if (puj->write_fd[1] > -1) {
			if (write(puj->write_fd[1], event.buffer, event.size) != (int)event.size) {
				DPRINTF("write() failed.\n");
			}
		}
		umidi20_jack_unlock();
	}
}

static const uint8_t umidi20_cmd_to_len[16] = {
	[0x0] = 0,			/* reserved */
	[0x1] = 0,			/* reserved */
	[0x2] = 2,			/* bytes */
	[0x3] = 3,			/* bytes */
	[0x4] = 3,			/* bytes */
	[0x5] = 1,			/* bytes */
	[0x6] = 2,			/* bytes */
	[0x7] = 3,			/* bytes */
	[0x8] = 3,			/* bytes */
	[0x9] = 3,			/* bytes */
	[0xA] = 3,			/* bytes */
	[0xB] = 3,			/* bytes */
	[0xC] = 2,			/* bytes */
	[0xD] = 2,			/* bytes */
	[0xE] = 3,			/* bytes */
	[0xF] = 1,			/* bytes */
};

/*
 * The following statemachine, that converts MIDI commands to
 * USB MIDI packets, derives from Linux's usbmidi.c, which
 * was written by "Clemens Ladisch":
 *
 * Returns:
 *    0: No command
 * Else: Command is complete
 */
static uint8_t
umidi20_convert_to_usb(struct umidi20_jack *puj, uint8_t cn, uint8_t b)
{
	uint8_t p0 = (cn << 4);

	if (b >= 0xf8) {
		puj->parse.temp_0[0] = p0 | 0x0f;
		puj->parse.temp_0[1] = b;
		puj->parse.temp_0[2] = 0;
		puj->parse.temp_0[3] = 0;
		puj->parse.temp_cmd = puj->parse.temp_0;
		return (1);

	} else if (b >= 0xf0) {
		switch (b) {
		case 0xf0:		/* system exclusive begin */
			puj->parse.temp_1[1] = b;
			puj->parse.state = UMIDI20_ST_SYSEX_1;
			break;
		case 0xf1:		/* MIDI time code */
		case 0xf3:		/* song select */
			puj->parse.temp_1[1] = b;
			puj->parse.state = UMIDI20_ST_1PARAM;
			break;
		case 0xf2:		/* song position pointer */
			puj->parse.temp_1[1] = b;
			puj->parse.state = UMIDI20_ST_2PARAM_1;
			break;
		case 0xf4:		/* unknown */
		case 0xf5:		/* unknown */
			puj->parse.state = UMIDI20_ST_UNKNOWN;
			break;
		case 0xf6:		/* tune request */
			puj->parse.temp_1[0] = p0 | 0x05;
			puj->parse.temp_1[1] = 0xf6;
			puj->parse.temp_1[2] = 0;
			puj->parse.temp_1[3] = 0;
			puj->parse.temp_cmd = puj->parse.temp_1;
			puj->parse.state = UMIDI20_ST_UNKNOWN;
			return (1);

		case 0xf7:		/* system exclusive end */
			switch (puj->parse.state) {
			case UMIDI20_ST_SYSEX_0:
				puj->parse.temp_1[0] = p0 | 0x05;
				puj->parse.temp_1[1] = 0xf7;
				puj->parse.temp_1[2] = 0;
				puj->parse.temp_1[3] = 0;
				puj->parse.temp_cmd = puj->parse.temp_1;
				puj->parse.state = UMIDI20_ST_UNKNOWN;
				return (1);
			case UMIDI20_ST_SYSEX_1:
				puj->parse.temp_1[0] = p0 | 0x06;
				puj->parse.temp_1[2] = 0xf7;
				puj->parse.temp_1[3] = 0;
				puj->parse.temp_cmd = puj->parse.temp_1;
				puj->parse.state = UMIDI20_ST_UNKNOWN;
				return (1);
			case UMIDI20_ST_SYSEX_2:
				puj->parse.temp_1[0] = p0 | 0x07;
				puj->parse.temp_1[3] = 0xf7;
				puj->parse.temp_cmd = puj->parse.temp_1;
				puj->parse.state = UMIDI20_ST_UNKNOWN;
				return (1);
			}
			puj->parse.state = UMIDI20_ST_UNKNOWN;
			break;
		}
	} else if (b >= 0x80) {
		puj->parse.temp_1[1] = b;
		if ((b >= 0xc0) && (b <= 0xdf)) {
			puj->parse.state = UMIDI20_ST_1PARAM;
		} else {
			puj->parse.state = UMIDI20_ST_2PARAM_1;
		}
	} else {			/* b < 0x80 */
		switch (puj->parse.state) {
		case UMIDI20_ST_1PARAM:
			if (puj->parse.temp_1[1] < 0xf0) {
				p0 |= puj->parse.temp_1[1] >> 4;
			} else {
				p0 |= 0x02;
				puj->parse.state = UMIDI20_ST_UNKNOWN;
			}
			puj->parse.temp_1[0] = p0;
			puj->parse.temp_1[2] = b;
			puj->parse.temp_1[3] = 0;
			puj->parse.temp_cmd = puj->parse.temp_1;
			return (1);
		case UMIDI20_ST_2PARAM_1:
			puj->parse.temp_1[2] = b;
			puj->parse.state = UMIDI20_ST_2PARAM_2;
			break;
		case UMIDI20_ST_2PARAM_2:
			if (puj->parse.temp_1[1] < 0xf0) {
				p0 |= puj->parse.temp_1[1] >> 4;
				puj->parse.state = UMIDI20_ST_2PARAM_1;
			} else {
				p0 |= 0x03;
				puj->parse.state = UMIDI20_ST_UNKNOWN;
			}
			puj->parse.temp_1[0] = p0;
			puj->parse.temp_1[3] = b;
			puj->parse.temp_cmd = puj->parse.temp_1;
			return (1);
		case UMIDI20_ST_SYSEX_0:
			puj->parse.temp_1[1] = b;
			puj->parse.state = UMIDI20_ST_SYSEX_1;
			break;
		case UMIDI20_ST_SYSEX_1:
			puj->parse.temp_1[2] = b;
			puj->parse.state = UMIDI20_ST_SYSEX_2;
			break;
		case UMIDI20_ST_SYSEX_2:
			puj->parse.temp_1[0] = p0 | 0x04;
			puj->parse.temp_1[3] = b;
			puj->parse.temp_cmd = puj->parse.temp_1;
			puj->parse.state = UMIDI20_ST_SYSEX_0;
			return (1);
		default:
			break;
		}
	}
	return (0);
}

static void
umidi20_jack_read(struct umidi20_jack *puj, jack_nframes_t nframes)
{
	uint8_t *buffer;
	void *buf;
	jack_nframes_t t;
	uint8_t data[1];
	uint8_t len;

	if (puj->output_port == NULL)
		return;

	if (jack_port_connected(puj->output_port) < 1) {
		int fd;

		umidi20_jack_lock();
		fd = puj->read_fd[0];
		puj->read_fd[0] = -1;
		umidi20_jack_unlock();
		if (fd > -1) {
			DPRINTF("Disconnect\n");
			close(fd);
		}
	}
	buf = jack_port_get_buffer(puj->output_port, nframes);
	if (buf == NULL) {
		DPRINTF("jack_port_get_buffer() failed, cannot "
		    "send anything.\n");
		return;
	}
#ifdef JACK_MIDI_NEEDS_NFRAMES
	jack_midi_clear_buffer(buf, nframes);
#else
	jack_midi_clear_buffer(buf);
#endif

	t = 0;
	umidi20_jack_lock();
	if (puj->read_fd[0] > -1) {
		while ((t < nframes) &&
		    (read(puj->read_fd[0], data, sizeof(data)) == sizeof(data))) {
			if (umidi20_convert_to_usb(puj, 0, data[0])) {

				len = umidi20_cmd_to_len[puj->parse.temp_cmd[0] & 0xF];
				if (len == 0)
					continue;
#ifdef JACK_MIDI_NEEDS_NFRAMES
				buffer = jack_midi_event_reserve(buf, t, len, nframes);
#else
				buffer = jack_midi_event_reserve(buf, t, len);
#endif
				if (buffer == NULL) {
					DPRINTF("jack_midi_event_reserve() failed, "
					    "MIDI event lost\n");
					break;
				}
				memcpy(buffer, &puj->parse.temp_cmd[1], len);
				t++;
			}
		}
	}
	umidi20_jack_unlock();
}

static int
umidi20_process_callback(jack_nframes_t nframes, void *reserved)
{
	uint8_t n;

	/*
	 * Check for impossible condition that actually happened to me,
	 * caused by some problem between jackd and OSS4.
	 */
	if (nframes <= 0) {
		DPRINTF("Process callback called with nframes = 0\n");
		return (0);
	}
	for (n = 0; n != UMIDI20_N_DEVICES; n++) {
		umidi20_jack_read(umidi20_jack + n, nframes);
		umidi20_jack_write(umidi20_jack + n, nframes);
	}
	return (0);
}

const char **
umidi20_jack_alloc_inputs(void)
{
	const char **ptr;
	int n;

	if (umidi20_jack_init_done == 0)
		return (0);

	ptr = jack_get_ports(umidi20_jack_client, NULL,
	    JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);

	if (ptr != NULL) {
		for (n = 0; ptr[n] != NULL; n++) {
			if (strstr(ptr[n], umidi20_jack_name) == ptr[n])
				ptr[n] = "";
		}
	}
	return (ptr);
}

const char **
umidi20_jack_alloc_outputs(void)
{
	const char **ptr;
	int n;

	if (umidi20_jack_init_done == 0)
		return (0);

	ptr = jack_get_ports(umidi20_jack_client, NULL,
	    JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput);

	if (ptr != NULL) {
		for (n = 0; ptr[n] != NULL; n++) {
			if (strstr(ptr[n], umidi20_jack_name) == ptr[n])
				ptr[n] = "";
		}
	}
	return (ptr);
}

void
umidi20_jack_free_inputs(const char **ports)
{
	jack_free(ports);
}

void
umidi20_jack_free_outputs(const char **ports)
{
	jack_free(ports);
}

int
umidi20_jack_rx_open(uint8_t n, const char *name)
{
	struct umidi20_jack *puj;
	int error;

	if (n >= UMIDI20_N_DEVICES || umidi20_jack_init_done == 0)
		return (-1);

	/* don't allow connecting with self */
	if (strstr(name, umidi20_jack_name) == name)
		return (-1);

	puj = &umidi20_jack[n];

	/* check if already opened */
	if (puj->write_fd[1] > -1 || puj->write_fd[0] > -1)
		return (-1);

	/* disconnect */
	error = jack_port_disconnect(umidi20_jack_client, puj->input_port);
	if (error)
		return (-1);

	/* connect */
	error = jack_connect(umidi20_jack_client, name,
	    jack_port_name(puj->input_port));
	if (error)
		return (-1);

	/* create looback pipe */
	umidi20_jack_lock();
	error = umidi20_pipe(puj->write_fd);
	umidi20_jack_unlock();

	if (error) {
		jack_port_disconnect(umidi20_jack_client, puj->input_port);
		return (-1);
	}
	return (puj->write_fd[0]);
}

int
umidi20_jack_tx_open(uint8_t n, const char *name)
{
	struct umidi20_jack *puj;
	int error;

	if (n >= UMIDI20_N_DEVICES || umidi20_jack_init_done == 0)
		return (-1);

	/* don't allow connecting with self */
	if (strstr(name, umidi20_jack_name) == name)
		return (-1);

	puj = &umidi20_jack[n];

	/* check if already opened */
	if (puj->read_fd[1] > -1 || puj->read_fd[0] > -1)
		return (-1);

	/* disconnect */
	error = jack_port_disconnect(umidi20_jack_client, puj->output_port);
	if (error)
		return (-1);

	/* connect */
	error = jack_connect(umidi20_jack_client,
	    jack_port_name(puj->output_port), name);
	if (error)
		return (-1);

	/* create looback pipe */
	umidi20_jack_lock();
	error = umidi20_pipe(puj->read_fd);
	if (error == 0) {
		fcntl(puj->read_fd[0], F_SETFL, (int)O_NONBLOCK);
		memset(&puj->parse, 0, sizeof(puj->parse));
	}
	umidi20_jack_unlock();

	if (error) {
		jack_port_disconnect(umidi20_jack_client, puj->output_port);
		return (-1);
	}
	return (puj->read_fd[1]);
}

int
umidi20_jack_rx_close(uint8_t n)
{
	struct umidi20_jack *puj;

	if (n >= UMIDI20_N_DEVICES || umidi20_jack_init_done == 0)
		return (-1);

	puj = &umidi20_jack[n];

	jack_port_disconnect(umidi20_jack_client, puj->input_port);

	umidi20_jack_lock();
	close(puj->write_fd[0]);
	close(puj->write_fd[1]);
	puj->write_fd[0] = -1;
	puj->write_fd[1] = -1;
	umidi20_jack_unlock();

	return (0);
}

int
umidi20_jack_tx_close(uint8_t n)
{
	struct umidi20_jack *puj;

	if (n >= UMIDI20_N_DEVICES || umidi20_jack_init_done == 0)
		return (-1);

	puj = &umidi20_jack[n];

	jack_port_disconnect(umidi20_jack_client, puj->output_port);

	umidi20_jack_lock();
	close(puj->read_fd[0]);
	close(puj->read_fd[1]);
	puj->read_fd[0] = -1;
	puj->read_fd[1] = -1;
	umidi20_jack_unlock();

	return (0);
}

static void
umidi20_jack_shutdown(void *arg)
{
	struct umidi20_jack *puj;
	int n;

	umidi20_jack_lock();
	for (n = 0; n != UMIDI20_N_DEVICES; n++) {
		puj = &umidi20_jack[n];
		if (puj->read_fd[0] > -1) {
			close(puj->read_fd[0]);
			puj->read_fd[0] = -1;
		}
		if (puj->write_fd[1] > -1) {
			close(puj->write_fd[1]);
			puj->write_fd[1] = -1;
		}
	}
	umidi20_jack_init_done = 0;
	umidi20_jack_unlock();
}

int
umidi20_jack_init(const char *name)
{
	struct umidi20_jack *puj;
	uint8_t n;
	int error;
	char devname[64];

	if (name == NULL)
		return (-1);

	umidi20_jack_name = strdup(name);
	if (umidi20_jack_name == NULL)
		return (-1);

	pthread_mutex_init(&umidi20_jack_mtx, NULL);

	umidi20_jack_client = jack_client_open(umidi20_jack_name,
	    JackNoStartServer, NULL);
	if (umidi20_jack_client == NULL)
		return (-1);

	error = jack_set_process_callback(umidi20_jack_client,
	    umidi20_process_callback, 0);
	if (error)
		return (-1);

	jack_on_shutdown(umidi20_jack_client, umidi20_jack_shutdown, 0);

	for (n = 0; n != UMIDI20_N_DEVICES; n++) {
		puj = &umidi20_jack[n];
		puj->read_fd[0] = -1;
		puj->read_fd[1] = -1;
		puj->write_fd[0] = -1;
		puj->write_fd[1] = -1;

		snprintf(devname, sizeof(devname), "dev%d.TX", (int)n);

		puj->output_port = jack_port_register(
		    umidi20_jack_client, devname, JACK_DEFAULT_MIDI_TYPE,
		    JackPortIsOutput, 0);

		snprintf(devname, sizeof(devname), "dev%d.RX", (int)n);

		puj->input_port = jack_port_register(
		    umidi20_jack_client, devname, JACK_DEFAULT_MIDI_TYPE,
		    JackPortIsInput, 0);
	}

	if (jack_activate(umidi20_jack_client))
		return (-1);

	umidi20_jack_init_done = 1;

	return (0);
}
