/*-
 * Copyright (c) 2017 Hans Petter Selasky <hselasky@FreeBSD.org>
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
#include <jni.h>

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

struct umidi20_android {
	int	read_fd[2];
	int	write_fd[2];
	struct umidi20_parse parse;
};

struct umidi20_class_recv {
  	jclass class;
};

struct umidi20_class_main {
  	jclass class;
	jmethodID constructor;
};

struct umidi20_class {
	void *activity;
	struct umidi20_class_recv recv;
	struct umidi20_class_main main;
};

static struct umidi20_class umidi20_class;

static pthread_mutex_t umidi20_android_mtx;
static pthread_cond_t umidi20_android_cv;
static pthread_t umidi20_android_thread;
static struct umidi20_android umidi20_android[UMIDI20_N_DEVICES];
static int umidi20_android_init_done;
static int umidi20_android_register_done;
static const char *umidi20_android_name;
static int umidi20_action_current;
static int umidi20_action_busy;

enum {
	UMIDI20_CMD_SCAN_RX = 0,
	UMIDI20_CMD_SCAN_TX = 1,
	UMIDI20_CMD_SEND_MIDI = 2,
	UMIDI20_CMD_OPEN_TX = 3,
	UMIDI20_CMD_OPEN_RX = 4,
	UMIDI20_CMD_CLOSE_TX = 5,
	UMIDI20_CMD_CLOSE_RX = 6,
	UMIDI20_CMD_INITIALIZE = 7,
};

#define	UMIDI20_MTOD(env,name, ...)	\
	env[0]->name(env,## __VA_ARGS__)

#define	UMIDI20_STRING_LENGTH(env, obj)	\
	UMIDI20_MTOD(env, GetStringUTFLength, obj)

#define	UMIDI20_STRING_COPY(env, obj, start, len, buf)	\
	UMIDI20_MTOD(env, GetStringUTFRegion, obj, start, len, (char *)(buf))

#ifdef HAVE_DEBUG
#define	DPRINTF(fmt, ...) \
    printf("%s:%d: " fmt, __FUNCTION__, __LINE__,## __VA_ARGS__)
#else
#define	DPRINTF(fmt, ...) do { } while (0)
#endif

static char *
umidi20_dup_jstring(JNIEnv *env, jstring str)
{
	char *ptr;
	jsize len = UMIDI20_STRING_LENGTH(env, str) + 1;

	ptr = malloc(len);
	if (ptr == NULL)
		return (NULL);
	UMIDI20_STRING_COPY(env, str, 0, len - 1, ptr);
	ptr[len - 1] = 0;
	return (ptr);
}

static void
umidi20_android_lock(void)
{
	pthread_mutex_lock(&umidi20_android_mtx);
}

static void
umidi20_android_unlock(void)
{
	pthread_mutex_unlock(&umidi20_android_mtx);
}

static void
umidi20_android_wait(void)
{
	pthread_cond_wait(&umidi20_android_cv, &umidi20_android_mtx);
}

static void
umidi20_android_wakeup(void)
{
	pthread_cond_broadcast(&umidi20_android_cv);
}

static void
umidi20_action_locked(int a, int b)
{
	while (umidi20_action_busy != 0)
		umidi20_android_wait();

	umidi20_action_busy = 1;
	umidi20_action_current = a;
	umidi20_android_wakeup();

	while (umidi20_action_busy != 3)
		umidi20_android_wait();

	if ((a & 0xFF) == UMIDI20_CMD_SEND_MIDI) {
	  	umidi20_action_busy = 1;
		umidi20_action_current = b;
		umidi20_android_wakeup();

		while (umidi20_action_busy != 3)
			umidi20_android_wait();
	}

	umidi20_action_busy = 0;
	umidi20_android_wakeup();
}

static void
umidi20_android_onSendNative(JNIEnv *env, jobject obj, jobject msg, int offset,
    int count, int device)
{
	struct umidi20_android *puj;
	uint8_t buffer[count];
	uint8_t x;

	UMIDI20_MTOD(env, GetByteArrayRegion, msg, offset, count, buffer);
	
	umidi20_android_lock();
	puj = &umidi20_android[device];
	if (puj->write_fd[1] >= 0)
		write(puj->write_fd[1], buffer, count);
	umidi20_android_unlock();
}

static jobject
umidi20_android_getActivity(JNIEnv *env, jobject obj)
{
	return (umidi20_class.activity);
}

static jint
umidi20_android_getAction(JNIEnv *env, jobject obj)
{
	jint retval;

	umidi20_android_lock();
	if (umidi20_action_busy == 2) {
		umidi20_action_busy = 3;
		umidi20_android_wakeup();
	}
	while (umidi20_action_busy != 1)
		umidi20_android_wait();
	retval = umidi20_action_current;
	umidi20_action_busy = 2;
	umidi20_android_unlock();

	return (retval);
}

static char **umidi20_rx_dev_ptr;

static void
umidi20_android_setRxDevices(JNIEnv *env, jobject obj, int num)
{
	umidi20_android_free_inputs(umidi20_rx_dev_ptr);
	umidi20_rx_dev_ptr = calloc(num + 1, sizeof(void *));
}

static char **umidi20_tx_dev_ptr;

static void
umidi20_android_setTxDevices(JNIEnv *env, jobject obj, int num)
{
	umidi20_android_free_outputs(umidi20_tx_dev_ptr);
	umidi20_tx_dev_ptr = calloc(num + 1, sizeof(void *));
}

static void
umidi20_android_storeRxDevice(JNIEnv *env, jobject obj, int num, jstring desc)
{
	umidi20_rx_dev_ptr[num] = umidi20_dup_jstring(env, desc);
}

static void
umidi20_android_storeTxDevice(JNIEnv *env, jobject obj, int num, jstring desc)
{
	umidi20_tx_dev_ptr[num] = umidi20_dup_jstring(env, desc);
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
umidi20_convert_to_usb(struct umidi20_android *puj, uint8_t cn, uint8_t b)
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
	uint8_t data[1];
	uint8_t len;
	int n;

	while (1) {
		umidi20_android_lock();
		for (n = 0; n != UMIDI20_N_DEVICES; n++) {
			struct umidi20_android *puj = umidi20_android + n;

			while (puj->read_fd[0] > -1 &&
			       read(puj->read_fd[0], data, sizeof(data)) == sizeof(data)) {

				/* parse MIDI stream */
				if (umidi20_convert_to_usb(puj, 0, data[0]) == 0)
					continue;

				len = umidi20_cmd_to_len[puj->parse.temp_cmd[0] & 0xF];
				if (len == 0)
					continue;

				umidi20_action_locked(UMIDI20_CMD_SEND_MIDI |
						      (n << 8) | (len << 12),
						      (puj->parse.temp_cmd[1]) |
						      (puj->parse.temp_cmd[2] << 8) |
						      (puj->parse.temp_cmd[3] << 16) |
						      (puj->parse.temp_cmd[4] << 24));
			}
		}
		umidi20_android_unlock();

		usleep(1000);
	}
	return (NULL);
}

