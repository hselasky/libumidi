/*-
 * Copyright (c) 2003-2006 David G. Slomin. All rights reserved.
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

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umidi20.h"

#define DEBUG(...)

/*
 * Helpers
 */

static uint16_t
interpret_uint16(uint8_t *buffer)
{
	return ((uint16_t)(buffer[0]) << 8) | (uint16_t)(buffer[1]);
}

static uint16_t
read_uint16(FILE *in)
{
	uint8_t buffer[2];
	fread(buffer, 1, 2, in);
	return interpret_uint16(buffer);
}

static void
write_uint16(FILE *out, uint16_t value)
{
	uint8_t buffer[2];
	buffer[0] = (value >> 8) & 0xFF;
	buffer[1] = (value & 0xFF);
	fwrite(buffer, 1, 2, out);
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
read_uint32(FILE *in)
{
	uint8_t buffer[4];
	fread(buffer, 1, 4, in);
	return interpret_uint32(buffer);
}

static void 
write_uint32(FILE *out, uint32_t value)
{
	uint8_t buffer[4];
	buffer[0] = (value >> 24);
	buffer[1] = (value >> 16) & 0xFF;
	buffer[2] = (value >> 8) & 0xFF;
	buffer[3] = (value & 0xFF);
	fwrite(buffer, 1, 4, out);
	return;
}

static uint32_t
read_variable_length_quantity(FILE *in)
{
	uint8_t b;
	uint32_t value = 0;
	uint8_t to = 4;

	do {
	    if (!to--) {
	        break;
	    }

	    b = fgetc(in);
	    value <<= 7;
	    value |= (b & 0x7F);
	} while (b & 0x80);
	return value;
}

static void
write_variable_length_quantity(FILE *out, uint32_t value)
{
	uint8_t buffer[4];
	uint8_t offset = 3;

	while (1) {
	    buffer[offset] = value & 0x7F;
	    if (offset < 3) buffer[offset] |= 0x80;
	    value >>= 7;
	    if ((value == 0) || (offset == 0)) break;
	    offset--;
	}

	fwrite(buffer + offset, 1, 4 - offset, out);
	return;
}

static void
write_event(struct umidi20_event *event, FILE *out, 
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

	fwrite(event->cmd + 1 + offset, 1, part_len, out);

	len -= part_len;
	offset = 0;
        event = event->p_next;
    }
    return;
}

