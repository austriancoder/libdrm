/*
 * Copyright (C) 2014 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Christian Gmeiner <christian.gmeiner@gmail.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <xf86drm.h>
#include <xf86atomic.h>

#include "etnaviv_priv.h"
#include "etnaviv_drmif.h"

static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;
static void * dev_table;

static struct etna_device * etna_device_new_impl(int fd)
{
	struct etna_device *dev = calloc(sizeof(*dev), 1);
	if (!dev)
		return NULL;
	dev->fd = fd;
	atomic_set(&dev->refcnt, 1);
	dev->handle_table = drmHashCreate();
	return dev;
}

struct etna_device * etna_device_new(int fd)
{
	struct etna_device *dev = NULL;

	pthread_mutex_lock(&table_lock);

	if (!dev_table)
		dev_table = drmHashCreate();

	if (drmHashLookup(dev_table, fd, (void **)&dev)) {
		/* not found, create new device */
		dev = etna_device_new_impl(fd);
		drmHashInsert(dev_table, fd, dev);
	} else {
		/* found, just incr refcnt */
		dev = etna_device_ref(dev);
	}

	pthread_mutex_unlock(&table_lock);

	return dev;
}

struct etna_device * etna_device_ref(struct etna_device *dev)
{
	atomic_inc(&dev->refcnt);
	return dev;
}

void etna_device_del(struct etna_device *dev)
{
	if (!atomic_dec_and_test(&dev->refcnt))
		return;
	pthread_mutex_lock(&table_lock);
	drmHashDestroy(dev->handle_table);
	drmHashDelete(dev_table, dev->fd);
	pthread_mutex_unlock(&table_lock);
	free(dev);
}

