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

#ifndef _UMIDI20_H_
#define	_UMIDI20_H_

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/queue.h>

#include <pthread.h>
#include <signal.h>
#include <stdint.h>

__BEGIN_DECLS

#define	UMIDI20_BPM 60000		/* Beats Per Minute */

#define	UMIDI20_COMMAND_LEN 8		/* bytes, max */
#define	UMIDI20_BUF_EVENTS 1024		/* units */

#define	UMIDI20_N_DEVICES 16		/* units */

#define	UMIDI20_FLAG_PLAY 0x01
#define	UMIDI20_FLAG_RECORD 0x02

#define	UMIDI20_MAX_OFFSET 0x80000000

#define	UMIDI20_BAND_SIZE 24
#define	UMIDI20_KEY_TO_BAND_OFFSET(n) (((n) + 12) % UMIDI20_BAND_SIZE)
#define	UMIDI20_KEY_TO_BAND_NUMBER(n) (((n) + 12) / UMIDI20_BAND_SIZE)

#define	UMIDI20_KEY_INVALID	255

enum {
	UMIDI20_FILE_DIVISION_TYPE_PPQ,
	UMIDI20_FILE_DIVISION_TYPE_SMPTE24,
	UMIDI20_FILE_DIVISION_TYPE_SMPTE25,
	UMIDI20_FILE_DIVISION_TYPE_SMPTE30DROP,
	UMIDI20_FILE_DIVISION_TYPE_SMPTE30
};

enum {
	UMIDI20_FILE_FORMAT_TYPE_0,
	UMIDI20_FILE_FORMAT_TYPE_1,
	UMIDI20_FILE_FORMAT_TYPE_2,
};

#define	UMIDI20_WHAT_CHANNEL          0x0001
#define	UMIDI20_WHAT_KEY              0x0002
#define	UMIDI20_WHAT_VELOCITY         0x0004
#define	UMIDI20_WHAT_KEY_PRESSURE     0x0008
#define	UMIDI20_WHAT_CONTROL_VALUE    0x0010
#define	UMIDI20_WHAT_CONTROL_ADDRESS  0x0020
#define	UMIDI20_WHAT_PROGRAM_VALUE    0x0040
#define	UMIDI20_WHAT_CHANNEL_PRESSURE 0x0080
#define	UMIDI20_WHAT_PITCH_BEND       0x0100
#define	UMIDI20_WHAT_BEAT_EVENT	      0x0200
#define	UMIDI20_WHAT_SONG_EVENT	      0x0400

struct timespec;
struct umidi20_event;
struct umidi20_track;
struct umidi20_song;
struct umidi20_timer_entry;

typedef void (umidi20_event_callback_t)(uint8_t unit, void *arg, struct umidi20_event *event, uint8_t *drop_event);
typedef void (umidi20_timer_callback_t)(void *arg);

/*--------------------------------------------------------------------------*
 * queue structures and macros
 *--------------------------------------------------------------------------*/

#define	UMIDI20_CACHE_INPUT  0
#define	UMIDI20_CACHE_OUTPUT 1
#define	UMIDI20_CACHE_EDIT   2
#define	UMIDI20_CACHE_OTHER  3
#define	UMIDI20_CACHE_MAX    4

struct umidi20_event_queue {
	struct umidi20_event *ifq_head;
	struct umidi20_event *ifq_tail;
	struct umidi20_event *ifq_cache[UMIDI20_CACHE_MAX];

	int32_t	ifq_len;
	int32_t	ifq_maxlen;
};

struct umidi20_track_queue {
	struct umidi20_track *ifq_head;
	struct umidi20_track *ifq_tail;
	struct umidi20_track *ifq_cache[UMIDI20_CACHE_MAX];

	int32_t	ifq_len;
	int32_t	ifq_maxlen;
};

struct umidi20_song_queue {
	struct umidi20_song *ifq_head;
	struct umidi20_song *ifq_tail;
	struct umidi20_song *ifq_cache[UMIDI20_CACHE_MAX];

	int32_t	ifq_len;
	int32_t	ifq_maxlen;
};

