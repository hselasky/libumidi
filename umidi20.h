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

#include <sys/cdefs.h>
#include <sys/param.h>

#include <pthread.h>

__BEGIN_DECLS

#define UMIDI20_BPM 60000 /* Beats Per Minute */

#define UMIDI20_COMMAND_LEN 8 /* bytes, max */
#define UMIDI20_BUF_EVENTS 1024 /* units */

#define UMIDI20_N_DEVICES 16 /* units */

#define UMIDI20_FLAG_PLAY 0x01
#define UMIDI20_FLAG_RECORD 0x02

#define UMIDI20_MAX_OFFSET 0x80000000

#define UMIDI20_BAND_SIZE 24
#define UMIDI20_KEY_TO_BAND_OFFSET(n) (((n) + 12) % UMIDI20_BAND_SIZE)
#define UMIDI20_KEY_TO_BAND_NUMBER(n) (((n) + 12) / UMIDI20_BAND_SIZE)

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

#define UMIDI20_WHAT_CHANNEL          0x0001
#define UMIDI20_WHAT_KEY              0x0002
#define UMIDI20_WHAT_VELOCITY         0x0004
#define UMIDI20_WHAT_KEY_PRESSURE     0x0008
#define UMIDI20_WHAT_CONTROL_VALUE    0x0010
#define UMIDI20_WHAT_CONTROL_ADDRESS  0x0020
#define UMIDI20_WHAT_PROGRAM_VALUE    0x0040
#define UMIDI20_WHAT_CHANNEL_PRESSURE 0x0080
#define UMIDI20_WHAT_PITCH_BEND       0x0100

struct timespec;
struct umidi20_event;
struct umidi20_track;
struct umidi20_song;

/*--------------------------------------------------------------------------*
 * queue structures and macros
 *--------------------------------------------------------------------------*/

#define UMIDI20_CACHE_INPUT  0
#define UMIDI20_CACHE_OUTPUT 1
#define UMIDI20_CACHE_EDIT   2
#define UMIDI20_CACHE_OTHER  3
#define UMIDI20_CACHE_MAX    4

struct umidi20_event_queue {
	struct umidi20_event *ifq_head;
	struct umidi20_event *ifq_tail;
	struct umidi20_event *ifq_cache[UMIDI20_CACHE_MAX];

	int32_t ifq_len;
	int32_t ifq_maxlen;
};

struct umidi20_track_queue {
	struct umidi20_track *ifq_head;
	struct umidi20_track *ifq_tail;
	struct umidi20_track *ifq_cache[UMIDI20_CACHE_MAX];

	int32_t ifq_len;
	int32_t ifq_maxlen;
};

struct umidi20_song_queue {
	struct umidi20_song *ifq_head;
	struct umidi20_song *ifq_tail;
	struct umidi20_song *ifq_cache[UMIDI20_CACHE_MAX];

	int32_t ifq_len;
	int32_t ifq_maxlen;
};

#define UMIDI20_IF_CHECK_CACHE_ONE(ifq,m,n)		\
    if ((ifq)->ifq_cache[n] == (m)) {			\
        (ifq)->ifq_cache[n] = (m)->p_nextpkt;		\
	if ((ifq)->ifq_cache[n] == NULL) {		\
	    (ifq)->ifq_cache[n] = (m)->p_prevpkt;	\
	}						\
    }

#define UMIDI20_IF_CHECK_CACHE(ifq, m) do {	\
    UMIDI20_IF_CHECK_CACHE_ONE(ifq,m,0);	\
    UMIDI20_IF_CHECK_CACHE_ONE(ifq,m,1);	\
    UMIDI20_IF_CHECK_CACHE_ONE(ifq,m,2);	\
    UMIDI20_IF_CHECK_CACHE_ONE(ifq,m,3);	\
} while (0)

#define UMIDI20_IF_REMOVE(ifq, m) do {			\
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

#define UMIDI20_IF_DEQUEUE(ifq, m) do {                         \
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

#define UMIDI20_IF_ENQUEUE_AFTER(ifq, m1, m2) do {	\
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

#define UMIDI20_IF_ENQUEUE_BEFORE(ifq, m1, m2) do {	\
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

#define UMIDI20_IF_ENQUEUE_LAST(ifq, m) do {	\
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

#define UMIDI20_IF_ENQUEUE_FIRST(ifq, m) do {	\
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