struct umidi20_song *
umidi20_load_file(pthread_mutex_t *p_mtx, const char *filename)
{
	struct umidi20_song * song = NULL;
	struct umidi20_track * track = NULL;
	struct umidi20_event * event = NULL;
	FILE *in = NULL;
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

	if ((filename == NULL) || 
	    ((in = fopen(filename, "rb")) == NULL)) {
	    goto error;
	}

	fread(chunk_id, 1, 4, in);
	chunk_size = read_uint32(in);
	chunk_start = ftell(in);

	/* check for the RMID variation on SMF */

	if (memcmp(chunk_id, "RIFF", 4) == 0)
	{
		/* technically this one is a type id rather 
		 * than a chunk id, but we'll reuse 
		 * the buffer anyway:
		 */
		fread(chunk_id, 1, 4, in);

		if (memcmp(chunk_id, "RMID", 4) != 0) {
		    goto error;
		}

		fread(chunk_id, 1, 4, in);
		chunk_size = read_uint32(in);

		if (memcmp(chunk_id, "data", 4) != 0) {
		    goto error;
		}

		fread(chunk_id, 1, 4, in);
		chunk_size = read_uint32(in);
		chunk_start = ftell(in);
	}

	if (memcmp(chunk_id, "MThd", 4) != 0) {
	    goto error;
	}

	file_format = read_uint16(in);

	number_of_tracks = read_uint16(in);

	fread(division_type_and_resolution, 1, 2, in);

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

	if (song == NULL) {
	    goto error;
	}

	strlcpy(song->filename, filename, sizeof(song->filename));

	/* forwards compatibility:  skip over any extra header data */
	fseek(in, chunk_start + chunk_size, SEEK_SET);

	while (number_of_tracks_read < number_of_tracks) {

	    fread(chunk_id, 1, 4, in);

	    chunk_size = read_uint32(in);

	    chunk_start = ftell(in);

	    if (memcmp(chunk_id, "MTrk", 4) == 0) {

		uint8_t *data_ptr;
		uint32_t data_len;
	        uint32_t tick = 0;
		uint8_t status;
		uint8_t running_status = 0;
		uint8_t at_end_of_track = 0;

		track = umidi20_track_alloc();

		if (track == NULL) {
		    goto error;
		}

		while ((((uint32_t)ftell(in)) < 
			(chunk_start + chunk_size)) && 
		       (!at_end_of_track)) {

		    tick += read_variable_length_quantity(in);

		    status = fgetc(in);

		    if (status & 0x80) {
		        running_status = status;
		    } else {
		        status = running_status;
			fseek(in, -1, SEEK_CUR);
		    }

		    DEBUG("tick %u, status 0x%02x\n", tick, status);

		    temp[0] = status;

		    switch (status >> 4) {
		    case 0x8: /* note off */
		    case 0x9: /* note on */
		    case 0xA: /* key pressure event */
		    case 0xB: /* control change */
		    case 0xE: /* pitch wheel event */

		      temp[1] = fgetc(in) & 0x7F;
		      temp[2] = fgetc(in) & 0x7F;

		      event = umidi20_event_from_data(temp, 3, flag);
		      if (event == NULL) {
			  goto error;
		      }
		      break;

		    case 0xC: /* program change */
		    case 0xD: /* channel pressure */
		      temp[1] = fgetc(in) & 0x7F;

		      event = umidi20_event_from_data(temp, 2, flag);
		      if (event == NULL) {
			  goto error;
		      }
		      break;

		    case 0xF:
		      switch (status) {
		      case 0xF1: /* MIDI time code */
		      case 0xF3: /* song select */
			  fgetc(in);
			  break;

		      case 0xF2: /* song position pointer */
			  fgetc(in);
			  fgetc(in);
			  break;

		      case 0xF6: /* tune request */
			  break;

		      case 0xF0: /* System Exclusive Begin */
		      case 0xF7: /* System Exclusive End */
			  data_len = read_variable_length_quantity(in);
			  data_ptr = malloc(data_len + 2);

			  if (data_ptr == NULL) {
			      goto error;
			  }

			  data_ptr[0] = 0xF0;
			  data_ptr[data_len + 1] = 0xF7;
			  fread(data_ptr + 1, 1, data_len, in);

			  event = umidi20_event_from_data(data_ptr, data_len + 2, flag);
			  free(data_ptr);
			  if (event == NULL) {
			      goto error;
			  }
			  break;

		      case 0xFF:
			  temp[1] = fgetc(in) & 0x7F;
			  data_len = read_variable_length_quantity(in);
			  data_ptr = malloc(data_len + 2);

			  if (data_ptr == NULL) {
			      goto error;
			  }

			  fread(data_ptr + 2, 1, data_len, in);

			  data_ptr[0] = 0xFF;
			  data_ptr[1] = temp[1];

			  if ((temp[1] == 0x51) && 
			      (number_of_tracks_read != 0)) {

			      /* discard tempo information */
			      free(data_ptr);

			  } else if (temp[1] == 0x2F) {
			    //MidiFileTrack_setEndTick(track, tick);
			      at_end_of_track = 1;
			      free(data_ptr);
			  } else {
			      event = umidi20_event_from_data(data_ptr, data_len + 2, flag);
			      free(data_ptr);

			      if (event == NULL) {
				  goto error;
			      }
			  }
			  break;
		      }
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
	    /* forwards compatibility:  
	     * skip over any unrecognized chunks, 
	     * or extra data at the end of tracks:
	     */
	    fseek(in, chunk_start + chunk_size, SEEK_SET);
	}

	fclose(in);

	umidi20_song_recompute_position(song);

	return song;

 error:
	umidi20_song_free(song);
	umidi20_track_free(track);
	umidi20_event_free(event);

	if (in) {
	    fclose(in);
	}
	return NULL;
}

uint8_t
umidi20_save_file(struct umidi20_song *song, const char* filename)
{
	FILE *out = NULL;
	struct umidi20_track *track;
	struct umidi20_event *event;
	struct umidi20_event *event_next;
	uint32_t track_size_offset;
	uint32_t track_start_offset;
	uint32_t track_end_offset;
	uint32_t tick;
	uint32_t previous_tick;
	uint32_t data_len;

	if (song == NULL) {
	    goto error;
	}
	if (filename == NULL) {
	    filename = song->filename;
	}
	if (filename[0] == '\0') {
	    goto error;
	}
	if ((out = fopen(filename, "wb")) == NULL) {
	    goto error;
	}

	pthread_mutex_assert(song->p_mtx, MA_OWNED);

	umidi20_song_recompute_tick(song);

	fwrite("MThd", 1, 4, out);
	write_uint32(out, 6); /* header length */
	write_uint16(out, song->midi_file_format); /* file format */
	write_uint16(out, song->queue.ifq_len); /* number of tracks */

	switch (song->midi_division_type) {
	case UMIDI20_FILE_DIVISION_TYPE_PPQ:
	    write_uint16(out, song->midi_resolution);
	    break;
	case UMIDI20_FILE_DIVISION_TYPE_SMPTE24:
	    fputc(-24, out);
	    fputc(song->midi_resolution, out);
	    break;
	case UMIDI20_FILE_DIVISION_TYPE_SMPTE25:
	    fputc(-25, out);
	    fputc(song->midi_resolution, out);
	    break;
	case UMIDI20_FILE_DIVISION_TYPE_SMPTE30DROP:
	    fputc(-29, out);
	    fputc(song->midi_resolution, out);
	    break;
	case UMIDI20_FILE_DIVISION_TYPE_SMPTE30:
	    fputc(-30, out);
	    fputc(song->midi_resolution, out);
	    break;
	default:
	    goto error;
	}

	UMIDI20_QUEUE_FOREACH(track, &(song->queue)) {

	    fwrite("MTrk", 1, 4, out);

	    track_size_offset = ftell(out);
	    write_uint32(out, 0); /* overwritten later */

	    track_start_offset = ftell(out);

	    previous_tick = 0;

	    UMIDI20_QUEUE_FOREACH_SAFE(event, &(track->queue), event_next) {

	      if (((event->cmd[1] & 0xF0) == 0xF0) &&
		  (event->cmd[1] != 0xFF) &&
		  (event->cmd[1] != 0xF0)) {
		  /* ignore commands */
		  continue;
	      }

	      tick = event->tick;
	      write_variable_length_quantity(out, tick - previous_tick);
	      previous_tick = tick;

	      switch(event->cmd[1] >> 4) {
	      case 0x8: /* note off */
	      case 0x9: /* note on */
	      case 0xA: /* key pressure event */
	      case 0xB: /* control change */
	      case 0xE: /* pitch wheel event */
		  fputc(event->cmd[1], out);
		  fputc(event->cmd[2] & 0x7F, out);
		  fputc(event->cmd[3] & 0x7F, out);
		  break;

	      case 0xC: /* program change */
	      case 0xD: /* channel pressure */
		  fputc(event->cmd[1], out);
		  fputc(event->cmd[2] & 0x7F, out);
		  break;

	      case 0xF:
		  switch(event->cmd[1]) {
		  case 0xF0: /* System Exclusive Begin */
		      fputc(0xF0, out);
		      data_len = umidi20_event_get_length(event);
		      write_variable_length_quantity(out, data_len - 2);
		      write_event(event, out, 1, data_len - 2);
		      break;

		  case 0xFF: /* Meta Event */
		      fputc(0xFF, out);
		      fputc(event->cmd[2] & 0x7F, out);
		      data_len = umidi20_event_get_length(event);
		      write_variable_length_quantity(out, data_len - 1);
		      write_event(event, out, 1, data_len - 1);
		      break;
		  default:
		      fputc(0xFE, out); /* dummy */
		      break;
		  }
		  break;
	      default:
		  fputc(0xFE, out); /* dummy */
		  break;
	      }
	    }

	    write_variable_length_quantity(out, 0);
	    fwrite("\xFF\x2F\x00", 1, 3, out);

	    track_end_offset = ftell(out);

	    fseek(out, track_size_offset, SEEK_SET);
	    write_uint32(out, track_end_offset - track_start_offset);

	    fseek(out, track_end_offset, SEEK_SET);
	}

	fclose(out);
	return 0;

 error:
	if (out) {
	  fclose(out);
	}
	return 1;
}