#define	UMIDI20_IF_CHECK_CACHE_ONE(ifq,m,n)		\
    if ((ifq)->ifq_cache[n] == (m)) {			\
        (ifq)->ifq_cache[n] = (m)->p_nextpkt;		\
	if ((ifq)->ifq_cache[n] == NULL) {		\
	    (ifq)->ifq_cache[n] = (m)->p_prevpkt;	\
	}						\
    }

#define	UMIDI20_IF_CHECK_CACHE(ifq, m) do {	\
    UMIDI20_IF_CHECK_CACHE_ONE(ifq,m,0);	\
    UMIDI20_IF_CHECK_CACHE_ONE(ifq,m,1);	\
    UMIDI20_IF_CHECK_CACHE_ONE(ifq,m,2);	\
    UMIDI20_IF_CHECK_CACHE_ONE(ifq,m,3);	\
} while (0)

#define	UMIDI20_IF_REMOVE(ifq, m) do {			\
    UMIDI20_IF_CHECK_CACHE(ifq, m);			\
    if ((m)->p_nextpkt) {				\
        (m)->p_nextpkt->p_prevpkt = (m)->p_prevpkt;	\
    } else {						\
        (ifq)->ifq_tail = (m)->p_prevpkt;		\
    }							\
    if ((m)->p_prevpkt) {				\
        (m)->p_prevpkt->p_nextpkt = (m)->p_nextpkt;	\
    } else {						\
        (ifq)->ifq_head = (m)->p_nextpkt;		\
    }							\
    (m)->p_nextpkt = NULL;				\
    (m)->p_prevpkt = NULL;				\
    (ifq)->ifq_len--;					\
  } while (0)

#define	UMIDI20_IF_DEQUEUE(ifq, m) do {                         \
    (m) = (ifq)->ifq_head;                                      \
    if (m) {                                                    \
        UMIDI20_IF_CHECK_CACHE(ifq, m);				\
        if (((ifq)->ifq_head = (m)->p_nextpkt) == NULL) {	\
	    (ifq)->ifq_tail = NULL;				\
        } else {						\
	    (ifq)->ifq_head->p_prevpkt = NULL;			\
	}							\
        (m)->p_nextpkt = NULL;					\
	(m)->p_prevpkt = NULL;					\
        (ifq)->ifq_len--;                                       \
    }                                                           \
  } while (0)

#define	UMIDI20_IF_ENQUEUE_AFTER(ifq, m1, m2) do {	\
    (m2)->p_nextpkt = (m1)->p_nextpkt;			\
    (m2)->p_prevpkt = (m1);				\
    if ((m1) == (ifq)->ifq_tail) {			\
	(ifq)->ifq_tail = (m2);				\
    } else {						\
	(m1)->p_nextpkt->p_prevpkt = (m2);		\
    }							\
    (m1)->p_nextpkt = (m2);				\
    (ifq)->ifq_len++;					\
  } while (0)

#define	UMIDI20_IF_ENQUEUE_BEFORE(ifq, m1, m2) do {	\
    (m2)->p_nextpkt = (m1);				\
    (m2)->p_prevpkt = (m1)->p_prevpkt;			\
    if ((m1) == (ifq)->ifq_head) {			\
	(ifq)->ifq_head = (m2);				\
    } else {						\
	(m1)->p_prevpkt->p_nextpkt = (m2);		\
    }							\
    (m1)->p_prevpkt = (m2);				\
    (ifq)->ifq_len++;					\
  } while (0)

#define	UMIDI20_IF_ENQUEUE_LAST(ifq, m) do {	\
    (m)->p_nextpkt = NULL;			\
    (m)->p_prevpkt = (ifq)->ifq_tail;		\
    if ((ifq)->ifq_tail == NULL) {		\
        (ifq)->ifq_head = (m);                  \
    } else {					\
        (ifq)->ifq_tail->p_nextpkt = (m);	\
    }						\
    (ifq)->ifq_tail = (m);                      \
    (ifq)->ifq_len++;                           \
  } while (0)

#define	UMIDI20_IF_ENQUEUE_FIRST(ifq, m) do {	\
    (m)->p_nextpkt = (ifq)->ifq_head;		\
    (m)->p_prevpkt = NULL;			\
    if ((ifq)->ifq_tail == NULL) {		\
        (ifq)->ifq_tail = (m);			\
    } else {					\
        (ifq)->ifq_head->p_prevkt = (m);	\
    }						\
    (ifq)->ifq_head = (m);			\
    (ifq)->ifq_len++;				\
  } while (0)