#define UMIDI20_IF_QFULL(ifq)   ((ifq)->ifq_len >= (ifq)->ifq_maxlen)
#define UMIDI20_IF_QLEN(ifq)    ((ifq)->ifq_len)
#define UMIDI20_IF_POLL_HEAD(ifq, m) ((m) = (ifq)->ifq_head)
#define UMIDI20_IF_POLL_TAIL(ifq, m) ((m) = (ifq)->ifq_tail)

#define UMIDI20_QUEUE_FOREACH(m, ifq)		\
  for ((m) = (ifq)->ifq_head;			\
       (m);					\
       (m) = (m)->p_nextpkt)

#define UMIDI20_QUEUE_FOREACH_SAFE(m, ifq, m_next)	\
  for ((m) = (ifq)->ifq_head;				\
       (m) && ((m_next) = (m)->p_nextpkt, 1);		\
       (m) = (m_next))


/*--------------------------------------------------------------------------*
 * MIDI command converter
 *--------------------------------------------------------------------------*/
struct umidi20_converter {
    struct umidi20_event **pp_next;
    struct umidi20_event *p_next;

    u_int8_t * temp_cmd;
    u_int8_t temp_0[UMIDI20_COMMAND_LEN];
    u_int8_t temp_1[UMIDI20_COMMAND_LEN];
    u_int8_t state;
#define UMIDI20_ST_UNKNOWN   0 /* scan for command */
#define UMIDI20_ST_1PARAM    1
#define UMIDI20_ST_2PARAM_1  2
#define UMIDI20_ST_2PARAM_2  3
#define UMIDI20_ST_SYSEX_0   4
#define UMIDI20_ST_SYSEX_1   5
#define UMIDI20_ST_SYSEX_2   6
#define UMIDI20_ST_SYSEX_3   7
#define UMIDI20_ST_SYSEX_4   8
#define UMIDI20_ST_SYSEX_5   9
#define UMIDI20_ST_SYSEX_6  10
};

/*--------------------------------------------------------------------------*
 * MIDI event structure
 *--------------------------------------------------------------------------*/
struct umidi20_event {
    struct umidi20_event *p_nextpkt;
    struct umidi20_event *p_prevpkt;
    struct umidi20_event *p_next;
    u_int32_t position; /* milliseconds */
    u_int32_t tick; /* units */
    u_int32_t duration; /* milliseconds */
    u_int16_t revision; /* unit */
    u_int8_t device_no; /* device number */
    u_int8_t unused;
    u_int8_t cmd[UMIDI20_COMMAND_LEN];
};

/*--------------------------------------------------------------------------*
 * MIDI config structures
 *--------------------------------------------------------------------------*/
struct umidi20_config_dev {
	char rec_fname[128];
	char play_fname[128];
	u_int8_t rec_enabled_cfg;
	u_int8_t play_enabled_cfg;
};

struct umidi20_config {
	struct umidi20_config_dev cfg_dev[UMIDI20_N_DEVICES];
	u_int32_t effects;
#define UMIDI20_EFFECT_LOOPBACK   0x0001
#define UMIDI20_EFFECT_KEYCOMPL_1 0x0002
#define UMIDI20_EFFECT_KEYCOMPL_2 0x0004
#define UMIDI20_EFFECT_KEYCOMPL_3 0x0008
};

/*--------------------------------------------------------------------------*
 * MIDI device structure
 *--------------------------------------------------------------------------*/
struct umidi20_device {

    struct umidi20_event_queue queue;
    struct umidi20_converter conv;

    u_int32_t start_position;
    u_int32_t end_offset;

    int32_t file_no; /* I/O device */

    u_int8_t device_no; /* device number */

    u_int8_t enabled_usr; /* enabled by user */
    u_int8_t enabled_cfg; /* enabled by config */
    u_int8_t update;
    u_int8_t fname[128];

    u_int8_t key_on_table[((128*16)+7) / 8];
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

    pthread_t thread_alloc;
    pthread_t thread_play_rec;
    pthread_t thread_files;

    u_int32_t effects;
    u_int32_t curr_position;
};

extern struct umidi20_root_device root_dev;

/*--------------------------------------------------------------------------*
 * MIDI track structure
 *--------------------------------------------------------------------------*/
struct umidi20_track {
    struct umidi20_event_queue queue;

