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
	jobject output_device;
	jobject output_port;
	jobject input_device;
  	jobject input_port;
	struct umidi20_parse parse;
};

struct umidi20_class_local {
	jclass class;
	jmethodID openCallback;
	jmethodID receiveMidi;
};

struct umidi20_class_context {
	jclass class;
	jmethodID getSystemService;
};

struct umidi20_class_MidiDevice {
	jclass class;
	jmethodID close;
	jmethodID connectPorts;
	jmethodID getInfo;
	jmethodID openInputPort;
	jmethodID openOutputPort;
	jmethodID toString;
};

struct umidi20_class_MidiDevice_MidiConnection {
	jclass class;
	jmethodID close;
};

struct umidi20_class_MidiDeviceInfo {
	jclass class;
	jmethodID describeContents;
	jmethodID equals;
	jmethodID getId;
	jmethodID getInputPortCount;
	jmethodID getOutputPortCount;
	jmethodID getPorts;
  	jmethodID getProperties;
  	jmethodID getType;
  	jmethodID hashCode;
	jmethodID isPrivate;
	jmethodID toString;
	jmethodID writeToParcel;
};

struct umidi20_class_MidiDeviceInfo_PortInfo {
	jclass class;
	jmethodID getName;
  	jmethodID getPortNumber;
  	jmethodID getType;
};

struct umidi20_class_MidiDeviceService {
	jclass class;
	jmethodID getDeviceInfo;
	jmethodID getOutputPortReceivers;
	jmethodID onBind;
	jmethodID onClose;
	jmethodID onCreate;
	jmethodID onDeviceStatusChanged;
	jmethodID onGetInputPortReceivers;
};

struct umidi20_class_MidiDeviceStatus {
  	jclass class;
	jmethodID describeContents;
	jmethodID getDeviceInfo;
	jmethodID getOutputPortOpenCount;
	jmethodID isInputPortOpen;
	jmethodID toString;
	jmethodID writeToParcel;
};

struct umidi20_class_MidiInputPort {
	jclass class;
	jmethodID close;
	jmethodID getPortNumber;
	jmethodID onFlush;
	jmethodID onSend;
};

struct umidi20_class_MidiManager {
	jclass class;
	jmethodID getDevices;
	jmethodID openBluetoothDevice;
	jmethodID openDevice;
	jmethodID registerDeviceCallback;
	jmethodID unregisterDeviceCallback;
};

struct umidi20_class_MidiManager_DeviceCallback {
	jclass class;
	jmethodID onDeviceAdded;
	jmethodID onDeviceRemoved;
	jmethodID onDeviceStatusChanged;
};

struct umidi20_class_MidiOutputPort {
	jclass class;
	jmethodID close;
	jmethodID getPortNumber;
	jmethodID onConnect;
	jmethodID onDisconnect;
};

struct umidi20_class_MidiReceiver {
	jclass class;
	jmethodID flush;
	jmethodID getMaxMessageSize;
	jmethodID onFlush;
	jmethodID onSend;
  	jmethodID send;
	jmethodID sendTs;
};

struct umidi20_class_MidiSender {
  	jclass class;
	jmethodID connect;
	jmethodID disconnect;
	jmethodID onConnect;
	jmethodID onDisconnect;
};

struct umidi20_class {
	JavaVM jvm;
	JNIEnv env;
	struct umidi20_class_local local;
	struct umidi20_class_context context;
	struct umidi20_class_MidiDevice MidiDevice;
	struct umidi20_class_MidiDevice_MidiConnection MidiDevice_MidiConnection;
	struct umidi20_class_MidiDeviceInfo MidiDeviceInfo;
	struct umidi20_class_MidiDeviceInfo_PortInfo MidiDeviceInfo_PortInfo;
	struct umidi20_class_MidiDeviceService MidiDeviceService;
	struct umidi20_class_MidiDeviceStatus MidiDeviceStatus;
	struct umidi20_class_MidiInputPort MidiInputPort;
	struct umidi20_class_MidiManager MidiManager;
	struct umidi20_class_MidiManager_DeviceCallback MidiManager_DeviceCallback;
	struct umidi20_class_MidiOutputPort MidiOutputPort;
	struct umidi20_class_MidiReceiver MidiReceiver;
	struct umidi20_class_MidiSender MidiSender;
};

static struct umidi20_class umidi20_class;

static jobject umidi20_MidiManager;