#define	UMIDI20_IF_QFULL(ifq)   ((ifq)->ifq_len >= (ifq)->ifq_maxlen)
#define	UMIDI20_IF_QLEN(ifq)    ((ifq)->ifq_len)
#define	UMIDI20_IF_POLL_HEAD(ifq, m) ((m) = (ifq)->ifq_head)
#define	UMIDI20_IF_POLL_TAIL(ifq, m) ((m) = (ifq)->ifq_tail)

#define	UMIDI20_QUEUE_FOREACH(m, ifq)		\
  for ((m) = (ifq)->ifq_head;			\
       (m);					\
       (m) = (m)->p_nextpkt)

#define	UMIDI20_QUEUE_FOREACH_SAFE(m, ifq, m_next)	\
  for ((m) = (ifq)->ifq_head;				\
       (m) && ((m_next) = (m)->p_nextpkt, 1);		\
       (m) = (m_next))


/*--------------------------------------------------------------------------*
 * MIDI command converter
 *--------------------------------------------------------------------------*/
struct umidi20_converter {
	struct umidi20_event **pp_next;
	struct umidi20_event *p_next;

	uint8_t *temp_cmd;
	uint8_t	temp_0[UMIDI20_COMMAND_LEN];
	uint8_t	temp_1[UMIDI20_COMMAND_LEN];
	uint8_t	state;
#define	UMIDI20_ST_UNKNOWN   0		/* scan for command */
#define	UMIDI20_ST_1PARAM    1
#define	UMIDI20_ST_2PARAM_1  2
#define	UMIDI20_ST_2PARAM_2  3
#define	UMIDI20_ST_SYSEX_0   4
#define	UMIDI20_ST_SYSEX_1   5
#define	UMIDI20_ST_SYSEX_2   6
#define	UMIDI20_ST_SYSEX_3   7
#define	UMIDI20_ST_SYSEX_4   8
#define	UMIDI20_ST_SYSEX_5   9
#define	UMIDI20_ST_SYSEX_6  10
};

/*--------------------------------------------------------------------------*
 * MIDI event structure
 *--------------------------------------------------------------------------*/
struct umidi20_event {
	struct umidi20_event *p_nextpkt;
	struct umidi20_event *p_prevpkt;
	struct umidi20_event *p_next;
	uint32_t position;		/* milliseconds */
	uint32_t tick;			/* units */
	uint32_t duration;		/* milliseconds */
	uint16_t revision;		/* unit */
	uint8_t	device_no;		/* device number */
	uint8_t	unused;
	uint8_t	cmd[UMIDI20_COMMAND_LEN];
};

/*--------------------------------------------------------------------------*
 * MIDI config structures
 *--------------------------------------------------------------------------*/
struct umidi20_config_dev {
	char	rec_fname[128];
	char	play_fname[128];
	uint8_t	rec_enabled_cfg;
	uint8_t	play_enabled_cfg;
#define	UMIDI20_DISABLE_CFG 0
#define	UMIDI20_ENABLED_CFG_DEV 1
#define	UMIDI20_ENABLED_CFG_JACK 2
#define	UMIDI20_ENABLED_CFG_COREMIDI 3
};

struct umidi20_config {
	struct umidi20_config_dev cfg_dev[UMIDI20_N_DEVICES];
};

/*--------------------------------------------------------------------------*
 * MIDI device structure
 *--------------------------------------------------------------------------*/
struct umidi20_device {

	struct umidi20_event_queue queue;
	struct umidi20_converter conv;

	umidi20_event_callback_t *event_callback_func;
	void   *event_callback_arg;

	uint32_t start_position;
	uint32_t end_offset;

	int	file_no;		/* file number */

	uint8_t	device_no;		/* device number */

	uint8_t	any_key_start;		/* a key start was transmitted */
	uint8_t	enabled_usr;		/* enabled by user */
	uint8_t	enabled_cfg;		/* enabled by config */
	uint8_t	enabled_cfg_last;	/* last enabled by config */
	uint8_t	update;
	char	fname[128];
};