static void
umidi20_android_uniq_inputs(char **ptr)
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
umidi20_android_dup_io(const char **ppstr)
{
	unsigned long n = 0;
	char **retval;
	

	for (n = 0; ppstr[n] != NULL; n++)
		;

	retval = calloc(n + 1, sizeof(void *));
	if (retval != NULL) {
		for (n = 0; ppstr[n] != NULL; n++)
			retval[n] = strdup(ppstr[n]);
	}
	return (retval);
}

const char **
umidi20_android_alloc_outputs(void)
{
	if (umidi20_android_init_done == 0)
		return (NULL);

	umidi20_android_lock();
	umidi20_action_locked(UMIDI20_CMD_SCAN_TX, 0);
	umidi20_android_unlock();

	umidi20_android_uniq_inputs(umidi20_tx_dev_ptr);

	return (umidi20_android_dup_io(umidi20_tx_dev_ptr));
}

const char **
umidi20_android_alloc_inputs(void)
{
	if (umidi20_android_init_done == 0)
		return (NULL);

	umidi20_android_lock();
	umidi20_action_locked(UMIDI20_CMD_SCAN_RX, 0);
	umidi20_android_unlock();

	umidi20_android_uniq_inputs(umidi20_rx_dev_ptr);

	return (umidi20_android_dup_io(umidi20_rx_dev_ptr));
}

