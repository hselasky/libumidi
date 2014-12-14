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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <err.h>
#include <sysexits.h>

#import <CoreMIDI/CoreMIDI.h>

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

struct umidi20_coremidi {
	MIDIPortRef output_port;
	MIDIPortRef input_port;
	MIDIEndpointRef output_endpoint;
	MIDIEndpointRef input_endpoint;
	int	read_fd[2];
	int	write_fd[2];
	char   *read_name;
	char   *write_name;
	struct umidi20_parse parse;
};

static pthread_mutex_t umidi20_coremidi_mtx;
static pthread_t umidi20_coremidi_thread;
static MIDIClientRef umidi20_coremidi_client;
static struct umidi20_coremidi umidi20_coremidi[UMIDI20_N_DEVICES];
static int umidi20_coremidi_init_done;
static const char *umidi20_coremidi_name;

#ifdef HAVE_DEBUG
#define	DPRINTF(fmt, ...) \
    printf("%s:%d: " fmt, __FUNCTION__, __LINE__,## __VA_ARGS__)
#else
#define	DPRINTF(fmt, ...) do { } while (0)
#endif

static char *
umidi20_dup_cfstr(CFStringRef str)
{
	char *ptr;
	CFIndex len = CFStringGetLength(str) + 1;

	ptr = malloc(len);
	if (ptr == NULL)
		return (NULL);
	CFStringGetCString(str, ptr, len, kCFStringEncodingMacRoman);
	ptr[len - 1] = 0;
	return (ptr);
}

static CFStringRef
umidi20_create_cfstr(const char *str)
{
	return (CFStringCreateWithCString(kCFAllocatorDefault,
	    str, kCFStringEncodingMacRoman));
}

static void
umidi20_coremidi_lock(void)
{
	pthread_mutex_lock(&umidi20_coremidi_mtx);
}

static void
umidi20_coremidi_unlock(void)
{
	pthread_mutex_unlock(&umidi20_coremidi_mtx);
}

static void
umidi20_read_event(const MIDIPacketList * pktList, void *refCon, void *connRefCon)
{
	struct umidi20_coremidi *puj = refCon;
	uint32_t n;

	umidi20_coremidi_lock();
	if (puj->write_fd[1] > -1) {
		const MIDIPacket *packet = &pktList->packet[0];

		for (n = 0; n != pktList->numPackets; n++) {
			write(puj->write_fd[1], packet->data, packet->length);
			packet = MIDIPacketNext(packet);
		}
	}
	umidi20_coremidi_unlock();
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
umidi20_convert_to_usb(struct umidi20_coremidi *puj, uint8_t cn, uint8_t b)
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

static void *
umidi20_write_process(void *arg)
{
	MIDIPacketList pktList;
	MIDIPacket *pkt;
	uint8_t data[1];
	uint8_t len;
	int n;

	while (1) {
		umidi20_coremidi_lock();
		for (n = 0; n != UMIDI20_N_DEVICES; n++) {
			struct umidi20_coremidi *puj = umidi20_coremidi + n;

			if (puj->read_fd[0] > -1) {
				while (read(puj->read_fd[0], data, sizeof(data)) == sizeof(data)) {
					if (umidi20_convert_to_usb(puj, 0, data[0])) {
						len = umidi20_cmd_to_len[puj->parse.temp_cmd[0] & 0xF];
						if (len == 0)
							continue;

						pkt = MIDIPacketListInit(&pktList);
						pkt = MIDIPacketListAdd(&pktList, sizeof(pktList),
						    pkt, 0, len, &puj->parse.temp_cmd[1]);
						MIDISend(puj->output_port,
						    puj->output_endpoint, &pktList);
					}
				}
			}
		}
		umidi20_coremidi_unlock();

		usleep(1000);
	}
}

static void
umidi20_coremidi_uniq_inputs(char **ptr)
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

const char **
umidi20_coremidi_alloc_outputs(void)
{
	unsigned long n;
	unsigned long x;
	unsigned long z;
	char **ptr;

	if (umidi20_coremidi_init_done == 0)
		return (0);

	n = MIDIGetNumberOfSources();

	ptr = malloc(sizeof(void *) * (n + 1));
	if (ptr == NULL)
		return (NULL);

	for (z = x = 0; x != n; x++) {
		CFStringRef name;
		MIDIEndpointRef src = MIDIGetSource(x);

		if (src == NULL)
			continue;

		if (noErr == MIDIObjectGetStringProperty(src,
		    kMIDIPropertyName, &name))
			ptr[z++] = umidi20_dup_cfstr(name);
	}
	ptr[z] = NULL;

	umidi20_coremidi_uniq_inputs(ptr);

	return (ptr);
}

const char **
umidi20_coremidi_alloc_inputs(void)
{
	unsigned long n;
	unsigned long x;
	unsigned long z;
	char **ptr;

	if (umidi20_coremidi_init_done == 0)
		return (0);

	n = MIDIGetNumberOfDestinations();

	ptr = malloc(sizeof(void *) * (n + 1));
	if (ptr == NULL)
		return (NULL);

	for (z = x = 0; x != n; x++) {
		CFStringRef name;
		MIDIEndpointRef dst = MIDIGetDestination(x);

		if (dst == NULL)
			continue;

		if (noErr == MIDIObjectGetStringProperty(dst,
		    kMIDIPropertyName, &name))
			ptr[z++] = umidi20_dup_cfstr(name);
	}
	ptr[z] = NULL;

	umidi20_coremidi_uniq_inputs(ptr);

	return (ptr);
}

void
umidi20_coremidi_free_outputs(const char **ports)
{
	unsigned long n;

	if (ports == NULL)
		return;

	for (n = 0; ports[n] != NULL; n++)
		free(ports[n]);

	free(ports);
}

void
umidi20_coremidi_free_inputs(const char **ports)
{
	unsigned long n;

	if (ports == NULL)
		return;

	for (n = 0; ports[n] != NULL; n++)
		free(ports[n]);

	free(ports);
}

static int
umidi20_coremidi_compare_dev_string(CFStringRef str, const char *name, int *pidx)
{
	char *ptr;
	char *tmp;
	char *cpy;
	int which;

	ptr = umidi20_dup_cfstr(str);
	if (ptr == NULL)
		return (0);

	tmp = strchr(ptr, '#');
	if (tmp != NULL)
		*tmp = 0;

	cpy = strdup(name);
	if (cpy == NULL) {
		free(ptr);
		return (0);
	}
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
			free(ptr);
			free(cpy);
			return (1);
		}
		(*pidx)++;
	}
	free(ptr);
	free(cpy);
	return (0);
}