/*--------------------------------------------------------------------------*
 * MIDI root-device structure
 *--------------------------------------------------------------------------*/
struct umidi20_root_device {
	struct umidi20_device rec[UMIDI20_N_DEVICES];
	struct umidi20_device play[UMIDI20_N_DEVICES];
	struct umidi20_event_queue free_queue;
	struct timespec curr_time;
	struct timespec start_time;
	pthread_mutex_t mutex;

	TAILQ_HEAD(, umidi20_timer_entry) timers;

	pthread_t thread_alloc;
	pthread_t thread_play_rec;
	pthread_t thread_files;

	uint32_t curr_position;
};

extern struct umidi20_root_device root_dev;

/*--------------------------------------------------------------------------*
 * MIDI track structure
 *--------------------------------------------------------------------------*/
struct umidi20_track {
	struct umidi20_event_queue queue;

	struct umidi20_track *p_nextpkt;
	struct umidi20_track *p_prevpkt;

	uint32_t position_max;

	uint8_t	mute_flag;
	uint8_t	selected_flag;
	uint8_t	draw_flag;
	uint8_t	temp_flag;

	uint8_t	key_min;
	uint8_t	key_max;

	uint8_t	band_min;
	uint8_t	band_max;

	uint8_t	name[256];
	uint8_t	instrument[256];
};

/*--------------------------------------------------------------------------*
 * MIDI song structure
 *--------------------------------------------------------------------------*/
struct umidi20_song {
	struct umidi20_track_queue queue;
	struct timespec play_start_time;

	struct umidi20_song *p_nextpkt;
	struct umidi20_song *p_prevpkt;
	struct umidi20_track *rec_track;

	pthread_mutex_t *p_mtx;
	pthread_t thread_io;

	uint32_t play_start_position;
	uint32_t play_end_offset;
	uint32_t play_start_offset;
	uint32_t play_last_offset;
	uint32_t position_max;
	uint32_t track_max;
	uint32_t band_max;

	uint16_t midi_file_format;
	uint16_t midi_resolution;
	uint16_t record_track;

	uint8_t	midi_division_type;

	uint8_t	play_enabled;
	uint8_t	rec_enabled;

	uint8_t	pc_flags;		/* play and record flags */
};

/*--------------------------------------------------------------------------*
 * prototypes from "umidi20.c"
 *--------------------------------------------------------------------------*/