void
umidi20_android_free_outputs(const char **ports)
{
	unsigned long n;

	if (ports == NULL)
		return;

	for (n = 0; ports[n] != NULL; n++)
		free(ports[n]);

	free(ports);
}

void
umidi20_android_free_inputs(const char **ports)
{
	unsigned long n;

	if (ports == NULL)
		return;

	for (n = 0; ports[n] != NULL; n++)
		free(ports[n]);

	free(ports);
}

int
umidi20_android_rx_open(uint8_t n, const char *name)
{
	struct umidi20_android *puj;
	unsigned long x;
	int error;

	if (n >= UMIDI20_N_DEVICES || umidi20_android_init_done == 0)
		return (-1);

	puj = &umidi20_android[n];

	/* check if already opened */
	if (puj->write_fd[1] > -1 || puj->write_fd[0] > -1 || umidi20_rx_dev_ptr == NULL)
		return (-1);

	for (x = 0; umidi20_rx_dev_ptr[x] != NULL; x++) {
		if (strcmp(umidi20_rx_dev_ptr[x], name) == 0)
			break;
	}

	/* check if device not found */
	if (umidi20_rx_dev_ptr[x] == NULL)
		return (-1);

	umidi20_android_lock();
	umidi20_action_locked(UMIDI20_CMD_OPEN_RX | (n << 8) | (x << 12), 0);
	/* create looback pipe */
	error = umidi20_pipe(puj->write_fd);
	/* check for error */
	if (error != 0) {
		umidi20_action_locked(UMIDI20_CMD_CLOSE_RX | (n << 8) | (x << 12), 0);
		puj->write_fd[0] = -1;
	}
	umidi20_android_unlock();

	return (puj->write_fd[0]);
}

int
umidi20_android_tx_open(uint8_t n, const char *name)
{
	struct umidi20_android *puj;
	unsigned long x;
	int error;

	if (n >= UMIDI20_N_DEVICES || umidi20_android_init_done == 0)
		return (-1);

	puj = &umidi20_android[n];

	/* check if already opened */
	if (puj->read_fd[1] > -1 || puj->read_fd[0] > -1 || umidi20_tx_dev_ptr == NULL)
		return (-1);

	for (x = 0; umidi20_tx_dev_ptr[x] != NULL; x++) {
		if (strcmp(umidi20_tx_dev_ptr[x], name) == 0)
			break;
	}

	/* check if device not found */
	if (umidi20_tx_dev_ptr[x] == NULL)
		return (-1);

	umidi20_android_lock();
	umidi20_action_locked(UMIDI20_CMD_OPEN_TX | (n << 8) | (x << 12), 0);

	/* create looback pipe */
	error = umidi20_pipe(puj->read_fd);
	if (error == 0) {
		fcntl(puj->read_fd[0], F_SETFL, (int)O_NONBLOCK);
		memset(&puj->parse, 0, sizeof(puj->parse));
	} else {
		umidi20_action_locked(UMIDI20_CMD_CLOSE_TX | (n << 8) | (x << 12), 0);
		puj->read_fd[1] = -1;
	}
	umidi20_android_unlock();

	return (puj->read_fd[1]);
}

int
umidi20_android_rx_close(uint8_t n)
{
	struct umidi20_android *puj;

	if (n >= UMIDI20_N_DEVICES || umidi20_android_init_done == 0)
		return (-1);

	puj = &umidi20_android[n];

	umidi20_android_lock();
	close(puj->write_fd[0]);
	close(puj->write_fd[1]);
	puj->write_fd[0] = -1;
	puj->write_fd[1] = -1;
	umidi20_action_locked(UMIDI20_CMD_CLOSE_RX | (n << 8), 0);
	umidi20_android_unlock();

	return (0);
}

int
umidi20_android_tx_close(uint8_t n)
{
	struct umidi20_android *puj;

	if (n >= UMIDI20_N_DEVICES || umidi20_android_init_done == 0)
		return (-1);

	puj = &umidi20_android[n];

	umidi20_android_lock();
	close(puj->read_fd[0]);
	close(puj->read_fd[1]);
	puj->read_fd[0] = -1;
	puj->read_fd[1] = -1;
	umidi20_action_locked(UMIDI20_CMD_CLOSE_TX | (n << 8), 0);
	umidi20_android_unlock();

	return (0);
}

