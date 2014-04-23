/*-
 * Copyright (c) 2003-2006 David G. Slomin. All rights reserved.
 * Copyright (c) 2006-2011 Hans Petter Selasky. All rights reserved.
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
#include <stdlib.h>
#include <string.h>

#include "umidi20.h"

#ifdef HAVE_DEBUG
#define	DPRINTF(fmt, ...) \
    printf("%s:%d: " fmt, __FUNCTION__, __LINE__,## __VA_ARGS__)
#else
#define	DPRINTF(fmt, ...) do { } while (0)
#endif

/*
 * File helpers
 */

struct midi_file {
	uint8_t *ptr;
	uint32_t end;
	uint32_t off;
};

static void
midi_write_multi(struct midi_file *pmf, const void *ptr, uint32_t len)
{
	uint32_t rem;

	rem = pmf->end - pmf->off;
	if (len > rem)
		len = rem;

	if (pmf->ptr != NULL)
		memcpy(pmf->ptr + pmf->off, ptr, len);

	pmf->off += len;
}

static void
midi_write_1(struct midi_file *pmf, uint8_t val)
{
	if (pmf->end == pmf->off)
		return;

	if (pmf->ptr != NULL)
		((uint8_t *)pmf->ptr)[pmf->off] = val;

	pmf->off += 1;
}

static void
midi_read_multi(struct midi_file *pmf, void *ptr, uint32_t len)
{
	uint32_t rem;

	rem = pmf->end - pmf->off;
	if (len > rem) {
		memset((char *)ptr + rem, 0, len - rem);
		len = rem;
	}
	memcpy(ptr, pmf->ptr + pmf->off, len);

	pmf->off += len;
}

static uint8_t
midi_read_1(struct midi_file *pmf)
{
	if (pmf->off == pmf->end)
		return (0);

	pmf->off += 1;

	return (((uint8_t *)pmf->ptr)[pmf->off - 1]);
}

static uint8_t
midi_read_peek_1(struct midi_file *pmf)
{
	if (pmf->off == pmf->end)
		return (0);

	return (((uint8_t *)pmf->ptr)[pmf->off]);
}

static void
midi_seek_set(struct midi_file *pmf, uint32_t off)
{
	if (off > pmf->end)
		off = pmf->end;

	pmf->off = off;
}

static uint32_t
midi_offset(struct midi_file *pmf)
{
	return (pmf->off);
}

/*
 * MIDI helpers
 */

static uint16_t
interpret_uint16(uint8_t *buffer)
{
	return ((uint16_t)(buffer[0]) << 8) | (uint16_t)(buffer[1]);
}

static uint16_t
read_uint16(struct midi_file *in)
{
	uint8_t buffer[2];

	midi_read_multi(in, buffer, 2);

	return (interpret_uint16(buffer));
}

static void
write_uint16(struct midi_file *out, uint16_t value)
{
	uint8_t buffer[2];

	buffer[0] = (value >> 8) & 0xFF;
	buffer[1] = (value & 0xFF);
	midi_write_multi(out, buffer, 2);
}

static uint32_t
interpret_uint32(uint8_t *buffer)
{
	return (((uint32_t)(buffer[0]) << 24) |
	    ((uint32_t)(buffer[1]) << 16) |
	    ((uint32_t)(buffer[2]) << 8) |
	    (uint32_t)(buffer[3]));
}

static uint32_t
read_uint32(struct midi_file *in)
{
	uint8_t buffer[4];

	midi_read_multi(in, buffer, 4);

	return (interpret_uint32(buffer));
}

static void
write_uint32(struct midi_file *out, uint32_t value)
{
	uint8_t buffer[4];

	buffer[0] = (value >> 24);
	buffer[1] = (value >> 16) & 0xFF;
	buffer[2] = (value >> 8) & 0xFF;
	buffer[3] = (value & 0xFF);
	midi_write_multi(out, buffer, 4);
}

static uint32_t
read_variable_length_quantity(struct midi_file *in)
{
	uint32_t value = 0;
	uint8_t to = 4;
	uint8_t b;

	do {
		if (!to--)
			break;

		b = midi_read_1(in);
		value <<= 7;
		value |= (b & 0x7F);

	} while (b & 0x80);

	return (value);
}

static void
write_variable_length_quantity(struct midi_file *out, uint32_t value)
{
	uint8_t buffer[4];
	uint8_t offset = 3;

	while (1) {
		buffer[offset] = value & 0x7F;
		if (offset < 3)
			buffer[offset] |= 0x80;
		value >>= 7;
		if ((value == 0) || (offset == 0))
			break;
		offset--;
	}

	midi_write_multi(out, buffer + offset, 4 - offset);
}