extern uint32_t umidi20_get_curr_position(void);
extern void umidi20_set_record_event_callback(uint8_t device_no, umidi20_event_callback_t *func, void *arg);
extern void umidi20_set_play_event_callback(uint8_t device_no, umidi20_event_callback_t *func, void *arg);
extern void umidi20_init(void);
extern void umidi20_uninit(void);
extern struct umidi20_event *umidi20_event_alloc(struct umidi20_event ***ppp_next, uint8_t flag);
extern void umidi20_event_free(struct umidi20_event *event);
extern struct umidi20_event *umidi20_event_copy(struct umidi20_event *event, uint8_t flag);
extern struct umidi20_event *umidi20_event_from_data(const uint8_t *data_ptr, uint32_t data_len, uint8_t flag);
extern uint32_t umidi20_event_get_what(struct umidi20_event *event);
extern uint8_t umidi20_event_is_meta(struct umidi20_event *event);
extern uint8_t umidi20_event_is_pitch_bend(struct umidi20_event *event);
extern uint8_t umidi20_event_is_key_start(struct umidi20_event *event);
extern uint8_t umidi20_event_is_key_end(struct umidi20_event *event);
extern uint8_t umidi20_event_is_tempo(struct umidi20_event *event);
extern uint8_t umidi20_event_is_voice(struct umidi20_event *event);
extern uint8_t umidi20_event_is_sysex(struct umidi20_event *event);
extern uint8_t umidi20_event_get_channel(struct umidi20_event *event);
extern void umidi20_event_set_channel(struct umidi20_event *event, uint8_t c);
extern uint8_t umidi20_event_get_key(struct umidi20_event *event);
extern void umidi20_event_set_key(struct umidi20_event *event, uint8_t k);
extern uint8_t umidi20_event_get_velocity(struct umidi20_event *event);
extern void umidi20_event_set_velocity(struct umidi20_event *event, uint8_t v);
extern uint8_t umidi20_event_get_pressure(struct umidi20_event *event);
extern void umidi20_event_set_pressure(struct umidi20_event *event, uint8_t p);
extern uint8_t umidi20_event_get_control_address(struct umidi20_event *event);
extern void umidi20_event_set_control_address(struct umidi20_event *event, uint8_t a);
extern uint8_t umidi20_event_get_control_value(struct umidi20_event *event);
extern void umidi20_event_set_control_value(struct umidi20_event *event, uint8_t a);
extern uint8_t umidi20_event_get_program_number(struct umidi20_event *event);
extern void umidi20_event_set_program_number(struct umidi20_event *event, uint8_t n);
extern uint16_t umidi20_event_get_pitch_value(struct umidi20_event *event);
extern void umidi20_event_set_pitch_value(struct umidi20_event *event, uint16_t n);
extern uint32_t umidi20_event_get_length_first(struct umidi20_event *event);
extern uint32_t umidi20_event_get_length(struct umidi20_event *event);
extern void umidi20_event_copy_out(struct umidi20_event *event, uint8_t *dst, uint32_t offset, uint32_t len);
extern uint8_t umidi20_event_get_meta_number(struct umidi20_event *event);
extern void umidi20_event_set_meta_number(struct umidi20_event *event, uint8_t n);
extern uint32_t umidi20_event_get_tempo(struct umidi20_event *event);
extern void umidi20_event_set_tempo(struct umidi20_event *event, uint32_t tempo);
extern struct umidi20_event *umidi20_event_queue_search(struct umidi20_event_queue *queue, uint32_t position, uint8_t cache_no);
extern void umidi20_event_queue_copy(struct umidi20_event_queue *src, struct umidi20_event_queue *dst, uint32_t pos_a, uint32_t pos_b, uint16_t rev_a, uint16_t rev_b, uint8_t cache_no, uint8_t flag);
extern void umidi20_event_queue_move(struct umidi20_event_queue *src, struct umidi20_event_queue *dst, uint32_t pos_a, uint32_t pos_b, uint16_t rev_a, uint16_t rev_b, uint8_t cache_no);
extern void umidi20_event_queue_insert(struct umidi20_event_queue *dst, struct umidi20_event *event_n, uint8_t cache_no);
extern void umidi20_event_queue_drain(struct umidi20_event_queue *src);
extern uint8_t umidi20_convert_to_command(struct umidi20_converter *conv, uint8_t b);
struct umidi20_event *umidi20_convert_to_event(struct umidi20_converter *conv, uint8_t b, uint8_t flag);
void	umidi20_convert_reset(struct umidi20_converter *conv);
extern const uint8_t umidi20_command_to_len[16];
extern void umidi20_gettime(struct timespec *ts);
extern uint32_t umidi20_difftime(struct timespec *a, struct timespec *b);
extern int umidi20_mutex_init(pthread_mutex_t *pmutex);
extern void umidi20_start(uint32_t start_position, uint32_t end_position, uint8_t flag);
extern void umidi20_stop(uint8_t flag);
struct umidi20_song *umidi20_song_alloc(pthread_mutex_t *p_mtx, uint16_t file_format, uint16_t resolution, uint8_t div_type);
extern void umidi20_song_free(struct umidi20_song *song);
extern struct umidi20_track *umidi20_song_track_by_unit(struct umidi20_song *song, uint16_t unit);
extern void umidi20_song_set_record_track(struct umidi20_song *song, struct umidi20_track *track);
extern void umidi20_song_start(struct umidi20_song *song, uint32_t start_offset, uint32_t end_offset, uint8_t flags);
extern void umidi20_song_stop(struct umidi20_song *song, uint8_t flags);
extern uint8_t umidi20_all_dev_off(uint8_t flag);
extern void umidi20_song_track_add(struct umidi20_song *song, struct umidi20_track *track_ref, struct umidi20_track *track_new, uint8_t is_before_ref);
extern void umidi20_song_track_remove(struct umidi20_song *song, struct umidi20_track *track);
extern void umidi20_song_recompute_position(struct umidi20_song *song);
extern void umidi20_song_recompute_tick(struct umidi20_song *song);
extern void umidi20_song_compute_max_min(struct umidi20_song *song);
extern void umidi20_config_export(struct umidi20_config *cfg);
extern void umidi20_config_import(struct umidi20_config *cfg);
extern struct umidi20_track *umidi20_track_alloc(void);
extern void umidi20_track_free(struct umidi20_track *track);
extern void umidi20_track_compute_max_min(struct umidi20_track *track);
extern void umidi20_set_timer(umidi20_timer_callback_t *fn, void *arg, uint32_t ms_interval);
extern void umidi20_update_timer(umidi20_timer_callback_t *fn, void *arg, uint32_t ms_interval, uint8_t do_sync);
extern void umidi20_unset_timer(umidi20_timer_callback_t *fn, void *arg);
extern int umidi20_pipe(int [2]);

