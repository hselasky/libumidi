/*-
 * Copyright (c) 2006 Hans Petter Selasky. All rights reserved.
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
#include <string.h>

#include "umidi20.h"

/*
 * This file implements simple MIDI music generator functions.
 *
 * Recommended software synths:
 * - Fluidsynth
 * - Timidity++
 */

static const uint8_t mid_next_key_tab[12] = {
	2, 2, 2, 3, 1,
	2, 2, 2, 2, 2, 3, 1,
};

static const uint8_t mid_prev_key_tab[12] = {
	1, 3, 2, 2, 2,
	1, 3, 2, 2, 2, 2, 2,
};

const char *mid_key_str[128] = {
	"C0", "D0B", "D0", "E0B", "E0", "F0", "G0B", "G0", "A0B", "A0", "H0B", "H0",
	"C1", "D1B", "D1", "E1B", "E1", "F1", "G1B", "G1", "A1B", "A1", "H1B", "H1",
	"C2", "D2B", "D2", "E2B", "E2", "F2", "G2B", "G2", "A2B", "A2", "H2B", "H2",
	"C3", "D3B", "D3", "E3B", "E3", "F3", "G3B", "G3", "A3B", "A3", "H3B", "H3",
	"C4", "D4B", "D4", "E4B", "E4", "F4", "G4B", "G4", "A4B", "A4", "H4B", "H4",
	"C5", "D5B", "D5", "E5B", "E5", "F5", "G5B", "G5", "A5B", "A5", "H5B", "H5",
	"C6", "D6B", "D6", "E6B", "E6", "F6", "G6B", "G6", "A6B", "A6", "H6B", "H6",
	"C7", "D7B", "D7", "E7B", "E7", "F7", "G7B", "G7", "A7B", "A7", "H7B", "H7",
	"C8", "D8B", "D8", "E8B", "E8", "F8", "G8B", "G8", "A8B", "A8", "H8B", "H8",
	"C9", "D9B", "D9", "E9B", "E9", "F9", "G9B", "G9", "A9B", "A9", "H9B", "H9",
	"C10", "D10B", "D10", "E10B", "E10", "F10", "G10B", "G10",
};

void
mid_set_device_no(struct mid_data *d, uint8_t device_no)
{
	uint8_t enable;

	if (device_no >= UMIDI20_N_DEVICES)
		enable = 0;
	else
		enable = 1;

	d->cc_enabled = enable;
	d->cc_device_no = device_no;
}

void
mid_sort(uint8_t *pk, uint8_t nk)
{
	uint8_t a;
	uint8_t b;
	uint8_t c;

	for (a = 0; a != nk; a++) {
		for (b = a + 1; b != nk; b++) {
			if (pk[a] > pk[b]) {
				c = pk[b];
				pk[b] = pk[a];
				pk[a] = c;
			}
		}
	}
}

void
mid_trans(uint8_t *pk, uint8_t nk, int8_t nt)
{
	uint8_t temp;

	if (nk == 0)
		return;

	mid_sort(pk, nk);

	if (nt < 0) {
		while (nt++) {
			temp = pk[nk - 1];
			do {
				temp = mid_sub(temp, 12);
				if (temp == UMIDI20_KEY_INVALID)
					return;
			} while (temp >= pk[0]);
			pk[nk - 1] = temp;
			mid_sort(pk, nk);
		}
	} else {
		while (nt--) {
			temp = pk[0];
			do {
				temp = mid_add(temp, 12);
				if (temp == UMIDI20_KEY_INVALID)
					return;
			} while (temp <= pk[nk - 1]);
			pk[0] = temp;
			mid_sort(pk, nk);
		}
	}
}

uint8_t
mid_add(uint8_t a, uint8_t b)
{
	int16_t t = a + b;

	if (t > 127)
		t = UMIDI20_KEY_INVALID;
	return (t);
}

uint8_t
mid_sub(uint8_t a, uint8_t b)
{
	int16_t t = a - b;

	if (t < 0)
		t = UMIDI20_KEY_INVALID;
	return (t);
}

