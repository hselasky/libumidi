/*-
 * Copyright (c) 2022 Hans Petter Selasky <hselasky@FreeBSD.org>
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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>

#include <alsa/asoundlib.h>

#include "umidi20.h"

struct umidi20_alsa_parse {
	uint8_t *temp_cmd;
	uint8_t	temp_0[4];
	uint8_t	temp_1[4];
	uint8_t	state;
#define	UMIDI20_ALSA_ST_UNKNOWN	 0	/* scan for command */
#define	UMIDI20_ALSA_ST_1PARAM	 1
#define	UMIDI20_ALSA_ST_2PARAM_1	 2
#define	UMIDI20_ALSA_ST_2PARAM_2	 3
#define	UMIDI20_ALSA_ST_SYSEX_0	 4
#define	UMIDI20_ALSA_ST_SYSEX_1	 5
#define	UMIDI20_ALSA_ST_SYSEX_2	 6
};

struct umidi20_alsa {
	struct umidi20_pipe *read_fd;
	struct umidi20_pipe *write_fd;
	snd_seq_addr_t read_addr;
	snd_seq_addr_t write_addr;
	struct umidi20_alsa_parse parse;
};

static snd_seq_t *umidi20_alsa_seq;
static struct umidi20_alsa umidi20_alsa[UMIDI20_N_DEVICES];
static const char *umidi20_alsa_name;
static pthread_mutex_t umidi20_alsa_mtx;
static pthread_cond_t umidi20_alsa_cv;
static uint8_t umidi20_alsa_init_done;
static uint8_t umidi20_alsa_tx_work;