    struct umidi20_track *p_nextpkt;
    struct umidi20_track *p_prevpkt;

    u_int32_t position_max;

    u_int8_t mute_flag;
    u_int8_t selected_flag;
    u_int8_t draw_flag;
    u_int8_t temp_flag;

    u_int8_t key_min;
    u_int8_t key_max;

    u_int8_t band_min;
    u_int8_t band_max;

    u_int8_t name[256];
    u_int8_t instrument[256];
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

    pthread_mutex_t * p_mtx;
    pthread_t thread_io;

    u_int32_t play_start_position;
    u_int32_t play_end_offset;
    u_int32_t play_start_offset;
    u_int32_t play_last_offset;
    u_int32_t position_max;
    u_int32_t track_max;
    u_int32_t band_max;

    u_int16_t midi_file_format;
    u_int16_t midi_resolution;
    u_int16_t record_track;

    u_int8_t midi_division_type;

    u_int8_t play_enabled;
    u_int8_t rec_enabled;

    u_int8_t pc_flags; /* play and record flags */

    char filename[MAXPATHLEN];
};

/*--------------------------------------------------------------------------*
 * prototypes from "umidi20.c"
 *--------------------------------------------------------------------------*/

extern void
umidi20_init(void);

extern struct umidi20_event *
umidi20_event_alloc(struct umidi20_event ***ppp_next, u_int8_t flag);

extern void
umidi20_event_free(struct umidi20_event *event);

extern struct umidi20_event *
umidi20_event_copy(struct umidi20_event *event, u_int8_t flag);

extern struct umidi20_event *
umidi20_event_from_data(const u_int8_t *data_ptr, 
			u_int32_t data_len, u_int8_t flag);
extern u_int32_t
umidi20_event_get_what(struct umidi20_event *event);

extern u_int8_t
umidi20_event_is_key_start(struct umidi20_event *event);

extern u_int8_t
umidi20_event_is_key_end(struct umidi20_event *event);

extern u_int8_t
umidi20_event_is_tempo(struct umidi20_event *event);

extern u_int8_t
umidi20_event_is_voice(struct umidi20_event *event);

extern u_int8_t
umidi20_event_is_sysex(struct umidi20_event *event);

extern u_int8_t
umidi20_event_get_channel(struct umidi20_event *event);

extern void
umidi20_event_set_channel(struct umidi20_event *event, u_int8_t c);

extern u_int8_t
umidi20_event_get_key(struct umidi20_event *event);

extern void
umidi20_event_set_key(struct umidi20_event *event, u_int8_t k);

extern u_int8_t
umidi20_event_get_velocity(struct umidi20_event *event);

extern void
umidi20_event_set_velocity(struct umidi20_event *event, u_int8_t v);

extern u_int8_t
umidi20_event_get_pressure(struct umidi20_event *event);

extern void
umidi20_event_set_pressure(struct umidi20_event *event, u_int8_t p);

extern u_int8_t
umidi20_event_get_control_address(struct umidi20_event *event);

extern void
umidi20_event_set_control_address(struct umidi20_event *event, u_int8_t a);

extern u_int8_t
umidi20_event_get_control_value(struct umidi20_event *event);

extern void
umidi20_event_set_control_value(struct umidi20_event *event, u_int8_t a);

extern u_int8_t
umidi20_event_get_program_number(struct umidi20_event *event);

extern void
umidi20_event_set_program_number(struct umidi20_event *event, u_int8_t n);

extern u_int16_t
umidi20_event_get_pitch_value(struct umidi20_event *event);

extern void
umidi20_event_set_pitch_value(struct umidi20_event *event, u_int16_t n);

extern u_int32_t
umidi20_event_get_length_first(struct umidi20_event *event);

extern u_int32_t
umidi20_event_get_length(struct umidi20_event *event);

extern void
umidi20_event_copy_out(struct umidi20_event *event, u_int8_t *dst,
		       u_int32_t offset, u_int32_t len);
extern u_int8_t
umidi20_event_get_meta_number(struct umidi20_event *event);

extern void
umidi20_event_set_meta_number(struct umidi20_event *event, u_int8_t n);

extern u_int32_t
umidi20_event_get_tempo(struct umidi20_event *event);

extern void
umidi20_event_set_tempo(struct umidi20_event *event, u_int32_t tempo);