static void
write_event(struct umidi20_event *event, struct midi_file *out,
    uint32_t offset, uint32_t len)
{
	uint32_t part_len;

	while (offset > 0) {
		part_len = umidi20_command_to_len[event->cmd[0] & 0xF];

		if (offset < part_len) {
			break;
		}
		offset -= part_len;
		event = event->p_next;
	}

	while (len > 0) {

		part_len = umidi20_command_to_len[event->cmd[0] & 0xF];
		part_len -= offset;

		if (part_len > len) {
			part_len = len;
		}
		midi_write_multi(out, event->cmd + 1 + offset, part_len);

		len -= part_len;
		offset = 0;
		event = event->p_next;
	}
}

struct umidi20_song *
umidi20_load_file(pthread_mutex_t *p_mtx, const uint8_t *ptr, uint32_t len)
{
	struct midi_file in[1];
	struct umidi20_song *song = NULL;
	struct umidi20_track *track = NULL;
	struct umidi20_event *event = NULL;
	uint32_t chunk_size;
	uint32_t chunk_start;
	uint16_t number_of_tracks;
	uint16_t number_of_tracks_read = 0;
	uint16_t file_format;
	uint16_t resolution;
	uint8_t chunk_id[4];
	uint8_t division_type_and_resolution[4];
	uint8_t div_type;
	uint8_t temp[4];
	uint8_t flag = 0;

	if (ptr == NULL || len == 0)
		goto error;

	/* init input file */
	in[0].ptr = (uint8_t *)(long)ptr;
	in[0].end = len;
	in[0].off = 0;

	midi_read_multi(in, chunk_id, 4);
	chunk_size = read_uint32(in);
	chunk_start = midi_offset(in);

	/* check for the RMID variation on SMF */

	if (memcmp(chunk_id, "RIFF", 4) == 0) {
		/*
		 * technically this one is a type id rather than a chunk id,
		 * but we'll reuse the buffer anyway:
		 */
		midi_read_multi(in, chunk_id, 4);

		if (memcmp(chunk_id, "RMID", 4) != 0)
			goto error;

		midi_read_multi(in, chunk_id, 4);

		chunk_size = read_uint32(in);

		if (memcmp(chunk_id, "data", 4) != 0)
			goto error;

		midi_read_multi(in, chunk_id, 4);

		chunk_size = read_uint32(in);
		chunk_start = midi_offset(in);
	}
	if (memcmp(chunk_id, "MThd", 4) != 0)
		goto error;

	file_format = read_uint16(in);

	number_of_tracks = read_uint16(in);

	midi_read_multi(in, division_type_and_resolution, 2);

	switch ((int8_t)(division_type_and_resolution[0])) {
	case -24:
		div_type = UMIDI20_FILE_DIVISION_TYPE_SMPTE24;
		resolution = division_type_and_resolution[1];
		break;
	case -25:
		div_type = UMIDI20_FILE_DIVISION_TYPE_SMPTE25;
		resolution = division_type_and_resolution[1];
		break;
	case -29:
		div_type = UMIDI20_FILE_DIVISION_TYPE_SMPTE30DROP;
		resolution = division_type_and_resolution[1];
		break;
	case -30:
		div_type = UMIDI20_FILE_DIVISION_TYPE_SMPTE30;
		resolution = division_type_and_resolution[1];
		break;
	default:
		div_type = UMIDI20_FILE_DIVISION_TYPE_PPQ;
		resolution = interpret_uint16(division_type_and_resolution);
		break;
	}

	song = umidi20_song_alloc(p_mtx, file_format, resolution, div_type);

	if (song == NULL)
		goto error;

	/* forwards compatibility:  skip over any extra header data */
	midi_seek_set(in, chunk_start + chunk_size);

	while (number_of_tracks_read < number_of_tracks) {

		midi_read_multi(in, chunk_id, 4);

		chunk_size = read_uint32(in);

		chunk_start = midi_offset(in);

		if (memcmp(chunk_id, "MTrk", 4) == 0) {

			uint8_t *data_ptr;
			uint32_t data_len;
			uint32_t tick = 0;
			uint8_t status;
			uint8_t running_status = 0;
			uint8_t at_end_of_track = 0;

			track = umidi20_track_alloc();

			if (track == NULL)
				goto error;

			while ((midi_offset(in) <
			    (chunk_start + chunk_size)) &&
			    (!at_end_of_track)) {

				tick += read_variable_length_quantity(in);

				status = midi_read_peek_1(in);

				if (status & 0x80)
					running_status = midi_read_1(in);
				else
					status = running_status;

				DPRINTF("tick %u, status 0x%02x\n", tick, status);

				temp[0] = status;

				switch (status >> 4) {
				case 0x8:	/* note off */
				case 0x9:	/* note on */
				case 0xA:	/* key pressure event */
				case 0xB:	/* control change */
				case 0xE:	/* pitch wheel event */

					temp[1] = midi_read_1(in) & 0x7F;
					temp[2] = midi_read_1(in) & 0x7F;

					event = umidi20_event_from_data(temp, 3, flag);
					if (event == NULL)
						goto error;
					break;

				case 0xC:	/* program change */
				case 0xD:	/* channel pressure */
					temp[1] = midi_read_1(in) & 0x7F;

					event = umidi20_event_from_data(temp, 2, flag);
					if (event == NULL)
						goto error;
					break;

				case 0xF:
					switch (status) {
					case 0xF1:	/* MIDI time code */
					case 0xF3:	/* song select */
						temp[1] = midi_read_1(in) & 0x7F;

						event = umidi20_event_from_data(temp, 2, flag);
						if (event == NULL)
							goto error;
						break;
					case 0xF2:	/* song position pointer */
						temp[1] = midi_read_1(in) & 0x7F;
						temp[2] = midi_read_1(in) & 0x7F;

						event = umidi20_event_from_data(temp, 3, flag);
						if (event == NULL)
							goto error;
						break;
					case 0xF8:	/* beat */
					case 0xFA:	/* song start */
					case 0xFB:	/* song continue */
					case 0xFC:	/* song stop */
						event = umidi20_event_from_data(temp, 1, flag);
						if (event == NULL)
							goto error;
						break;
					case 0xF0:	/* System Exclusive
							 * Begin */
					case 0xF7:	/* System Exclusive End */
						data_len = read_variable_length_quantity(in);
						data_ptr = malloc(data_len + 2);

						if (data_ptr == NULL)
							goto error;

						data_ptr[0] = 0xF0;
						data_ptr[data_len + 1] = 0xF7;
						midi_read_multi(in, data_ptr + 1, data_len);

						event = umidi20_event_from_data(data_ptr, data_len + 2, flag);
						free(data_ptr);
						if (event == NULL)
							goto error;
						break;
					case 0xFF:
						temp[1] = midi_read_1(in) & 0x7F;
						data_len = read_variable_length_quantity(in);
						data_ptr = malloc(data_len + 2);

						if (data_ptr == NULL)
							goto error;

						midi_read_multi(in, data_ptr + 2, data_len);

						data_ptr[0] = 0xFF;
						data_ptr[1] = temp[1];

						if ((temp[1] == 0x51) &&
						    (number_of_tracks_read != 0)) {

							/*
							 * discard tempo
							 * information
							 */
							free(data_ptr);

						} else if (temp[1] == 0x2F) {
							/*
							 * Set end tick
							 */
							at_end_of_track = 1;
							free(data_ptr);
						} else {
							event = umidi20_event_from_data(data_ptr, data_len + 2, flag);
							free(data_ptr);
							if (event == NULL)
								goto error;
						}
						break;
					default:
						break;
					}
					break;
				default:
					break;
				}

				if (event) {
					event->position = tick;
					event->tick = tick;
					umidi20_event_queue_insert(&(track->queue), event,
					    UMIDI20_CACHE_INPUT);
					event = NULL;
				}
			}
			number_of_tracks_read++;
			umidi20_song_track_add(song, NULL, track, 0);
			track = NULL;
		}
		/*
		 * forwards compatibility:  skip over any unrecognized
		 * chunks, or extra data at the end of tracks:
		 */
		midi_seek_set(in, chunk_start + chunk_size);
	}

	umidi20_song_recompute_position(song);
	return (song);

error:
	umidi20_song_free(song);
	umidi20_track_free(track);
	umidi20_event_free(event);
	return (NULL);
}