int
umidi20_coremidi_rx_open(uint8_t n, const char *name)
{
	struct umidi20_coremidi *puj;
	MIDIEndpointRef src = 0;
	unsigned long x;
	unsigned long y;
	int error;
	int index = 0;

	if (n >= UMIDI20_N_DEVICES || umidi20_coremidi_init_done == 0)
		return (-1);

	puj = &umidi20_coremidi[n];

	/* check if already opened */
	if (puj->write_fd[1] > -1 || puj->write_fd[0] > -1)
		return (-1);

	y = MIDIGetNumberOfSources();
	for (x = 0; x != y; x++) {
		CFStringRef str;

		src = MIDIGetSource(x);
		if (noErr == MIDIObjectGetStringProperty(src,
		    kMIDIPropertyName, &str)) {
			if (umidi20_coremidi_compare_dev_string(str, name, &index))
				break;
		}
	}

	if (x == y)
		return (-1);

	umidi20_coremidi_lock();
	puj->input_endpoint = src;
	umidi20_coremidi_unlock();

	MIDIPortConnectSource(puj->input_port, src, NULL);

	/* create looback pipe */
	umidi20_coremidi_lock();
	error = umidi20_pipe(puj->write_fd);
	umidi20_coremidi_unlock();

	if (error) {
		MIDIPortDisconnectSource(puj->input_port, src);
		return (-1);
	}
	return (puj->write_fd[0]);
}

int
umidi20_coremidi_tx_open(uint8_t n, const char *name)
{
	struct umidi20_coremidi *puj;
	MIDIEndpointRef dst = 0;
	unsigned long x;
	unsigned long y;
	int error;
	int index = 0;

	if (n >= UMIDI20_N_DEVICES || umidi20_coremidi_init_done == 0)
		return (-1);

	puj = &umidi20_coremidi[n];

	/* check if already opened */
	if (puj->read_fd[1] > -1 || puj->read_fd[0] > -1)
		return (-1);

	y = MIDIGetNumberOfDestinations();
	for (x = 0; x != y; x++) {
		CFStringRef str;

		dst = MIDIGetDestination(x);
		if (noErr == MIDIObjectGetStringProperty(dst,
		    kMIDIPropertyName, &str)) {
			if (umidi20_coremidi_compare_dev_string(str, name, &index))
				break;
		}
	}

	if (x == y)
		return (-1);

	umidi20_coremidi_lock();
	puj->output_endpoint = dst;
	umidi20_coremidi_unlock();

	MIDIPortConnectSource(puj->output_port, dst, NULL);

	/* create looback pipe */
	umidi20_coremidi_lock();
	error = umidi20_pipe(puj->read_fd);
	if (error == 0) {
		fcntl(puj->read_fd[0], F_SETFL, (int)O_NONBLOCK);
		memset(&puj->parse, 0, sizeof(puj->parse));
	}
	umidi20_coremidi_unlock();

	if (error) {
		MIDIPortDisconnectSource(puj->output_port, dst);
		return (-1);
	}
	return (puj->read_fd[1]);
}

