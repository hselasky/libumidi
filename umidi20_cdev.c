/*-
 * Copyright (c) 2022 Hans Petter Selasky <hselasky@FreeBSD.org>
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

#include <sys/stat.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <paths.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include "umidi20.h"

struct umidi20_cdev {
	int	rx_fd;
	int	tx_fd;
	struct umidi20_pipe *read_pipe;
	struct umidi20_pipe *write_pipe;
};

static struct umidi20_cdev umidi20_cdev[UMIDI20_N_DEVICES];
static pthread_mutex_t umidi20_cdev_mtx;
static pthread_cond_t umidi20_cdev_cv;
static uint8_t umidi20_cdev_init_done;
static uint8_t umidi20_cdev_tx_work;

static void
umidi20_cdev_lock(void)
{
	pthread_mutex_lock(&umidi20_cdev_mtx);
}

static void
umidi20_cdev_unlock(void)
{
	pthread_mutex_unlock(&umidi20_cdev_mtx);
}

static void
umidi20_cdev_free(const void *ptr)
{
	free((void *)(uintptr_t)ptr);
}

const char **
umidi20_cdev_alloc_inputs(void)
{
	return (umidi20_cdev_alloc_outputs());
}

const char **
umidi20_cdev_alloc_outputs(void)
{
	enum {
	MAX = 256};
#if __BSD_VISIBLE == 0
	struct stat filestat;
#endif
	char *stmp;
	const char **retval;
	struct dirent *dp;
	DIR *dirp;
	size_t n;

	if (umidi20_cdev_init_done == 0)
		return (NULL);

	dirp = opendir("/dev");
	if (dirp == NULL)
		return (NULL);

	retval = malloc(sizeof(retval[0]) * MAX);
	n = 0;

	while ((dp = readdir(dirp)) != NULL) {
		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
			continue;
#if __BSD_VISIBLE == 0
		if (asprintf(&stmp, "/dev/%s", dp->d_name) < 0)
			continue;
		stat(stmp, &filestat);
		free(stmp);

		if (S_ISDIR(filestat.st_mode))
			continue;
		else if (S_ISREG(filestat.st_mode))
			continue;
#else
		switch (dp->d_type) {
		case DT_DIR:
		case DT_REG:
			continue;
		default:
			break;
		}
#endif
		if (strstr(dp->d_name, "midi") == dp->d_name ||
		    strstr(dp->d_name, "umidi") == dp->d_name) {
			if (n < MAX - 1) {
				if (asprintf(&stmp, "/dev/%s", dp->d_name) < 0)
					continue;
				retval[n++] = stmp;
			}
		}
	}
	closedir(dirp);
	retval[n] = NULL;
	return (retval);
}

void
umidi20_cdev_free_inputs(const char **ptr)
{
	return (umidi20_cdev_free_outputs(ptr));
}

void
umidi20_cdev_free_outputs(const char **ptr)
{

	if (ptr == NULL)
		return;

	for (size_t x = 0; ptr[x] != NULL; x++)
		umidi20_cdev_free(ptr[x]);
	umidi20_cdev_free(ptr);
}

struct umidi20_pipe **
umidi20_cdev_rx_open(uint8_t n, const char *name)
{
	struct umidi20_cdev *puj;

	if (n >= UMIDI20_N_DEVICES || umidi20_cdev_init_done == 0)
		return (NULL);

	puj = &umidi20_cdev[n];

	if (puj->write_pipe != NULL)
		return (NULL);

	puj->rx_fd = open(name, O_RDONLY | O_NONBLOCK);
	if (puj->rx_fd < 0)
		return (NULL);

	/* set non-blocking I/O */
	fcntl(puj->rx_fd, F_SETFL, (int)O_NONBLOCK);

	umidi20_cdev_lock();
	umidi20_pipe_alloc(&puj->write_pipe, NULL);
	umidi20_cdev_unlock();

	return (&puj->write_pipe);
}

static void
umidi20_cdev_write_callback(void)
{
	umidi20_cdev_lock();
	umidi20_cdev_tx_work = 1;
	pthread_cond_broadcast(&umidi20_cdev_cv);
	umidi20_cdev_unlock();
}

struct umidi20_pipe **
umidi20_cdev_tx_open(uint8_t n, const char *name)
{
	struct umidi20_cdev *puj;

	if (n >= UMIDI20_N_DEVICES || umidi20_cdev_init_done == 0)
		return (NULL);

	puj = &umidi20_cdev[n];

	if (puj->read_pipe != NULL)
		return (NULL);

	puj->tx_fd = open(name, O_WRONLY | O_NONBLOCK);
	if (puj->tx_fd < 0)
		return (NULL);

	umidi20_cdev_lock();
	umidi20_pipe_alloc(&puj->read_pipe, &umidi20_cdev_write_callback);
	umidi20_cdev_unlock();

	return (&puj->read_pipe);
}