static pthread_mutex_t umidi20_android_mtx;
static pthread_cond_t umidi20_android_cv;
static pthread_t umidi20_android_thread;
static struct umidi20_android umidi20_android[UMIDI20_N_DEVICES];
static int umidi20_android_init_done;
static int umidi20_android_init_error;
static const char *umidi20_android_name;

#define	UMIDI20_MAX_PORTS 16

#define	UMIDI20_MTOD(name, ...)	\
	umidi20_class.env->name(&umidi20_class.env,## __VA_ARGS__)

#define UMIDI20_CALL(ret,func,...) \
	UMIDI20_MTOD(ret, umidi20_class.func,## __VA_ARGS__)

#define	UMIDI20_ARRAY_LENGTH(obj) \
	UMIDI20_MTOD(GetArrayLength, obj)

#define	UMIDI20_ARRAY_INDEX(obj, i) \
	UMIDI20_MTOD(GetObjectArrayElement, obj, i)

#define	UMIDI20_DELETE(obj) \
	UMIDI20_MTOD(DeleteLocalRef, obj);

#define	UMIDI20_STRING_LENGTH(obj) \
	UMIDI20_MTOD(GetStringUTFLength, obj)

#define	UMIDI20_STRING_COPY(obj, start, len, buf) \
	UMIDI20_MTOD(GetStringUTFRegion, obj, start, len, (char *)(buf))

#ifdef HAVE_DEBUG
#define	DPRINTF(fmt, ...) \
    printf("%s:%d: " fmt, __FUNCTION__, __LINE__,## __VA_ARGS__)
#else
#define	DPRINTF(fmt, ...) do { } while (0)
#endif

static char *
umidi20_dup_jstring(jstring str)
{
	char *ptr;
	jsize len = UMIDI20_STRING_LENGTH(str) + 1;

	ptr = malloc(len);
	if (ptr == NULL)
		return (NULL);
	UMIDI20_STRING_COPY(str, 0, len - 1, ptr);
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
umidi20_android_on_send_callback(JNIEnv *env, jobject obj, jobject msg, int offset,
    int count, long timestamp)
{
	struct umidi20_android *puj;
	uint8_t buffer[count];
	uint8_t x;

	UMIDI20_MTOD(GetByteArrayRegion, msg, offset, count, buffer);
	
	umidi20_android_lock();
	for (x = 0; x != UMIDI20_N_DEVICES; x++) {
		puj = &umidi20_android[x];

		if (puj->write_fd[1] < 0)
			continue;
		if (puj->input_port != obj)
			continue;
		write(puj->write_fd[1], buffer, count);
	}
	umidi20_android_unlock();
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

			if (puj->read_fd[0] > -1) {
				while (read(puj->read_fd[0], data, sizeof(data)) == sizeof(data)) {
					if (umidi20_convert_to_usb(puj, 0, data[0])) {
						jbyteArray pkt;

						len = umidi20_cmd_to_len[puj->parse.temp_cmd[0] & 0xF];
						if (len == 0)
							continue;

						pkt = UMIDI20_MTOD(NewByteArray, len);
						if (pkt == NULL)
							continue;

						UMIDI20_MTOD(SetByteArrayRegion, pkt, 0, len, &puj->parse.temp_cmd[1]);

						UMIDI20_CALL(CallVoidMethod, MidiReceiver.send, puj->output_port,
						    pkt, 0, len);

						UMIDI20_DELETE(pkt);
					}
				}
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

static const char **
umidi20_android_alloc_devices(int is_output)
{
	jobject MidiDeviceInfoArray;
	jobject MidiDeviceInfo;
	unsigned long n;
	unsigned long x;
	unsigned long z;
	char **ptr;

	if (umidi20_android_init_done == 0)
		return (0);

	MidiDeviceInfoArray =
	    UMIDI20_CALL(CallObjectMethod, MidiManager.getDevices, umidi20_MidiManager);

	if (MidiDeviceInfoArray == NULL)
		return (0);

	n = UMIDI20_ARRAY_LENGTH(MidiDeviceInfoArray);

	ptr = malloc(sizeof(void *) * (UMIDI20_MAX_PORTS * n + 1));
	if (ptr == NULL) {
	  	UMIDI20_DELETE(MidiDeviceInfoArray);
		return (NULL);
	}

	for (z = x = 0; x != n; x++) {
		jstring name;
		int ports;
		int y;

		MidiDeviceInfo = UMIDI20_ARRAY_INDEX(MidiDeviceInfoArray, x);

		if (is_output)
			ports = UMIDI20_CALL(CallIntMethod, MidiDeviceInfo.getInputPortCount, MidiDeviceInfo);
		else
			ports = UMIDI20_CALL(CallIntMethod, MidiDeviceInfo.getOutputPortCount, MidiDeviceInfo);

		if (ports > UMIDI20_MAX_PORTS)
			ports = UMIDI20_MAX_PORTS;

		name = UMIDI20_CALL(CallIntMethod, MidiDeviceInfo.toString, MidiDeviceInfo);
		for (y = 0; y < ports; y++)
			ptr[z++] = umidi20_dup_jstring(name);
		UMIDI20_DELETE(name);
	}
	UMIDI20_DELETE(MidiDeviceInfoArray);

	ptr[z] = NULL;

	umidi20_android_uniq_inputs(ptr);

	return ((const char **)ptr);
}

const char **
umidi20_android_alloc_outputs(void)
{
	return (umidi20_android_alloc_devices(1));
}

const char **
umidi20_android_alloc_inputs(void)
{
  	return (umidi20_android_alloc_devices(0));
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

static int
umidi20_android_compare_dev_string(jstring str, const char *name, int *pidx)
{
	char *ptr;
	char *tmp;
	char *cpy;
	int which;

	ptr = umidi20_dup_jstring(str);
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

static jobject umidi20_device;

static void
umidi20_android_open_device_callback(JNIEnv *env, jobject obj, jobject device)
{
	umidi20_android_lock();
	umidi20_device = device;
	umidi20_android_wakeup();
	umidi20_android_unlock();
}

static int
umidi20_android_open_device(int is_tx, const char *devname, jobject *pdev, jobject *pport)
{
  	struct umidi20_android *puj;
	jstring name = NULL;
	jobject MidiDeviceInfoArray;
	jobject MidiDeviceInfo;
	unsigned long n;
	unsigned long x;
	int index = 0;
	int retval = 0;

	*pdev = NULL;
	*pport = NULL;

	if (umidi20_android_init_done == 0)
		return (retval);

	MidiDeviceInfoArray =
	    UMIDI20_CALL(CallObjectMethod, MidiManager.getDevices, umidi20_MidiManager);

	if (MidiDeviceInfoArray == NULL)
		return (retval);

	n = UMIDI20_ARRAY_LENGTH(MidiDeviceInfoArray);

	for (x = 0; x != n; x++) {
		int ports;
		int y;

		MidiDeviceInfo = UMIDI20_ARRAY_INDEX(MidiDeviceInfoArray, x);

		if (is_tx)
			ports = UMIDI20_CALL(CallIntMethod, MidiDeviceInfo.getInputPortCount, MidiDeviceInfo);
		else
			ports = UMIDI20_CALL(CallIntMethod, MidiDeviceInfo.getOutputPortCount, MidiDeviceInfo);

		if (ports > UMIDI20_MAX_PORTS)
			ports = UMIDI20_MAX_PORTS;

		name = UMIDI20_CALL(CallIntMethod, MidiDeviceInfo.toString, MidiDeviceInfo);
		for (y = 0; y < ports; y++) {
			if (umidi20_android_compare_dev_string(name, devname, &index)) {
				umidi20_android_lock();
				umidi20_device = (jobject)-1UL;
				UMIDI20_CALL(CallVoidMethod, MidiManager.openDevice, umidi20_MidiManager,
				    MidiDeviceInfo, umidi20_class.local.openCallback, NULL);
				while (umidi20_device == (jobject)-1UL)
					umidi20_android_wait();
				umidi20_android_unlock();

				if (umidi20_device == NULL)
					goto done;

				*pdev = umidi20_device;
				if (is_tx)
					*pport = UMIDI20_CALL(CallObjectMethod, MidiDevice.openInputPort, *pdev, y);
				else
					*pport = UMIDI20_CALL(CallObjectMethod, MidiDevice.openOutputPort, *pdev, y);
				if (*pport == NULL) {
					UMIDI20_CALL(CallVoidMethod, MidiDevice.close, *pdev);
					UMIDI20_DELETE(*pdev);
					*pdev = NULL;
					goto done;
				}
				retval = 1;
				goto done;
			}
		}
		UMIDI20_DELETE(name);
	}
	UMIDI20_DELETE(MidiDeviceInfoArray);
	return (retval);
done:
	UMIDI20_DELETE(name);
	UMIDI20_DELETE(MidiDeviceInfoArray);
	return (retval);
}

static void
umidi20_android_close_device(int is_tx, jobject pdev, jobject pport)
{
	if (is_tx) {
		UMIDI20_CALL(CallVoidMethod, MidiInputPort.close, pport);
		UMIDI20_CALL(CallVoidMethod, MidiDevice.close, pdev);
	} else {
		UMIDI20_CALL(CallVoidMethod, MidiOutputPort.close, pport);
		UMIDI20_CALL(CallVoidMethod, MidiDevice.close, pdev);
	}
	UMIDI20_DELETE(pport);
	UMIDI20_DELETE(pdev);
}

int
umidi20_android_rx_open(uint8_t n, const char *name)
{
	struct umidi20_android *puj;
	int error;

	if (n >= UMIDI20_N_DEVICES || umidi20_android_init_done == 0)
		return (-1);

	puj = &umidi20_android[n];

	/* check if already opened */
	if (puj->write_fd[1] > -1 || puj->write_fd[0] > -1)
		return (-1);

	if (umidi20_android_open_device(0, name, &puj->input_device, &puj->input_port) == 0)
		return (-1);

	/* create looback pipe */
	umidi20_android_lock();
	error = umidi20_pipe(puj->write_fd);
	umidi20_android_unlock();

	if (error) {
		umidi20_android_close_device(0, puj->input_device, puj->input_port);
		return (-1);
	}
	return (puj->write_fd[0]);
}

int
umidi20_android_tx_open(uint8_t n, const char *name)
{
	struct umidi20_android *puj;
	int error;

	if (n >= UMIDI20_N_DEVICES || umidi20_android_init_done == 0)
		return (-1);

	puj = &umidi20_android[n];

	/* check if already opened */
	if (puj->read_fd[1] > -1 || puj->read_fd[0] > -1)
		return (-1);

	if (umidi20_android_open_device(1, name, &puj->output_device, &puj->output_port) == 0)
		return (-1);

	/* create looback pipe */
	umidi20_android_lock();
	error = umidi20_pipe(puj->read_fd);
	if (error == 0) {
		fcntl(puj->read_fd[0], F_SETFL, (int)O_NONBLOCK);
		memset(&puj->parse, 0, sizeof(puj->parse));
	}
	umidi20_android_unlock();

	if (error) {
		umidi20_android_close_device(1, puj->output_device, puj->output_port);
		return (-1);
	}
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
	umidi20_android_unlock();

	umidi20_android_close_device(0, puj->input_device, puj->input_port);

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
	umidi20_android_unlock();

	umidi20_android_close_device(1, puj->output_device, puj->output_port);

	return (0);
}

static jclass
umidi20_android_find_class(const char *name)
{
	jclass class;

	class = UMIDI20_MTOD(FindClass, name);
	if (class == NULL) {
		DPRINTF("Class %s not found\n");
		umidi20_android_init_error = 1;
	}
	return (class);
}

#define	UMIDI20_RESOLVE_CLASS(name, str) \
	umidi20_class.name.class = umidi20_android_find_class(str)

static void
umidi20_android_find_func(jclass class, jmethodID *out, const char *name, const char *args)
{
	jmethodID mtod;

	mtod = UMIDI20_MTOD(GetStaticMethodID, class, name, args);
	if (mtod == NULL) {
		DPRINTF("Method %s not found\n", name);
		umidi20_android_init_error = 1;
	}
	*out = mtod;
}

#define	UMIDI20_RESOLVE_FUNC(field,func,name,type) \
	umidi20_android_find_func(umidi20_class.field.class, &umidi20_class.field.func, name, type)

static const JNINativeMethod umidi20_android_method_table[] = {
	{ "onDeviceOpened", "(Ljava/lang/Object;)V", (void *)umidi20_android_open_device_callback },
	{ "onSend", "([BIIJ)V", (void *)umidi20_android_on_send_callback },
};

int
umidi20_android_init(const char *name, void *parent_jvm, void *parent_env)
{
	struct umidi20_android *puj;
	char devname[64];
	uint8_t n;

	umidi20_android_name = strdup(name);
	if (umidi20_android_name == NULL)
		return (-1);

	pthread_mutex_init(&umidi20_android_mtx, NULL);
	pthread_cond_init(&umidi20_android_cv, NULL);

	umidi20_class.env = parent_env;
	umidi20_class.jvm = parent_jvm;
	umidi20_class.local.class =
	    UMIDI20_MTOD(DefineClass, "/com/android/media/midi/Local", NULL, NULL, 0);
	if (umidi20_class.local.class == NULL)
		return (-1);

	UMIDI20_RESOLVE_CLASS(context, "/com/android/content/Context");
	UMIDI20_RESOLVE_CLASS(MidiDevice, "/com/android/media/midi/MidiDevice");
	UMIDI20_RESOLVE_CLASS(MidiDevice_MidiConnection, "/com/android/media/midi/MidiDevice.MidiConnection");
	UMIDI20_RESOLVE_CLASS(MidiDeviceInfo, "/com/android/media/midi/MidiDeviceInfo");
	UMIDI20_RESOLVE_CLASS(MidiDeviceInfo_PortInfo, "/com/android/media/midi/MidiDeviceInfo.PortInfo");
	UMIDI20_RESOLVE_CLASS(MidiDeviceService, "/com/android/media/midi/MidiDeviceService");
	UMIDI20_RESOLVE_CLASS(MidiDeviceStatus, "/com/android/media/midi/MidiDeviceStatus");
	UMIDI20_RESOLVE_CLASS(MidiManager, "/com/android/media/midi/MidiManager");
	UMIDI20_RESOLVE_CLASS(MidiManager_DeviceCallback, "/com/android/media/midi/MidiManager.DeviceCallback");
	UMIDI20_RESOLVE_CLASS(MidiOutputPort, "/com/android/media/midi/MidiOutputPort");
	UMIDI20_RESOLVE_CLASS(MidiReceiver, "/com/android/media/midi/MidiReceiver");
	UMIDI20_RESOLVE_CLASS(MidiSender, "/com/android/media/midi/MidiSender");

	if (umidi20_android_init_error != 0)
		return (-1);
	if (UMIDI20_MTOD(RegisterNatives, umidi20_class.local.class, &umidi20_android_method_table[0], 1))
		return (-1);

	UMIDI20_RESOLVE_FUNC(local, openCallback, "openCallback", "(Ljava/lang/Object;)V");

	UMIDI20_RESOLVE_FUNC(context, getSystemService, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");

	UMIDI20_RESOLVE_FUNC(MidiDevice, close, "close", "()V");
	UMIDI20_RESOLVE_FUNC(MidiDevice, connectPorts, "connectPorts", "(Ljava/lang/Object;I)Ljava/lang/Object;");
	UMIDI20_RESOLVE_FUNC(MidiDevice, getInfo, "getInfo", "()Ljava/lang/Object;");
	UMIDI20_RESOLVE_FUNC(MidiDevice, openInputPort, "openInputPort", "(I)Ljava/lang/Object;");
	UMIDI20_RESOLVE_FUNC(MidiDevice, openOutputPort, "openOutputPort", "(I)Ljava/lang/Object;");
	UMIDI20_RESOLVE_FUNC(MidiDevice, toString, "toString", "()Ljava/lang/Object;");

	UMIDI20_RESOLVE_FUNC(MidiDevice_MidiConnection, close, "close", "()V");

	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo, describeContents, "describeContents", "()I");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo, equals, "equals", "(Ljava/lang/Object;)Z");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo, getId, "getId", "()I");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo, getInputPortCount, "getInputPortCount", "()I");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo, getOutputPortCount, "getOutputPortCount", "()I");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo, getPorts, "getPorts", "()[Ljava/lang/Object;");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo, getProperties, "getProperties", "()[Ljava/lang/Object;");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo, getType, "getType", "()I");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo, hashCode, "hashCode", "()I");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo, isPrivate, "isPrivate", "()Z");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo, toString, "toString", "()Ljava/lang/String;");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo, writeToParcel, "writeToParcel", "(Ljava/lang/Object;I)V");

	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo_PortInfo, getName, "getName", "()Ljava/lang/String;");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo_PortInfo, getPortNumber, "getPortNumber", "()I");
	UMIDI20_RESOLVE_FUNC(MidiDeviceInfo_PortInfo, getType, "getType", "()I");

	UMIDI20_RESOLVE_FUNC(MidiDeviceStatus, describeContents, "describeContents", "()I");
	UMIDI20_RESOLVE_FUNC(MidiDeviceStatus, getDeviceInfo, "getDeviceInfo", "()Ljava/lang/Object;");
	UMIDI20_RESOLVE_FUNC(MidiDeviceStatus, getOutputPortOpenCount, "getOutputPortOpenCount", "(I)I");
	UMIDI20_RESOLVE_FUNC(MidiDeviceStatus, isInputPortOpen, "isInputPortOpen", "(I)Z");
	UMIDI20_RESOLVE_FUNC(MidiDeviceStatus, toString, "toString", "()Ljava/lang/String;");
	UMIDI20_RESOLVE_FUNC(MidiDeviceStatus, writeToParcel, "writeToParcel", "(Ljava/lang/Object;I)V");

	UMIDI20_RESOLVE_FUNC(MidiInputPort, close, "close", "()V");
	UMIDI20_RESOLVE_FUNC(MidiInputPort, getPortNumber, "getPortNumber", "()I");
	UMIDI20_RESOLVE_FUNC(MidiInputPort, onFlush, "onFlush", "()V");
	UMIDI20_RESOLVE_FUNC(MidiInputPort, onSend, "onSend", "([BIIJ)V");

	UMIDI20_RESOLVE_FUNC(MidiManager, getDevices, "getDevices", "()[Ljava/lang/Object;");
	UMIDI20_RESOLVE_FUNC(MidiManager, openBluetoothDevice, "openBluetoothDevice", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V");
	UMIDI20_RESOLVE_FUNC(MidiManager, openDevice, "openDevice", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V");
	UMIDI20_RESOLVE_FUNC(MidiManager, registerDeviceCallback, "registerDeviceCallback", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V");
	UMIDI20_RESOLVE_FUNC(MidiManager, unregisterDeviceCallback, "unregisterDeviceCallback", "(Ljava/lang/Object;)V");

	UMIDI20_RESOLVE_FUNC(MidiManager_DeviceCallback, onDeviceAdded, "onDeviceAdded", "(Ljava/lang/Object;)V");
	UMIDI20_RESOLVE_FUNC(MidiManager_DeviceCallback, onDeviceRemoved, "onDeviceRemoved", "(Ljava/lang/Object;)V");
	UMIDI20_RESOLVE_FUNC(MidiManager_DeviceCallback, onDeviceStatusChanged, "onDeviceStatusChanged", "(Ljava/lang/Object;)V");

	UMIDI20_RESOLVE_FUNC(MidiOutputPort, close, "close", "()V");
	UMIDI20_RESOLVE_FUNC(MidiOutputPort, getPortNumber, "getPortNumber", "()I");
	UMIDI20_RESOLVE_FUNC(MidiOutputPort, onConnect, "onConnect", "(Ljava/lang/Object;)V");
	UMIDI20_RESOLVE_FUNC(MidiOutputPort, onDisconnect, "onDisconnect", "(Ljava/lang/Object;)V");

	UMIDI20_RESOLVE_FUNC(MidiReceiver, flush, "flush", "()V");
	UMIDI20_RESOLVE_FUNC(MidiReceiver, getMaxMessageSize, "getMaxMessageSize", "()I");
	UMIDI20_RESOLVE_FUNC(MidiReceiver, onFlush, "onFlush", "()V");
	UMIDI20_RESOLVE_FUNC(MidiReceiver, onSend, "onSend", "([BIIJ)V");
	UMIDI20_RESOLVE_FUNC(MidiReceiver, send, "send", "([BII)V");
	UMIDI20_RESOLVE_FUNC(MidiReceiver, sendTs, "send", "([BIIJ)V");

	UMIDI20_RESOLVE_FUNC(MidiSender, connect, "connect", "(Ljava/lang/Object;)V");
	UMIDI20_RESOLVE_FUNC(MidiSender, disconnect, "disconnect", "(Ljava/lang/Object;)V");
	UMIDI20_RESOLVE_FUNC(MidiSender, onConnect, "onConnect", "(Ljava/lang/Object;)V");
	UMIDI20_RESOLVE_FUNC(MidiSender, onDisconnect, "onDisconnect", "(Ljava/lang/Object;)V");

	/* Register on send function */
	if (UMIDI20_MTOD(RegisterNatives, umidi20_class.MidiReceiver.class, &umidi20_android_method_table[1], 1))
		return (-1);

	/* basic init */
	umidi20_MidiManager = UMIDI20_CALL(CallObjectMethod,
	    context.getSystemService, NULL, "midi");
	if (umidi20_MidiManager == NULL)
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

	umidi20_android_init_done = 1;

	return (0);
}