static uint8_t
umidi20_save_file_sub(struct umidi20_song *song, struct midi_file *out)
{
	struct umidi20_track *track;
	struct umidi20_event *event;
	struct umidi20_event *event_next;
	uint32_t track_size_offset;
	uint32_t track_start_offset;
	uint32_t track_end_offset;
	uint32_t tick;
	uint32_t previous_tick;
	uint32_t data_len;

	if (song == NULL)
		goto error;

	pthread_mutex_assert(song->p_mtx, MA_OWNED);

	umidi20_song_recompute_tick(song);

	midi_write_multi(out, "MThd", 4);
	write_uint32(out, 6);		/* header length */
	write_uint16(out, song->midi_file_format);	/* file format */
	write_uint16(out, song->queue.ifq_len);	/* number of tracks */

	switch (song->midi_division_type) {
	case UMIDI20_FILE_DIVISION_TYPE_PPQ:
		write_uint16(out, song->midi_resolution);
		break;
	case UMIDI20_FILE_DIVISION_TYPE_SMPTE24:
		midi_write_1(out, -24);
		midi_write_1(out, song->midi_resolution);
		break;
	case UMIDI20_FILE_DIVISION_TYPE_SMPTE25:
		midi_write_1(out, -25);
		midi_write_1(out, song->midi_resolution);
		break;
	case UMIDI20_FILE_DIVISION_TYPE_SMPTE30DROP:
		midi_write_1(out, -29);
		midi_write_1(out, song->midi_resolution);
		break;
	case UMIDI20_FILE_DIVISION_TYPE_SMPTE30:
		midi_write_1(out, -30);
		midi_write_1(out, song->midi_resolution);
		break;
	default:
		goto error;
	}

	UMIDI20_QUEUE_FOREACH(track, &(song->queue)) {

		midi_write_multi(out, "MTrk", 4);

		track_size_offset = midi_offset(out);

		/* this field is written later */
		write_uint32(out, 0);

		track_start_offset = midi_offset(out);

		previous_tick = 0;

		UMIDI20_QUEUE_FOREACH_SAFE(event, &(track->queue), event_next) {
			switch (event->cmd[1]) {
			case 0xF4:
			case 0xF5:
			case 0xF6:
			case 0xF7:
			case 0xF9:
			case 0xFD:
			case 0xFE:
				/* ignore commands */
				continue;
			default:
				break;
			}
			tick = event->tick;
			write_variable_length_quantity(out, tick - previous_tick);
			previous_tick = tick;

			switch (event->cmd[1] >> 4) {
			case 0x8:	/* note off */
			case 0x9:	/* note on */
			case 0xA:	/* key pressure event */
			case 0xB:	/* control change */
			case 0xE:	/* pitch wheel event */
				midi_write_1(out, event->cmd[1]);
				midi_write_1(out, event->cmd[2] & 0x7F);
				midi_write_1(out, event->cmd[3] & 0x7F);
				break;

			case 0xC:	/* program change */
			case 0xD:	/* channel pressure */
				midi_write_1(out, event->cmd[1]);
				midi_write_1(out, event->cmd[2] & 0x7F);
				break;
			case 0xF:
				switch (event->cmd[1]) {
				case 0xF0:	/* System Exclusive Begin */
					midi_write_1(out, 0xF0);
					data_len = umidi20_event_get_length(event);
					write_variable_length_quantity(out, data_len - 2);
					write_event(event, out, 1, data_len - 2);
					break;
				case 0xF1:	/* MIDI time code */
					midi_write_1(out, 0xF1);
					midi_write_1(out, event->cmd[2] & 0x7F);
					break;
				case 0xF2:	/* MIDI song position */
					midi_write_1(out, 0xF2);
					midi_write_1(out, event->cmd[2] & 0x7F);
					midi_write_1(out, event->cmd[3] & 0x7F);
					break;
				case 0xF3:	/* song select */
					midi_write_1(out, 0xF3);
					midi_write_1(out, event->cmd[2] & 0x7F);
					break;
				case 0xF8:	/* beat */
					midi_write_1(out, 0xF8);
					break;
				case 0xFA:	/* song start */
					midi_write_1(out, 0xFA);
					break;
				case 0xFB:	/* song continue */
					midi_write_1(out, 0xFB);
					break;
				case 0xFC:	/* song stop */
					midi_write_1(out, 0xFC);
					break;
				case 0xFF:	/* Meta Event */
					midi_write_1(out, 0xFF);
					midi_write_1(out, event->cmd[2] & 0x7F);
					data_len = umidi20_event_get_length(event);
					write_variable_length_quantity(out, data_len - 1);
					write_event(event, out, 1, data_len - 1);
					break;
				default:
					midi_write_1(out, 0xFE);	/* dummy */
					break;
				}
				break;
			default:
				midi_write_1(out, 0xFE);	/* dummy */
				break;
			}
		}

		write_variable_length_quantity(out, 0);

		midi_write_multi(out, "\xFF\x2F\x00", 3);

		track_end_offset = midi_offset(out);

		midi_seek_set(out, track_size_offset);

		write_uint32(out, track_end_offset - track_start_offset);

		midi_seek_set(out, track_end_offset);
	}
	return (0);

error:
	return (1);
}

/* Must be called having the song locked. */
uint8_t
umidi20_save_file(struct umidi20_song *song, uint8_t **pptr, uint32_t *plen)
{
	struct midi_file out;
	uint32_t len;

	out.ptr = NULL;
	out.end = -1U;
	out.off = 0;

	if (umidi20_save_file_sub(song, &out))
		return (1);

	len = out.off;

	*pptr = out.ptr = malloc(len);
	*plen = out.end = len;

	if (out.ptr == NULL)
		return (1);

	out.off = 0;

	umidi20_save_file_sub(song, &out);

	return (0);
}