/*--------------------------------------------------------------------------*
 * prototypes from "umidi20_file.c"
 *--------------------------------------------------------------------------*/
extern struct umidi20_song *umidi20_load_file(pthread_mutex_t *p_mtx, const uint8_t *ptr, uint32_t len);
extern uint8_t umidi20_save_file(struct umidi20_song *song, uint8_t **pptr, uint32_t *plen);

/*--------------------------------------------------------------------------*
 * prototypes from "umidi20_assert.c"
 *--------------------------------------------------------------------------*/
#define	pthread_mutex_assert(mtx, flags) do { } while (0)

/*--------------------------------------------------------------------------*
 * prototypes from "umidi20_jack.c"
 *--------------------------------------------------------------------------*/
const char **umidi20_jack_alloc_inputs(void);
const char **umidi20_jack_alloc_outputs(void);
void umidi20_jack_free_inputs(const char **);
void umidi20_jack_free_outputs(const char **);

int	umidi20_jack_rx_open(uint8_t n, const char *name);
int	umidi20_jack_tx_open(uint8_t n, const char *name);
int	umidi20_jack_rx_close(uint8_t n);
int	umidi20_jack_tx_close(uint8_t n);
int	umidi20_jack_init(const char *name);

/*--------------------------------------------------------------------------*
 * prototypes from "umidi20_coremidi.c"
 *--------------------------------------------------------------------------*/
const char **umidi20_coremidi_alloc_inputs(void);
const char **umidi20_coremidi_alloc_outputs(void);
void umidi20_coremidi_free_inputs(const char **);
void umidi20_coremidi_free_outputs(const char **);

int	umidi20_coremidi_rx_open(uint8_t n, const char *name);
int	umidi20_coremidi_tx_open(uint8_t n, const char *name);
int	umidi20_coremidi_rx_close(uint8_t n);
int	umidi20_coremidi_tx_close(uint8_t n);
int	umidi20_coremidi_init(const char *name);

/*--------------------------------------------------------------------------*
 * MIDI generator code
 *--------------------------------------------------------------------------*/
#define	C0 0
#define	D0B 1
#define	D0 2
#define	E0B 3
#define	E0 4
#define	F0 5
#define	G0B 6
#define	G0 7
#define	A0B 8
#define	A0 9
#define	H0B 10
#define	H0 11

#define	C1 12
#define	D1B 13
#define	D1 14
#define	E1B 15
#define	E1 16
#define	F1 17
#define	G1B 18
#define	G1 19
#define	A1B 20
#define	A1 21
#define	H1B 22
#define	H1 23

#define	C2 24
#define	D2B 25
#define	D2 26
#define	E2B 27
#define	E2 28
#define	F2 29
#define	G2B 30
#define	G2 31
#define	A2B 32
#define	A2 33
#define	H2B 34
#define	H2 35

#define	C3 36
#define	D3B 37
#define	D3 38
#define	E3B 39
#define	E3 40
#define	F3 41
#define	G3B 42
#define	G3 43
#define	A3B 44
#define	A3 45
#define	H3B 46
#define	H3 47

#define	C4 48
#define	D4B 49
#define	D4 50
#define	E4B 51
#define	E4 52
#define	F4 53
#define	G4B 54
#define	G4 55
#define	A4B 56
#define	A4 57
#define	H4B 58
#define	H4 59

