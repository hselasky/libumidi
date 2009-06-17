/*-
 * Copyright (c) 2006-2009 Hans Petter Selasky. All rights reserved.
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
#include <string.h>

#include <umidi20.h>

static int32_t
rand_noise(uint32_t prime)
{
	uint32_t temp;
	static uint32_t noise_rem = 1;

	if (prime == 0)
		prime = 0xFFFF1D;

	if (noise_rem & 1) {
		noise_rem += prime;
	}
	noise_rem /= 2;

	temp = noise_rem;

	/* unsigned to signed conversion */

	temp ^= 0x800000;
	if (temp & 0x800000) {
		temp |= (-0x800000);
	}
	return temp;
}

static void
song_011_sub_003(struct mid_data *d, uint8_t key)
{
	uint8_t c;
	uint32_t pos;

	c = mid_get_channel(d);
	pos = mid_get_position(d);
	mid_set_channel(d, TRACK_P);
	mid_set_position(d, pos);
	mid_key_press(d, key, 50, 1000);
	mid_set_channel(d, c);
}

static void
song_011_sub_001(struct mid_data *d, uint8_t vel, uint8_t flag)
{
	static const uint8_t a0[] = {C3, C3, C3, F3, F3, F3, G3, G3, G3, G2, G2, G2};
	static const uint16_t d0[] = {450, 450, 300, 450, 450, 300, 450, 450, 300, 450, 450, 300};
	uint8_t x;

	mid_pedal(d, 0);

	for (x = 0; x < (sizeof(a0) / sizeof(a0[0])); x++) {
		mid_key_press(d, a0[x], vel, d0[x] / 2);
		mid_delay(d, d0[x]);
	}
	printf("sub_001 @ %u\n", mid_get_position(d) / 150);
	return;
}

static void
song_011_sub_002(struct mid_data *d, uint8_t vel, uint8_t flag)
{
	static const uint8_t a0[] = {C3, D3, E3, F3, G3, A3, G3, G2};
	static const uint16_t d0[] = {450, 450, 300, 450, 450, 300, 1200, 1200};
	uint8_t x;

	for (x = 0; x < (sizeof(a0) / sizeof(a0[0])); x++) {
		song_011_sub_003(d, 39);
		mid_key_press(d, a0[x], 80, d0[x] - 50);
		mid_delay(d, d0[x]);
	}

	printf("sub_002 @ %u\n", mid_get_position(d) / 150);
}