uint8_t
mid_next_key(uint8_t key, int8_t n)
{
	uint8_t temp;
	if (n > 0) {
		while (n--) {
			temp = mid_add(key, mid_next_key_tab[key % 12]);
			if (temp == UMIDI20_KEY_INVALID)
				break;
			key = temp;
		}
	} else {
		while (n++) {
			temp = mid_sub(key, mid_prev_key_tab[key % 12]);
			if (temp == UMIDI20_KEY_INVALID)
				break;
			key = temp;
		}
	}
	return (key);
}

void
mid_dump(struct mid_data *d)
{
	struct umidi20_event *event;

	uint32_t last_pos = 0;
	uint32_t delta;
	uint8_t new_pedal;
	uint8_t pedal_down = 0;

	umidi20_track_compute_max_min(d->track);

	UMIDI20_QUEUE_FOREACH(event, &(d->track->queue)) {

		delta = event->position - last_pos;

		if (umidi20_event_get_channel(event) != 0) {
			continue;
		}
		if (umidi20_event_is_key_start(event)) {

			if (delta > 30) {
				last_pos = event->position;
				printf("\t" "mid_delay(d,%d);\n", delta);
			}
			printf("\t" "mid_key_press(d,%s,%d,%d);\n",
			    mid_key_str[umidi20_event_get_key(event)],
			    umidi20_event_get_velocity(event),
			    event->duration);
		} else if (umidi20_event_get_control_address(event) == 0x40) {

			/* pedal */

			new_pedal = (umidi20_event_get_control_value(event) >= 0x40);

			if (new_pedal != pedal_down) {
				pedal_down = new_pedal;

				if (delta > 30) {
					last_pos = event->position;
					printf("\t" "mid_delay(d,%d);\n", delta);
				}
				printf("\t" "mid_pedal(d,%d);\n", new_pedal);
			}
		}
	}
}

void
mid_add_raw(struct mid_data *d, const uint8_t *buf,
    uint32_t len, uint32_t offset)
{
	struct umidi20_event *event;

	event = umidi20_event_from_data(buf, len, 0);
	if (event) {
		event->position = d->position[d->channel] + offset;
		event->cmd[1] |= (d->channel & 0xF);

		if (d->cc_enabled) {
			/*
			 * Need to lock the root device before adding
			 * entries to the play queue:
			 */
			pthread_mutex_lock(&(root_dev.mutex));
			umidi20_event_queue_insert(&(root_dev.play[d->cc_device_no].queue),
			    event, UMIDI20_CACHE_INPUT);
			pthread_mutex_unlock(&(root_dev.mutex));

		} else {
			umidi20_event_queue_insert(&d->track->queue,
			    event, UMIDI20_CACHE_INPUT);
		}

	} else {
		printf("Lost event: Out of memory\n");
	}
	return;
}

uint32_t
mid_get_position(struct mid_data *d)
{
	return d->position[d->channel];
}

void
mid_set_position(struct mid_data *d, uint32_t pos)
{
	d->position[d->channel] = pos;
}

uint32_t
mid_delay(struct mid_data *d, int32_t off)
{
	return (d->position[d->channel] += off);
}

void
mid_position_ceil(struct mid_data *d, uint16_t channel_mask)
{
	uint32_t min = 0;
	uint8_t x;

	for (x = 0; x < 16; x++) {
		if ((channel_mask & (1 << x)) &&
		    (d->position[x] > min)) {
			min = d->position[x];
		}
	}

	for (x = 0; x < 16; x++) {
		if ((channel_mask & (1 << x))) {
			d->position[x] = min;
		}
	}
}

void
mid_position_floor(struct mid_data *d, uint16_t channel_mask)
{
	uint32_t max = 0 - 1;
	uint8_t x;

	for (x = 0; x < 16; x++) {
		if ((channel_mask & (1 << x)) &&
		    (d->position[x] < max)) {
			max = d->position[x];
		}
	}

	for (x = 0; x < 16; x++) {
		if ((channel_mask & (1 << x))) {
			d->position[x] = max;
		}
	}
}