static jclass
umidi20_android_find_class(JNIEnv *env, const char *name)
{
	jclass class;

	class = UMIDI20_MTOD(env, FindClass, name);
	if (class == NULL) {
		DPRINTF("Class %s not found\n");
	} else {
		jclass nclass;
		nclass = UMIDI20_MTOD(env, NewGlobalRef, class);
		UMIDI20_MTOD(env, DeleteLocalRef, class);
		class = nclass;
	}
	return (class);
}

#define	UMIDI20_RESOLVE_CLASS(env, name, str)	\
	(umidi20_class.name.class = umidi20_android_find_class(env, str))

JNIEXPORT jint
JNI_OnLoad(JavaVM *jvm, void *reserved)
{
	static const JNINativeMethod recv[] = {
		{ "onSendNative", "([BIII)V", (void *)&umidi20_android_onSendNative },
	};
	static const JNINativeMethod main[] = {
		{ "getAction", "()I", (void *)&umidi20_android_getAction },
		{ "getActivity", "()Landroid/app/Activity;", (void *)&umidi20_android_getActivity },
		{ "setRxDevices", "(I)V", (void *)&umidi20_android_setRxDevices },
		{ "setTxDevices", "(I)V", (void *)&umidi20_android_setTxDevices },
		{ "storeRxDevice", "(ILjava/lang/String;)V", (void *)&umidi20_android_storeRxDevice },
		{ "storeTxDevice", "(ILjava/lang/String;)V", (void *)&umidi20_android_storeTxDevice },
	};
	JNIEnv *env;
	jobject obj;

	if (jvm[0]->GetEnv(jvm, &env, JNI_VERSION_1_6) != JNI_OK)
		return (JNI_ERR);

	pthread_mutex_init(&umidi20_android_mtx, NULL);
	pthread_cond_init(&umidi20_android_cv, NULL);

	if (UMIDI20_RESOLVE_CLASS(env, recv, "org/selasky/umidi20/UMidi20Recv") == NULL ||
	    UMIDI20_RESOLVE_CLASS(env, main, "org/selasky/umidi20/UMidi20Main") == NULL)
		return (JNI_ERR);

	if (UMIDI20_MTOD(env, RegisterNatives, umidi20_class.recv.class,
			 &recv[0], sizeof(recv) / sizeof(recv[0])))
		return (JNI_ERR);

	if (UMIDI20_MTOD(env, RegisterNatives, umidi20_class.main.class,
			 &main[0], sizeof(main) / sizeof(main[0])))
		return (JNI_ERR);

	umidi20_class.main.constructor = UMIDI20_MTOD(env, GetMethodID,
	    umidi20_class.main.class, "<init>", "()V");
	if (umidi20_class.main.constructor == NULL)
		return (JNI_ERR);

	obj = UMIDI20_MTOD(env, NewObject, umidi20_class.main.class,
	    umidi20_class.main.constructor);
	if (obj == NULL)
		return (JNI_ERR);

	UMIDI20_MTOD(env, NewGlobalRef, obj);
	UMIDI20_MTOD(env, DeleteLocalRef, obj);

	umidi20_android_register_done = 1;

	return (JNI_VERSION_1_6);
}

int
umidi20_android_init(const char *name, void *activity)
{
	struct umidi20_android *puj;
	char devname[64];
	uint8_t n;

	umidi20_android_name = strdup(name);

	if (umidi20_android_name == NULL || activity == NULL ||
	    umidi20_android_register_done == 0)
		return (-1);

	for (n = 0; n != UMIDI20_N_DEVICES; n++) {
		puj = &umidi20_android[n];
		puj->read_fd[0] = -1;
		puj->read_fd[1] = -1;
		puj->write_fd[0] = -1;
		puj->write_fd[1] = -1;
	}

	if (pthread_create(&umidi20_android_thread, NULL,
	    &umidi20_write_process, NULL))
		return (-1);

	umidi20_class.activity = activity;

	umidi20_android_lock();
	umidi20_action_locked(UMIDI20_CMD_INITIALIZE, 0);
	umidi20_android_unlock();

	umidi20_android_init_done = 1;

	return (0);
}