extern struct umidi20_event *
umidi20_event_queue_search(struct umidi20_event_queue *queue, 
			   u_int32_t position, u_int8_t cache_no);
extern void
umidi20_event_queue_copy(struct umidi20_event_queue *src, 
			 struct umidi20_event_queue *dst,
			 u_int32_t pos_a, u_int32_t pos_b,
			 u_int16_t rev_a, u_int16_t rev_b,
			 u_int8_t cache_no, u_int8_t flag);
extern void
umidi20_event_queue_move(struct umidi20_event_queue *src, 
			 struct umidi20_event_queue *dst,
			 u_int32_t pos_a, u_int32_t pos_b,  
			 u_int16_t rev_a, u_int16_t rev_b,
			 u_int8_t cache_no);
extern void
umidi20_event_queue_insert(struct umidi20_event_queue *dst, 
			   struct umidi20_event *event_n, 
			   u_int8_t cache_no);
extern void
umidi20_event_queue_drain(struct umidi20_event_queue *src);

extern u_int8_t
umidi20_convert_to_command(struct umidi20_converter *conv, u_int8_t b);

struct umidi20_event *
umidi20_convert_to_event(struct umidi20_converter *conv, 
			 u_int8_t b, u_int8_t flag);
void
umidi20_convert_reset(struct umidi20_converter *conv);

extern const
u_int8_t umidi20_command_to_len[16];

extern void
umidi20_gettime(struct timespec *ts);

extern u_int32_t
umidi20_difftime(struct timespec *a, struct timespec *b);

extern int
umidi20_mutex_init(pthread_mutex_t *pmutex);

extern void
umidi20_start(u_int32_t start_position, 
	      u_int32_t end_position, u_int8_t flag);

extern void
umidi20_stop(u_int8_t flag);

struct umidi20_song *
umidi20_song_alloc(pthread_mutex_t *p_mtx, u_int16_t file_format, u_int16_t resolution, 
		   u_int8_t div_type);
extern void
umidi20_song_free(struct umidi20_song *song);

extern struct umidi20_track *
umidi20_song_track_by_unit(struct umidi20_song *song, u_int16_t unit);

extern void
umidi20_song_set_record_track(struct umidi20_song *song, struct umidi20_track *track);

extern void
umidi20_song_start(struct umidi20_song *song, u_int32_t start_offset, 
		   u_int32_t end_offset, u_int8_t flags);
extern void
umidi20_song_stop(struct umidi20_song *song, u_int8_t flags);

extern u_int8_t
umidi20_all_dev_off(u_int8_t flag);

extern void
umidi20_song_track_add(struct umidi20_song *song, 
		       struct umidi20_track *track_ref,
		       struct umidi20_track *track_new,
		       u_int8_t is_before_ref);
extern void
umidi20_song_track_remove(struct umidi20_song *song, 
			  struct umidi20_track *track);
extern void
umidi20_song_recompute_position(struct umidi20_song *song);

extern void
umidi20_song_recompute_tick(struct umidi20_song *song);

extern void
umidi20_song_compute_max_min(struct umidi20_song *song);

extern void
umidi20_config_export(struct umidi20_config *cfg);

extern void
umidi20_config_import(struct umidi20_config *cfg);

extern struct umidi20_track *
umidi20_track_alloc(void);

extern void
umidi20_track_free(struct umidi20_track *track);

extern void
umidi20_track_compute_max_min(struct umidi20_track *track);

/*--------------------------------------------------------------------------*
 * prototypes from "umidi20_file.c"
 *--------------------------------------------------------------------------*/
extern struct umidi20_song *
umidi20_load_file(pthread_mutex_t *p_mtx, const char *filename);

extern u_int8_t
umidi20_save_file(struct umidi20_song *song, const char* filename);

/*--------------------------------------------------------------------------*
 * prototypes from "umidi20_assert.c"
 *--------------------------------------------------------------------------*/
extern void
__pthread_mutex_assert(pthread_mutex_t *p_mtx, u_int32_t flags, 
		       const char *file, const char *func, u_int32_t line);
#define MA_NOTOWNED 1
#define MA_OWNED 2
#define pthread_mutex_assert(mtx, flags) \
    __pthread_mutex_assert(mtx, flags, __FILE__, __FUNCTION__, __LINE__);

#define DEBUG(...) 

__END_DECLS