static void
song_011(struct mid_data *d)
{
	uint16_t x;
	uint8_t y;
	uint8_t z;
	uint8_t up;
	uint8_t k[3];
	const uint8_t *ptr;
	uint32_t pos;

	enum {
	rounds = 32};

	mid_delay_all(d, 500);

	mid_set_bank_program(d, TRACK_A, 122, 0);
	mid_set_bank_program(d, TRACK_B, 122, 0);
	mid_set_bank_program(d, TRACK_C, 122, 0);
	mid_set_bank_program(d, TRACK_D, 122, 0);
	mid_set_bank_program(d, TRACK_E, 122, 0);

	mid_delay_all(d, 500);

	mid_set_channel(d, TRACK_P);

	pos = mid_get_position(d);

	for (x = 0; x != 1160; x++) {
		if (rand_noise(0) & 1) {
			mid_key_press(d, 42, 15, 100);
		}
		if ((x % 4) == 0) {
			mid_key_press(d, ((x / (4 * 16)) & 1) ?
			    56 : 35, 90, 2000);
		}
		mid_delay(d, 150);
	}

	mid_set_position(d, pos);

	mid_set_channel(d, TRACK_A);

	mid_pedal(d, 0);

	up = 0;

	song_011_sub_001(d, 80, up);
	song_011_sub_001(d, 80, up);
	song_011_sub_002(d, 80, up);
	song_011_sub_002(d, 80, up);

	mid_delay_all(d, 0);

	for (y = 0; y < (rounds / 4); y++) {

		song_011_sub_001(d, 80, up);
		song_011_sub_001(d, 80, up);
		song_011_sub_002(d, 80, up);
		song_011_sub_002(d, 80, up);
	}

	mid_set_channel(d, TRACK_B);

	mid_pedal(d, 1);

	static const uint8_t a10[][3] = {{G4, C5, E5}, {A4, C5, F5},
	{G4, C5, E5}, {G4, H4, D5}};
	static const uint16_t d1[] = {1200, 1200, 1200, 1200};

	for (y = 0; y < rounds; y++) {

		for (x = 0; x < (sizeof(a10) / sizeof(a10[0])); x++) {

			if (y & 8) {
				up = 2;
			} else {
				up = 0;
			}

			ptr = a10[x];

			k[0] = ptr[0];
			k[1] = ptr[1];
			k[2] = ptr[2];

			mid_trans(k, 3, ((y & x & 1) ? 1 :
			    (y & x & 2) ? -1 : 0) + up);

			mid_key_press(d, k[0], 70, d1[x] / 2);
			mid_key_press(d, k[1], 70, d1[x] / 2);
			mid_key_press(d, k[2], 70, d1[x] / 2);

			k[0] = ptr[0];
			k[1] = ptr[1];
			k[2] = ptr[2];

			mid_trans(k, 3, 1 + up);

			z = (y & 7);

			if ((z == 4) || (z == 5)) {
				for (z = 0; z < 8; z++) {
					mid_trans(k, 3, 1);
					mid_key_press(d, k[2], 70, d1[x] / 16);
					mid_delay(d, d1[x] / 8);
				}
			} else {
				mid_s_pedal(d, d1[x] - 100, 50, 50, 1);
			}
		}
	}

	mid_delay_all(d, 0);

	mid_set_channel(d, TRACK_B);

	mid_pedal(d, 0);

	printf("end @ %u\n", mid_get_position(d));

	mid_key_press(d, C2, 70, 2400);
	mid_key_press(d, G4, 70, 2400);
	mid_key_press(d, C4, 70, 2400);
	mid_key_press(d, E4, 70, 2400);
}

int
main(int argc, char **argv)
{
	struct mid_data data;
	struct umidi20_song *song;
	struct umidi20_track *track;
	struct umidi20_config cfg;
	pthread_mutex_t mtx = NULL;

	umidi20_mutex_init(&mtx);

	umidi20_init();

	/* setup the I/O devices */

	umidi20_config_export(&cfg);

	strlcpy(cfg.cfg_dev[0].rec_fname, "/dev/umidi0.0",
	    sizeof(cfg.cfg_dev[0].rec_fname));

	cfg.cfg_dev[0].rec_enabled_cfg = 1;

	strlcpy(cfg.cfg_dev[0].play_fname, "/dev/umidi0.0",
	    sizeof(cfg.cfg_dev[0].play_fname));

	cfg.cfg_dev[0].play_enabled_cfg = 1;

	umidi20_config_import(&cfg);

	pthread_mutex_lock(&mtx);

	song = umidi20_song_alloc(&mtx, UMIDI20_FILE_FORMAT_TYPE_0, 500,
	    UMIDI20_FILE_DIVISION_TYPE_PPQ);

	track = umidi20_track_alloc();

	if (song == NULL) {
		printf("could not allocate new song\n");
		return (0);
	}
	umidi20_song_track_add(song, NULL, track, 0);
	umidi20_song_set_record_track(song, track);

	/* get the line up! */

	mid_init(&data, track);

	mid_delay_all(&data, 1);

	song_011(&data);

	if (umidi20_save_file(song, "hps_0011.mid")) {
		printf("// could not save file\n");
	}
	while (1) {
		int c;

		printf("// playing ... (press enter when finished)\n");

		umidi20_song_start(song, 0, 0x80000000, UMIDI20_FLAG_PLAY |
		    UMIDI20_FLAG_RECORD);

		pthread_mutex_unlock(&mtx);

		c = getchar();

		pthread_mutex_lock(&mtx);

		umidi20_song_stop(song, UMIDI20_FLAG_PLAY |
		    UMIDI20_FLAG_RECORD);
		break;
	}

	mid_dump(&data);

	umidi20_song_free(song);

	pthread_mutex_unlock(&mtx);

	return (0);
}