static const uint8_t umidi20_alsa_cmd_to_len[16] = {
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
umidi20_alsa_midi_convert(struct umidi20_alsa_parse *parse, uint8_t cn, uint8_t b)
{
	uint8_t p0 = (cn << 4);

	if (b >= 0xf8) {
		parse->temp_0[0] = p0 | 0x0f;
		parse->temp_0[1] = b;
		parse->temp_0[2] = 0;
		parse->temp_0[3] = 0;
		parse->temp_cmd = parse->temp_0;
		return (1);

	} else if (b >= 0xf0) {
		switch (b) {
		case 0xf0:		/* system exclusive begin */
			parse->temp_1[1] = b;
			parse->state = UMIDI20_ALSA_ST_SYSEX_1;
			break;
		case 0xf1:		/* MIDI time code */
		case 0xf3:		/* song select */
			parse->temp_1[1] = b;
			parse->state = UMIDI20_ALSA_ST_1PARAM;
			break;
		case 0xf2:		/* song position pointer */
			parse->temp_1[1] = b;
			parse->state = UMIDI20_ALSA_ST_2PARAM_1;
			break;
		case 0xf4:		/* unknown */
		case 0xf5:		/* unknown */
			parse->state = UMIDI20_ALSA_ST_UNKNOWN;
			break;
		case 0xf6:		/* tune request */
			parse->temp_1[0] = p0 | 0x05;
			parse->temp_1[1] = 0xf6;
			parse->temp_1[2] = 0;
			parse->temp_1[3] = 0;
			parse->temp_cmd = parse->temp_1;
			parse->state = UMIDI20_ALSA_ST_UNKNOWN;
			return (1);
		case 0xf7:		/* system exclusive end */
			switch (parse->state) {
			case UMIDI20_ALSA_ST_SYSEX_0:
				parse->temp_1[0] = p0 | 0x05;
				parse->temp_1[1] = 0xf7;
				parse->temp_1[2] = 0;
				parse->temp_1[3] = 0;
				parse->temp_cmd = parse->temp_1;
				parse->state = UMIDI20_ALSA_ST_UNKNOWN;
				return (2);
			case UMIDI20_ALSA_ST_SYSEX_1:
				parse->temp_1[0] = p0 | 0x06;
				parse->temp_1[2] = 0xf7;
				parse->temp_1[3] = 0;
				parse->temp_cmd = parse->temp_1;
				parse->state = UMIDI20_ALSA_ST_UNKNOWN;
				return (2);
			case UMIDI20_ALSA_ST_SYSEX_2:
				parse->temp_1[0] = p0 | 0x07;
				parse->temp_1[3] = 0xf7;
				parse->temp_cmd = parse->temp_1;
				parse->state = UMIDI20_ALSA_ST_UNKNOWN;
				return (2);
			}
			parse->state = UMIDI20_ALSA_ST_UNKNOWN;
			break;
		}
	} else if (b >= 0x80) {
		parse->temp_1[1] = b;
		if ((b >= 0xc0) && (b <= 0xdf)) {
			parse->state = UMIDI20_ALSA_ST_1PARAM;
		} else {
			parse->state = UMIDI20_ALSA_ST_2PARAM_1;
		}
	} else {			/* b < 0x80 */
		switch (parse->state) {
		case UMIDI20_ALSA_ST_1PARAM:
			if (parse->temp_1[1] < 0xf0) {
				p0 |= parse->temp_1[1] >> 4;
			} else {
				p0 |= 0x02;
				parse->state = UMIDI20_ALSA_ST_UNKNOWN;
			}
			parse->temp_1[0] = p0;
			parse->temp_1[2] = b;
			parse->temp_1[3] = 0;
			parse->temp_cmd = parse->temp_1;
			return (1);
		case UMIDI20_ALSA_ST_2PARAM_1:
			parse->temp_1[2] = b;
			parse->state = UMIDI20_ALSA_ST_2PARAM_2;
			break;
		case UMIDI20_ALSA_ST_2PARAM_2:
			if (parse->temp_1[1] < 0xf0) {
				p0 |= parse->temp_1[1] >> 4;
				parse->state = UMIDI20_ALSA_ST_2PARAM_1;
			} else {
				p0 |= 0x03;
				parse->state = UMIDI20_ALSA_ST_UNKNOWN;
			}
			parse->temp_1[0] = p0;
			parse->temp_1[3] = b;
			parse->temp_cmd = parse->temp_1;
			return (1);
		case UMIDI20_ALSA_ST_SYSEX_0:
			parse->temp_1[1] = b;
			parse->state = UMIDI20_ALSA_ST_SYSEX_1;
			break;
		case UMIDI20_ALSA_ST_SYSEX_1:
			parse->temp_1[2] = b;
			parse->state = UMIDI20_ALSA_ST_SYSEX_2;
			break;
		case UMIDI20_ALSA_ST_SYSEX_2:
			parse->temp_1[0] = p0 | 0x04;
			parse->temp_1[3] = b;
			parse->temp_cmd = parse->temp_1;
			parse->state = UMIDI20_ALSA_ST_SYSEX_0;
			return (2);
		default:
			break;
		}
	}
	return (0);
}

static void
umidi20_alsa_lock(void)
{
	pthread_mutex_lock(&umidi20_alsa_mtx);
}

static void
umidi20_alsa_unlock(void)
{
	pthread_mutex_unlock(&umidi20_alsa_mtx);
}

static bool
umidi20_alsa_addr_compare(const snd_seq_addr_t *pa, const snd_seq_addr_t *pb)
{
	return (pa->client == pb->client && pa->port == pb->port);
}

static bool
umidi20_alsa_receive_seq_event(struct snd_seq_event *ev,
    struct umidi20_alsa_parse *parse, struct umidi20_pipe **pp)
{
	uint8_t buffer[1];

	while (umidi20_pipe_read_data(pp, buffer, sizeof(buffer)) == 1) {
		switch (umidi20_alsa_midi_convert(parse, 0, buffer[0])) {
		case 0:
			continue;
		case 1:
			break;
		default:
			memset(ev, 0, sizeof(*ev));
			ev->type = SND_SEQ_EVENT_SYSEX;
			ev->flags = SND_SEQ_EVENT_LENGTH_VARIABLE;
			ev->data.ext.len = umidi20_alsa_cmd_to_len[
			    parse->temp_cmd[0] & 0xF];
			ev->data.ext.ptr = parse->temp_cmd + 1;
			return (true);
		}

		memset(ev, 0, sizeof(*ev));
		switch ((parse->temp_cmd[1] & 0xF0) >> 4) {
		case 0x9:
			ev->type = SND_SEQ_EVENT_NOTEON;
			break;
		case 0x8:
			ev->type = SND_SEQ_EVENT_NOTEOFF;
			break;
		case 0xA:
			ev->type = SND_SEQ_EVENT_KEYPRESS;
			break;
		case 0xB:
			ev->type = SND_SEQ_EVENT_CONTROLLER;
			break;
		case 0xC:
			ev->type = SND_SEQ_EVENT_PGMCHANGE;
			break;
		case 0xD:
			ev->type = SND_SEQ_EVENT_CHANPRESS;
			break;
		case 0xE:
			ev->type = SND_SEQ_EVENT_PITCHBEND;
			break;
		case 0xF:
			switch (parse->temp_cmd[1] & 0x0F) {
			case 0x1:
				ev->type = SND_SEQ_EVENT_QFRAME;
				break;
			case 0x2:
				ev->type = SND_SEQ_EVENT_SONGPOS;
				break;
			case 0x3:
				ev->type = SND_SEQ_EVENT_SONGSEL;
				break;
			case 0x6:
				ev->type = SND_SEQ_EVENT_TUNE_REQUEST;
				break;
			case 0x8:
				ev->type = SND_SEQ_EVENT_CLOCK;
				break;
			case 0xA:
				ev->type = SND_SEQ_EVENT_START;
				break;
			case 0xB:
				ev->type = SND_SEQ_EVENT_CONTINUE;
				break;
			case 0xC:
				ev->type = SND_SEQ_EVENT_STOP;
				break;
			case 0xE:
				ev->type = SND_SEQ_EVENT_SENSING;
				break;
			case 0xF:
				ev->type = SND_SEQ_EVENT_RESET;
				break;
			default:
				continue;
			}
			break;
		default:
			continue;
		}

		switch (ev->type) {
		case SND_SEQ_EVENT_NOTEON:
		case SND_SEQ_EVENT_NOTEOFF:
		case SND_SEQ_EVENT_KEYPRESS:
			ev->data.note.channel = parse->temp_cmd[1] & 0xF;
			ev->data.note.note = parse->temp_cmd[2] & 0x7F;
			ev->data.note.velocity = parse->temp_cmd[3] & 0x7F;
			break;
		case SND_SEQ_EVENT_PGMCHANGE:
		case SND_SEQ_EVENT_CHANPRESS:
			ev->data.control.channel = parse->temp_cmd[1] & 0xF;
			ev->data.control.value = parse->temp_cmd[2] & 0x7F;
			break;
		case SND_SEQ_EVENT_CONTROLLER:
			ev->data.control.channel = parse->temp_cmd[1] & 0xF;
			ev->data.control.param = parse->temp_cmd[2] & 0x7F;
			ev->data.control.value = parse->temp_cmd[3] & 0x7F;
			break;
		case SND_SEQ_EVENT_PITCHBEND:
			ev->data.control.channel = parse->temp_cmd[1] & 0xF;
			ev->data.control.value =
			    (parse->temp_cmd[2] & 0x7F) |
			    ((parse->temp_cmd[3] & 0x7F) << 7);
			ev->data.control.value -= 8192;
			break;
		case SND_SEQ_EVENT_QFRAME:
		case SND_SEQ_EVENT_SONGSEL:
			ev->data.control.value = parse->temp_cmd[1] & 0x7F;
			break;
		case SND_SEQ_EVENT_SONGPOS:
			ev->data.control.value = (parse->temp_cmd[1] & 0x7F) |
			    ((parse->temp_cmd[2] & 0x7F) << 7);
			break;
		default:
			break;
		}
		return (true);
	}
	return (false);
}

static void
umidi20_alsa_write_event(struct umidi20_pipe **pp, const snd_seq_event_t *event)
{
	uint8_t buffer[3] = {};
	int len;

	switch (event->type) {
	case SND_SEQ_EVENT_NOTEON:
		buffer[0] |= 0x90;
		break;
	case SND_SEQ_EVENT_NOTEOFF:
		buffer[0] |= 0x80;
		break;
	case SND_SEQ_EVENT_KEYPRESS:
		buffer[0] |= 0xA0;
		break;
	case SND_SEQ_EVENT_CONTROLLER:
		buffer[0] |= 0xB0;
		break;
	case SND_SEQ_EVENT_PGMCHANGE:
		buffer[0] |= 0xC0;
		break;
	case SND_SEQ_EVENT_CHANPRESS:
		buffer[0] |= 0xD0;
		break;
	case SND_SEQ_EVENT_PITCHBEND:
		buffer[0] |= 0xE0;
		break;
	case SND_SEQ_EVENT_SYSEX:
		umidi20_pipe_write_data(pp, event->data.ext.ptr, event->data.ext.len);
		return;
	case SND_SEQ_EVENT_QFRAME:
		buffer[0] |= 0xF1;
		break;
	case SND_SEQ_EVENT_SONGPOS:
		buffer[0] |= 0xF2;
		break;
	case SND_SEQ_EVENT_SONGSEL:
		buffer[0] |= 0xF3;
		break;
	case SND_SEQ_EVENT_TUNE_REQUEST:
		buffer[0] |= 0xF6;
		break;
	case SND_SEQ_EVENT_CLOCK:
		buffer[0] |= 0xF8;
		break;
	case SND_SEQ_EVENT_START:
		buffer[0] |= 0xFA;
		break;
	case SND_SEQ_EVENT_CONTINUE:
		buffer[0] |= 0xFB;
		break;
	case SND_SEQ_EVENT_STOP:
		buffer[0] |= 0xFC;
		break;
	case SND_SEQ_EVENT_SENSING:
		buffer[0] |= 0xFE;
		break;
	case SND_SEQ_EVENT_RESET:
		buffer[0] |= 0xFF;
		break;
	default:
		return;
	}

	switch (event->type) {
	case SND_SEQ_EVENT_NOTEON:
	case SND_SEQ_EVENT_NOTEOFF:
	case SND_SEQ_EVENT_KEYPRESS:
		buffer[0] |= event->data.note.channel & 0xF;
		buffer[1] |= event->data.note.note & 0x7F;
		buffer[2] |= event->data.note.velocity & 0x7F;
		len = 3;
		break;
	case SND_SEQ_EVENT_CHANPRESS:
	case SND_SEQ_EVENT_PGMCHANGE:
		buffer[0] |= event->data.control.channel & 0xF;
		buffer[1] |= event->data.control.value & 0x7F;
		len = 2;
		break;
	case SND_SEQ_EVENT_CONTROLLER:
		buffer[0] |= event->data.control.channel & 0xF;
		buffer[1] |= event->data.control.param & 0x7F;
		buffer[2] |= event->data.control.value & 0x7F;
		len = 3;
		break;
	case SND_SEQ_EVENT_PITCHBEND:
		buffer[0] |= event->data.control.channel & 0xF;
		buffer[1] |= (event->data.control.value + 8192) & 0x7F;
		buffer[2] |= ((event->data.control.value + 8192) >> 7) & 0x7F;
		len = 3;
		break;
	case SND_SEQ_EVENT_QFRAME:
	case SND_SEQ_EVENT_SONGSEL:
		buffer[1] |= event->data.control.value & 0x7F;
		len = 2;
		break;
	case SND_SEQ_EVENT_SONGPOS:
		buffer[1] |= (event->data.control.value & 0x7F);
		buffer[2] |= ((event->data.control.value >> 7) & 0x7F);
		len = 3;
		break;
	default:
		len = 1;
		break;
	}
	umidi20_pipe_write_data(pp, buffer, len);
}

static void *
umidi20_alsa_rx_worker(void *arg)
{
	snd_seq_event_t *event;
	int nfds;
	int err;

	nfds = snd_seq_poll_descriptors_count(umidi20_alsa_seq, POLLIN);

	while (1) {
		struct pollfd fds[nfds];
		int x;

		snd_seq_poll_descriptors(umidi20_alsa_seq, fds, nfds, POLLIN);

		x = poll(fds, nfds, -1);
		if (x < 0)
			continue;

		umidi20_alsa_lock();
		do {
			err = snd_seq_event_input(umidi20_alsa_seq, &event);
			if (err < 0)
				break;
			if (event == NULL)
				continue;
			if (event->type == SND_SEQ_EVENT_PORT_UNSUBSCRIBED) {
				snd_seq_addr_t self;

				self.client = snd_seq_client_id(umidi20_alsa_seq);
				for (x = 0; x != UMIDI20_N_DEVICES; x++) {
					self.port = x;

					if (umidi20_alsa_addr_compare(&umidi20_alsa[x].write_addr, &event->data.connect.sender) &&
					    umidi20_alsa_addr_compare(&self, &event->data.connect.dest)) {
						umidi20_pipe_free(&umidi20_alsa[x].write_fd);
					} else if (umidi20_alsa_addr_compare(&self, &event->data.connect.sender) &&
					    umidi20_alsa_addr_compare(&umidi20_alsa[x].read_addr, &event->data.connect.dest)) {
						umidi20_pipe_free(&umidi20_alsa[x].read_fd);
					}
				}
			} else {
				for (x = 0; x != UMIDI20_N_DEVICES; x++) {
					if (umidi20_alsa[x].write_addr.client != event->source.client ||
					    umidi20_alsa[x].write_addr.port != event->source.port)
						continue;
					umidi20_alsa_write_event(&umidi20_alsa[x].write_fd, event);
				}
			}
			snd_seq_free_event(event);
		} while (err > 0);
		umidi20_alsa_unlock();
	}
	return (NULL);
}

static void *
umidi20_alsa_tx_worker(void *arg)
{
	while (1) {
		umidi20_alsa_lock();
		while (umidi20_alsa_tx_work == 0)
			pthread_cond_wait(&umidi20_alsa_cv, &umidi20_alsa_mtx);
		umidi20_alsa_tx_work = 0;

		for (unsigned x = 0; x != UMIDI20_N_DEVICES; x++) {
			struct snd_seq_event temp;

			while (umidi20_alsa_receive_seq_event(&temp,
			    &umidi20_alsa[x].parse, &umidi20_alsa[x].read_fd)) {
				snd_seq_ev_set_source(&temp, x);
				snd_seq_ev_set_subs(&temp);
				snd_seq_ev_set_direct(&temp);
				snd_seq_event_output(umidi20_alsa_seq, &temp);
			}
		}
		umidi20_alsa_unlock();

		snd_seq_drain_output(umidi20_alsa_seq);
	}
	return (NULL);
}

static void
umidi20_alsa_uniq_inputs(char **ptr)
{
	unsigned long x;
	unsigned long y;
	unsigned long z;
	unsigned long n;
	char *pstr;

	/* remove any hashes from device names */
	for (n = 0; ptr[n]; n++) {
		pstr = strchr(ptr[n], '#');
		if (pstr != NULL)
			*pstr = 0;
	}

	/* make all device names uniqe */
	for (x = 0; x != n; x++) {
		for (z = 0, y = x + 1; y != n; y++) {
			if (strcmp(ptr[x], ptr[y]) == 0) {
				size_t s = strlen(ptr[y]) + 16;

				pstr = ptr[y];
				ptr[y] = malloc(s);
				if (ptr[y] == NULL) {
					ptr[y] = pstr;
					return;
				}
				z++;
				snprintf(ptr[y], s, "%s#%d", pstr, (int)z);
				free(pstr);
			}
		}
	}
}

static const char **
umidi20_alsa_alloc_ports(unsigned mask)
{
	snd_seq_client_info_t *cinfo;
	snd_seq_port_info_t *pinfo;
	char **ptr;
	size_t num = 1;
	size_t n = 0;

	snd_seq_client_info_alloca(&cinfo);
	snd_seq_port_info_alloca(&pinfo);

	snd_seq_client_info_set_client(cinfo, -1);
	while (snd_seq_query_next_client(umidi20_alsa_seq, cinfo) >= 0) {
		int client = snd_seq_client_info_get_client(cinfo);

		snd_seq_port_info_set_client(pinfo, client);
		snd_seq_port_info_set_port(pinfo, -1);
		while (snd_seq_query_next_port(umidi20_alsa_seq, pinfo) >= 0) {
			if ((snd_seq_port_info_get_capability(pinfo) & mask) != mask)
				continue;
			num++;
		}
	}
	if (num == 0)
		return (NULL);

	ptr = malloc(sizeof(ptr[0]) * num);

	snd_seq_client_info_set_client(cinfo, -1);
	while (snd_seq_query_next_client(umidi20_alsa_seq, cinfo) >= 0) {
		int client = snd_seq_client_info_get_client(cinfo);

		/* Skip self. */
		if (strcmp(snd_seq_client_info_get_name(cinfo), umidi20_alsa_name) == 0)
			continue;
		snd_seq_port_info_set_client(pinfo, client);
		snd_seq_port_info_set_port(pinfo, -1);
		while (snd_seq_query_next_port(umidi20_alsa_seq, pinfo) >= 0) {
			if ((snd_seq_port_info_get_capability(pinfo) & mask) != mask)
				continue;
			if (n + 1 < num) {
				asprintf(&ptr[n], "%s:%s",
				    snd_seq_client_info_get_name(cinfo),
				    snd_seq_port_info_get_name(pinfo));
				n++;
			}
		}
	}

	ptr[n] = NULL;

	umidi20_alsa_uniq_inputs(ptr);

	return ((const char **)ptr);
}

static bool
umidi20_alsa_compare_dev_string(char *ptr, const char *name, int *pidx)
{
	char *tmp;
	char *cpy;
	int which;

	tmp = strchr(ptr, '#');
	if (tmp != NULL)
		*tmp = 0;

	cpy = strdup(name);
	if (cpy == NULL)
		return (false);

	tmp = strchr(cpy, '#');
	if (tmp != NULL) {
		which = atoi(tmp + 1);
		*tmp = 0;
	} else {
		which = 0;
	}

	if (strcmp(ptr, cpy) == 0) {
		if (*pidx == which) {
			(*pidx)++;
			free(cpy);
			return (true);
		}
		(*pidx)++;
	}
	free(cpy);
	return (false);
}

static int
umidi20_alsa_find_port(unsigned mask, const char *pname, snd_seq_addr_t *paddr)
{
	snd_seq_client_info_t *cinfo;
	snd_seq_port_info_t *pinfo;
	int index = 0;

	snd_seq_client_info_alloca(&cinfo);
	snd_seq_port_info_alloca(&pinfo);

	snd_seq_client_info_set_client(cinfo, -1);
	while (snd_seq_query_next_client(umidi20_alsa_seq, cinfo) >= 0) {
		int client = snd_seq_client_info_get_client(cinfo);

		snd_seq_port_info_set_client(pinfo, client);
		snd_seq_port_info_set_port(pinfo, -1);
		while (snd_seq_query_next_port(umidi20_alsa_seq, pinfo) >= 0) {
			char *ptr;
			bool found;

			if ((snd_seq_port_info_get_capability(pinfo) & mask) != mask)
				continue;
			asprintf(&ptr, "%s:%s",
			    snd_seq_client_info_get_name(cinfo),
			    snd_seq_port_info_get_name(pinfo));
			found = umidi20_alsa_compare_dev_string(ptr, pname, &index);
			free(ptr);

			if (found) {
				paddr->client = snd_seq_port_info_get_client(pinfo);
				paddr->port = snd_seq_port_info_get_port(pinfo);
				return (0);
			}
		}
	}
	return (-1);
}

const char **
umidi20_alsa_alloc_inputs(void)
{
	if (umidi20_alsa_init_done == 0)
		return (NULL);

	return (umidi20_alsa_alloc_ports(
	    SND_SEQ_PORT_CAP_WRITE |
	    SND_SEQ_PORT_CAP_SUBS_WRITE));
}

const char **
umidi20_alsa_alloc_outputs(void)
{
	if (umidi20_alsa_init_done == 0)
		return (NULL);

	return (umidi20_alsa_alloc_ports(
	    SND_SEQ_PORT_CAP_READ |
	    SND_SEQ_PORT_CAP_SUBS_READ));
}

static void
umidi20_alsa_free(const void *ptr)
{
	free((void *)(uintptr_t)ptr);
}

void
umidi20_alsa_free_inputs(const char **ports)
{
	size_t x;

	if (ports == NULL)
		return;
	for (x = 0; ports[x] != NULL; x++)
		umidi20_alsa_free(ports[x]);
	umidi20_alsa_free(ports);
}

void
umidi20_alsa_free_outputs(const char **ports)
{
	size_t x;

	if (ports == NULL)
		return;
	for (x = 0; ports[x] != NULL; x++)
		umidi20_alsa_free(ports[x]);
	umidi20_alsa_free(ports);
}

struct umidi20_pipe **
umidi20_alsa_rx_open(uint8_t n, const char *name)
{
	struct umidi20_alsa *puj;

	if (n >= UMIDI20_N_DEVICES || umidi20_alsa_init_done == 0)
		return (NULL);

	puj = &umidi20_alsa[n];

	/* check if already opened */
	if (puj->write_fd != NULL)
		return (NULL);

	/* find the port */
	if (umidi20_alsa_find_port(SND_SEQ_PORT_CAP_READ |
	    SND_SEQ_PORT_CAP_SUBS_READ, name, &puj->write_addr) < 0)
		return (NULL);

	umidi20_alsa_lock();
	umidi20_pipe_alloc(&puj->write_fd, NULL);
	umidi20_alsa_unlock();

	/* try to connect */
	if (snd_seq_connect_from(umidi20_alsa_seq, n,
	    puj->write_addr.client, puj->write_addr.port)) {
		umidi20_pipe_free(&puj->write_fd);
		return (NULL);
	}
	return (&puj->write_fd);
}

static void
umidi20_alsa_write_callback(void)
{
	umidi20_alsa_lock();
	umidi20_alsa_tx_work = 1;
	pthread_cond_broadcast(&umidi20_alsa_cv);
	umidi20_alsa_unlock();
}

struct umidi20_pipe **
umidi20_alsa_tx_open(uint8_t n, const char *name)
{
	struct umidi20_alsa *puj;

	if (n >= UMIDI20_N_DEVICES || umidi20_alsa_init_done == 0)
		return (NULL);

	puj = &umidi20_alsa[n];

	/* check if already opened */
	if (puj->read_fd != NULL)
		return (NULL);

	/* find the port */
	if (umidi20_alsa_find_port(SND_SEQ_PORT_CAP_WRITE |
	    SND_SEQ_PORT_CAP_SUBS_WRITE, name, &puj->read_addr) < 0)
		return (NULL);

	umidi20_alsa_lock();
	umidi20_pipe_alloc(&puj->read_fd, &umidi20_alsa_write_callback);
	memset(&puj->parse, 0, sizeof(puj->parse));
	umidi20_alsa_unlock();

	/* try to connect */
	if (snd_seq_connect_to(umidi20_alsa_seq, n,
	    puj->read_addr.client, puj->read_addr.port)) {
		umidi20_pipe_free(&puj->read_fd);
		return (NULL);
	}
	return (&puj->read_fd);
}

int
umidi20_alsa_rx_close(uint8_t n)
{
	struct umidi20_alsa *puj;

	if (n >= UMIDI20_N_DEVICES || umidi20_alsa_init_done == 0)
		return (-1);

	puj = &umidi20_alsa[n];

	snd_seq_disconnect_from(umidi20_alsa_seq, n,
	    puj->write_addr.client, puj->write_addr.port);

	umidi20_pipe_free(&puj->write_fd);

	return (0);
}

int
umidi20_alsa_tx_close(uint8_t n)
{
	struct umidi20_alsa *puj;

	if (n >= UMIDI20_N_DEVICES || umidi20_alsa_init_done == 0)
		return (-1);

	puj = &umidi20_alsa[n];

	snd_seq_disconnect_to(umidi20_alsa_seq, n,
	    puj->read_addr.client, puj->read_addr.port);

	umidi20_pipe_free(&puj->read_fd);

	return (0);
}

int
umidi20_alsa_init(const char *name)
{
	struct umidi20_alsa *puj;
	pthread_t td;
	uint8_t n;

	if (name == NULL)
		return (-1);

	umidi20_alsa_name = strdup(name);
	if (umidi20_alsa_name == NULL)
		return (-1);

	if (snd_seq_open(&umidi20_alsa_seq, "default", SND_SEQ_OPEN_DUPLEX, 0))
		return (-1);

	pthread_mutex_init(&umidi20_alsa_mtx, NULL);
	pthread_cond_init(&umidi20_alsa_cv, NULL);

	/* set non-blocking mode for event handler */
	snd_seq_nonblock(umidi20_alsa_seq, 1);

	snd_seq_set_client_name(umidi20_alsa_seq, umidi20_alsa_name);

	for (n = 0; n != UMIDI20_N_DEVICES; n++) {
		puj = &umidi20_alsa[n];
		puj->read_fd = NULL;
		puj->write_fd = NULL;

		snd_seq_create_simple_port(umidi20_alsa_seq, umidi20_alsa_name,
		    SND_SEQ_PORT_CAP_WRITE |
		    SND_SEQ_PORT_CAP_SUBS_WRITE |
		    SND_SEQ_PORT_CAP_READ |
		    SND_SEQ_PORT_CAP_SUBS_READ,
		    SND_SEQ_PORT_TYPE_MIDI_GENERIC |
		    SND_SEQ_PORT_TYPE_APPLICATION);
	}

	umidi20_alsa_init_done = 1;

	pthread_create(&td, NULL, &umidi20_alsa_rx_worker, NULL);
	pthread_create(&td, NULL, &umidi20_alsa_tx_worker, NULL);

	return (0);
}