int
umidi20_cdev_rx_close(uint8_t n)
{
	struct umidi20_cdev *puj;

	if (n >= UMIDI20_N_DEVICES)
		return (-1);

	puj = &umidi20_cdev[n];
	close(puj->rx_fd);
	puj->rx_fd = -1;
	umidi20_pipe_free(&puj->write_pipe);

	return (0);
}

int
umidi20_cdev_tx_close(uint8_t n)
{
	struct umidi20_cdev *puj;

	if (n >= UMIDI20_N_DEVICES)
		return (-1);

	puj = &umidi20_cdev[n];
	close(puj->tx_fd);
	puj->tx_fd = -1;
	umidi20_pipe_free(&puj->read_pipe);

	return (0);
}

static void *
umidi20_cdev_rx_worker(void *arg)
{
	struct pollfd fds[UMIDI20_N_DEVICES];
	uint8_t buffer[16];
	int len;
	int x;

	while (1) {
		umidi20_cdev_lock();
		for (x = 0; x != UMIDI20_N_DEVICES; x++) {
			fds[x].revents = 0;
			fds[x].fd = umidi20_cdev[x].rx_fd;
			if (fds[x].fd > -1)
				fds[x].events = POLLIN | POLLHUP;
			else
				fds[x].events = 0;
		}
		umidi20_cdev_unlock();

		x = poll(fds, UMIDI20_N_DEVICES, 1000);
		if (x < 0)
			continue;

		umidi20_cdev_lock();
		for (x = 0; x != UMIDI20_N_DEVICES; x++) {
			if (fds[x].events == 0)
				continue;
			if (umidi20_cdev[x].rx_fd < 0)
				continue;
			while (1) {
				len = read(umidi20_cdev[x].rx_fd, buffer, sizeof(buffer));
				if (len < 0 && errno != EWOULDBLOCK) {
					close(umidi20_cdev[x].rx_fd);
					umidi20_cdev[x].rx_fd = -1;
					umidi20_pipe_free(&umidi20_cdev[x].write_pipe);
				} else if (len <= 0)
					break;
				umidi20_pipe_write_data(&umidi20_cdev[x].write_pipe, buffer, len);
			}
		}
		umidi20_cdev_unlock();
	}
	return (NULL);
}

static void *
umidi20_cdev_tx_worker(void *arg)
{
	uint8_t buffer[16];
	struct timespec ts;

	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		ts.tv_sec += 1;

		umidi20_cdev_lock();
		while (umidi20_cdev_tx_work == 0) {
			if (pthread_cond_timedwait(&umidi20_cdev_cv, &umidi20_cdev_mtx, &ts))
				break;
		}
		umidi20_cdev_tx_work = 0;

		for (unsigned x = 0; x != UMIDI20_N_DEVICES; x++) {
			ssize_t len;

			while (1) {
				len = umidi20_pipe_read_data(
				    &umidi20_cdev[x].read_pipe, buffer, sizeof(buffer));
				if (len >= 0) {
					if (write(umidi20_cdev[x].tx_fd, buffer, len) < 0) {
						close(umidi20_cdev[x].tx_fd);
						umidi20_cdev[x].tx_fd = -1;
						umidi20_pipe_free(&umidi20_cdev[x].read_pipe);
						break;
					}
					if (len == 0)
						break;
				} else {
					break;
				}
			}
		}
		umidi20_cdev_unlock();
	}
	return (NULL);
}

int
umidi20_cdev_init(const char *name)
{
	struct umidi20_cdev *puj;
	pthread_condattr_t attr;
	pthread_t td;
	uint8_t n;

	pthread_mutex_init(&umidi20_cdev_mtx, NULL);
	pthread_condattr_init(&attr);
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
	pthread_cond_init(&umidi20_cdev_cv, &attr);
	pthread_condattr_destroy(&attr);

	for (n = 0; n != UMIDI20_N_DEVICES; n++) {
		puj = &umidi20_cdev[n];
		puj->read_pipe = NULL;
		puj->write_pipe = NULL;
		puj->rx_fd = -1;
		puj->tx_fd = -1;
	}

	umidi20_cdev_init_done = 1;

	pthread_create(&td, NULL, &umidi20_cdev_rx_worker, NULL);
	pthread_create(&td, NULL, &umidi20_cdev_tx_worker, NULL);

	return (0);
}