int
umidi20_coremidi_rx_close(uint8_t n)
{
	struct umidi20_coremidi *puj;

	if (n >= UMIDI20_N_DEVICES || umidi20_coremidi_init_done == 0)
		return (-1);

	puj = &umidi20_coremidi[n];

	MIDIPortDisconnectSource(puj->input_port, puj->input_endpoint);

	umidi20_coremidi_lock();
	close(puj->write_fd[0]);
	close(puj->write_fd[1]);
	puj->write_fd[0] = -1;
	puj->write_fd[1] = -1;
	puj->input_endpoint = NULL;
	umidi20_coremidi_unlock();

	return (0);
}

int
umidi20_coremidi_tx_close(uint8_t n)
{
	struct umidi20_coremidi *puj;

	if (n >= UMIDI20_N_DEVICES || umidi20_coremidi_init_done == 0)
		return (-1);

	puj = &umidi20_coremidi[n];

	MIDIPortDisconnectSource(puj->output_port, puj->output_endpoint);

	umidi20_coremidi_lock();
	close(puj->read_fd[0]);
	close(puj->read_fd[1]);
	puj->read_fd[0] = -1;
	puj->read_fd[1] = -1;
	puj->output_endpoint = NULL;
	umidi20_coremidi_unlock();

	return (0);
}

static void
umidi20_coremidi_notify(const MIDINotification *message, void *refCon)
{
	MIDIEndpointRef ref;
	int n;
	int x;
	int y;
	int z;

	if (message->messageID != kMIDIMsgSetupChanged)
		return;
    
	y = MIDIGetNumberOfSources();
	z = MIDIGetNumberOfDestinations();

	umidi20_coremidi_lock();
	for (n = 0; n != UMIDI20_N_DEVICES; n++) {
		struct umidi20_coremidi *puj = umidi20_coremidi + n;

		for (x = 0; x != y; x++) {
			ref = MIDIGetSource(x);
			if (puj->input_endpoint == ref)
				break;
		}
		if (x == y) {
			if (puj->write_fd[1] > -1) {
				close(puj->write_fd[1]);
				puj->write_fd[1] = -1;
				DPRINTF("Closed receiver %d\n", n);
			}
		}

		for (x = 0; x != z; x++) {
			ref = MIDIGetDestination(x);
			if (puj->output_endpoint == ref)
				break;
		}
		if (x == z) {         
			if (puj->read_fd[0] > -1) {
				close(puj->read_fd[0]);
				puj->read_fd[0] = -1;
				DPRINTF("Closed transmitter %d\n", n);
			}
		}
	}
	umidi20_coremidi_unlock();
}

int
umidi20_coremidi_init(const char *name)
{
	struct umidi20_coremidi *puj;
	char devname[64];
	uint8_t n;

	umidi20_coremidi_name = strdup(name);
	if (umidi20_coremidi_name == NULL)
		return (-1);

	pthread_mutex_init(&umidi20_coremidi_mtx, NULL);

	MIDIClientCreate(umidi20_create_cfstr(umidi20_coremidi_name),
	    umidi20_coremidi_notify, NULL, &umidi20_coremidi_client);

	if (umidi20_coremidi_client == 0)
		return (-1);

	for (n = 0; n != UMIDI20_N_DEVICES; n++) {
		puj = &umidi20_coremidi[n];
		puj->read_fd[0] = -1;
		puj->read_fd[1] = -1;
		puj->write_fd[0] = -1;
		puj->write_fd[1] = -1;

		snprintf(devname, sizeof(devname), "midipp.dev%d.TX", (int)n);

		MIDIOutputPortCreate(umidi20_coremidi_client,
		    umidi20_create_cfstr(devname), &puj->output_port);

		snprintf(devname, sizeof(devname), "midipp.dev%d.RX", (int)n);

		MIDIInputPortCreate(umidi20_coremidi_client,
		    umidi20_create_cfstr(devname), umidi20_read_event,
		    puj, &puj->input_port);
	}

	if (pthread_create(&umidi20_coremidi_thread, NULL,
	    &umidi20_write_process, NULL))
		return (-1);

	umidi20_coremidi_init_done = 1;

	return (0);
}