#define	C5 60
#define	D5B 61
#define	D5 62
#define	E5B 63
#define	E5 64
#define	F5 65
#define	G5B 66
#define	G5 67
#define	A5B 68
#define	A5 69
#define	H5B 70
#define	H5 71

#define	C6 72
#define	D6B 73
#define	D6 74
#define	E6B 75
#define	E6 76
#define	F6 77
#define	G6B 78
#define	G6 79
#define	A6B 80
#define	A6 81
#define	H6B 82
#define	H6 83

#define	C7 84
#define	D7B 85
#define	D7 86
#define	E7B 87
#define	E7 88
#define	F7 89
#define	G7B 90
#define	G7 91
#define	A7B 92
#define	A7 93
#define	H7B 94
#define	H7 95

#define	C8 96
#define	D8B 97
#define	D8 98
#define	E8B 99
#define	E8 100
#define	F8 101
#define	G8B 102
#define	G8 103
#define	A8B 104
#define	A8 105
#define	H8B 106
#define	H8 107

#define	C9 108
#define	D9B 109
#define	D9 110
#define	E9B 111
#define	E9 112
#define	F9 113
#define	G9B 114
#define	G9 115
#define	A9B 116
#define	A9 117
#define	H9B 118
#define	H9 119

#define	C10 120
#define	D10B 121
#define	D10 122
#define	E10B 123
#define	E10 124
#define	F10 125
#define	G10B 126
#define	G10 127

/* Definition of channel numbers */

#define	TRACK_R 0			/* recording track */
#define	TRACK_A 1
#define	TRACK_B 2
#define	TRACK_C 3
#define	TRACK_D 4
#define	TRACK_E 5
#define	TRACK_F 6
#define	TRACK_G 7
#define	TRACK_H 8
#define	TRACK_P 9			/* percussion */

struct mid_data {
	struct umidi20_track *track;	/* track we are generating */
	uint32_t position[16];		/* track position */
	uint32_t priv[16];		/* client counters */
	uint8_t	channel;		/* currently selected MIDI channel */
	uint8_t	cc_enabled;		/* carbon copy enabled */
	uint8_t	cc_device_no;		/* carbon copy device number */
};

extern const char *mid_key_str[128];
void	mid_set_device_no(struct mid_data *d, uint8_t device_no);
void	mid_sort(uint8_t *pk, uint8_t nk);
void	mid_trans(uint8_t *pk, uint8_t nk, int8_t nt);
uint8_t	mid_add(uint8_t a, uint8_t b);
uint8_t	mid_sub(uint8_t a, uint8_t b);
uint8_t	mid_next_key(uint8_t a, int8_t n);
void	mid_dump(struct mid_data *d);
void	mid_add_raw(struct mid_data *d, const uint8_t *buf, uint32_t len, uint32_t offset);
uint32_t mid_get_position(struct mid_data *d);
void	mid_set_position(struct mid_data *d, uint32_t pos);
uint32_t mid_delay(struct mid_data *d, int32_t off);
void	mid_position_ceil(struct mid_data *d, uint16_t channel_mask);
void	mid_position_floor(struct mid_data *d, uint16_t channel_mask);
void	mid_delay_all(struct mid_data *d, int32_t off);
void	mid_key_press(struct mid_data *d, uint8_t key, uint8_t vel, uint32_t duration);
void	mid_key_press_n(struct mid_data *d, const uint8_t *pkey, uint8_t nkey, uint8_t vel, uint32_t duration);
void	mid_set_channel(struct mid_data *d, uint8_t channel);
uint8_t	mid_get_channel(struct mid_data *d);
void	mid_control(struct mid_data *d, uint8_t ctrl, uint8_t val);
void	mid_pitch_bend(struct mid_data *d, uint16_t val);
void	mid_pedal(struct mid_data *d, uint8_t on);
void	mid_s_pedal(struct mid_data *d, int32_t db, int32_t dm, int32_t da, uint8_t on);
void	mid_init(struct mid_data *d, struct umidi20_track *track);
void	mid_set_bank_program(struct mid_data *d, uint8_t channel, uint16_t bank, uint8_t prog);

__END_DECLS

#endif					/* _UMIDI20_H_ */