void
mid_delay_all(struct mid_data *d, int32_t off)
{
	mid_delay(d, off);

	if (off >= 0)
		mid_position_ceil(d, 0 - 1);
	else
		mid_position_floor(d, 0 - 1);
}

void
mid_key_press(struct mid_data *d, uint8_t key, uint8_t vel, uint32_t duration)
{
	uint8_t buf0[4];
	uint8_t buf1[4];

	buf0[0] = 0x90;
	buf1[0] = 0x90;

	buf0[1] = key & 0x7F;
	buf1[1] = key & 0x7F;

	buf0[2] = vel & 0x7F;
	buf1[2] = 0;			/* key off */

	mid_add_raw(d, buf0, 3, 0);

	if (duration != 0 && vel != 0)
		mid_add_raw(d, buf1, 3, duration);
}

void
mid_key_press_n(struct mid_data *d, const uint8_t *pkey, uint8_t nkey,
    uint8_t vel, uint32_t duration)
{
	uint8_t n;

	for (n = 0; n != nkey; n++) {
		mid_key_press(d, pkey[n], vel, duration);
	}
}

void
mid_set_channel(struct mid_data *d, uint8_t channel)
{
	d->channel = channel & 0xF;
}

uint8_t
mid_get_channel(struct mid_data *d)
{
	return (d->channel & 0xF);
}

void
mid_control(struct mid_data *d, uint8_t ctrl, uint8_t val)
{
	uint8_t buf[4];

	buf[0] = 0xB0;
	buf[1] = ctrl & 0x7F;
	buf[2] = val & 0x7F;

	mid_add_raw(d, buf, 3, 0);
}

void
mid_pitch_bend(struct mid_data *d, uint16_t val)
{
	uint8_t buf[4];

	buf[0] = 0xE0;
	buf[1] = val & 0x7F;
	buf[2] = (val >> 7) & 0x7F;

	mid_add_raw(d, buf, 3, 0);
}

void
mid_pedal(struct mid_data *d, uint8_t on)
{
	uint8_t buf[4];

	buf[0] = 0xB0;
	buf[1] = 0x40;
	buf[2] = on ? 127 : 0;

	mid_add_raw(d, buf, 3, 0);
}

void
mid_s_pedal(struct mid_data *d, int32_t db, int32_t dm, int32_t da,
    uint8_t on)
{
	if (db > 0) {
		mid_delay(d, db);
	}
	mid_pedal(d, !on);
	mid_delay(d, dm);
	mid_pedal(d, on);
	if (da > 0) {
		mid_delay(d, da);
	}
}

void
mid_init(struct mid_data *d, struct umidi20_track *track)
{
#if 0
	uint8_t buf[4];
	uint8_t x;
#endif
	memset(d, 0, sizeof(*d));

	d->track = track;

#if 0
	buf[0] = 0xFE;

	for (x = 0; x < 16; x++) {
		mid_set_channel(d, x);
		mid_add_raw(d, buf, 1, 0);
	}

	buf[0] = 0xB0;
	buf[1] = 0x79;
	buf[2] = 0;

	for (x = 0; x < 16; x++) {
		mid_set_channel(d, x);
		mid_add_raw(d, buf, 3, 2);
	}
#endif
}

void
mid_set_bank_program(struct mid_data *d, uint8_t channel, uint16_t bank,
    uint8_t prog)
{
	uint8_t buf[4];

	mid_set_channel(d, channel);

	/* Select the correct Bank and Program Number */

	buf[0] = 0xB0;
	buf[1] = 0x00;
	buf[2] = (bank >> 7) & 0x7F;

	mid_add_raw(d, buf, 3, 0);

	buf[0] = 0xB0;
	buf[1] = 0x20;
	buf[2] = bank & 0x7F;

	mid_add_raw(d, buf, 3, 1);

	buf[0] = 0xC0;
	buf[1] = prog & 0x7F;

	mid_add_raw(d, buf, 2, 2);
}
