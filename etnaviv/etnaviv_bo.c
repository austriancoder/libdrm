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
# include <config.h>
#endif

#include "etnaviv_priv.h"
#include "etnaviv_drmif.h"

static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;

/* set buffer name, and add to table, call w/ table_lock held: */
static void set_name(struct etna_bo *bo, uint32_t name)
{
	bo->name = name;
	/* add ourself into the handle table: */
	drmHashInsert(bo->dev->handle_table, name, bo);
}

/* lookup a buffer from it's handle, call w/ table_lock held: */
static struct etna_bo * lookup_bo(struct etna_device *dev,
		uint32_t handle)
{
	struct etna_bo *bo = NULL;
	if (!drmHashLookup(dev->handle_table, handle, (void **)&bo)) {
		/* found, incr refcnt and return: */
		bo = etna_bo_ref(bo);
	}
	return bo;
}

/* allocate a new buffer object, call w/ table_lock held */
static struct etna_bo * bo_from_handle(struct etna_device *dev,
		uint32_t size, uint32_t handle)
{
	unsigned int i;

	struct etna_bo *bo = calloc(sizeof(*bo), 1);
	if (!bo) {
		struct drm_gem_close req = {
				.handle = handle,
		};
		drmIoctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &req);
		return NULL;
	}

	for (i = 0; i < ARRAY_SIZE(bo->list); i++)
		list_inithead(&bo->list[i]);

	bo->dev = etna_device_ref(dev);
	bo->size = size;
	bo->handle = handle;
	atomic_set(&bo->refcnt, 1);
	/* add ourselves to the handle table: */
	drmHashInsert(dev->handle_table, handle, bo);
	return bo;
}

/* allocate a new (un-tiled) buffer object */
struct etna_bo *etna_bo_new(struct etna_device *dev,
		uint32_t size, uint32_t flags)
{
	struct etna_bo *bo = NULL;

	struct drm_etnaviv_gem_new req = {
			.size = size,
			.flags = flags,
	};

	int ret;
	ret = drmCommandWriteRead(dev->fd, DRM_ETNAVIV_GEM_NEW,
			&req, sizeof(req));
	if (ret)
		return NULL;

	pthread_mutex_lock(&table_lock);
	bo = bo_from_handle(dev, size, req.handle);
	pthread_mutex_unlock(&table_lock);

	return bo;
}

struct etna_bo * etna_bo_ref(struct etna_bo *bo)
{
	atomic_inc(&bo->refcnt);
	return bo;
}

/* get buffer info */
static int get_buffer_info(struct etna_bo *bo)
{
	struct drm_etnaviv_gem_info req = {
			.handle = bo->handle,
	};
	int ret = drmCommandWriteRead(bo->dev->fd, DRM_ETNAVIV_GEM_INFO,
			&req, sizeof(req));
	if (ret) {
		return ret;
	}

	/* really all we need for now is mmap offset */
	bo->offset = req.offset;

	return 0;
}

/* import a buffer object from DRI2 name */
struct etna_bo * etna_bo_from_name(struct etna_device *dev, uint32_t name)
{
	struct drm_gem_open req = {
			.name = name,
	};
	struct etna_bo *bo;

	pthread_mutex_lock(&table_lock);

	/* check name table first, to see if bo is already open: */
	bo = lookup_bo(dev, req.handle);
	if (bo)
		goto out_unlock;

	if (drmIoctl(dev->fd, DRM_IOCTL_GEM_OPEN, &req)) {
		ERROR_MSG("gem-open failed: %s", strerror(errno));
		goto out_unlock;
	}

	bo = lookup_bo(dev, req.handle);
	if (bo)
		goto out_unlock;

	bo = bo_from_handle(dev, req.size, req.handle);
	if (bo)
		set_name(bo, name);

out_unlock:
	pthread_mutex_unlock(&table_lock);

	return bo;
}

/* import a buffer from dmabuf fd, does not take ownership of the
 * fd so caller should close() the fd when it is otherwise done
 * with it (even if it is still using the 'struct etna_bo *')
 */
struct etna_bo * etna_bo_from_dmabuf(struct etna_device *dev, int fd)
{
	struct etna_bo *bo = NULL;
	struct drm_prime_handle req = {
			.fd = fd,
	};
	int ret, size;

	pthread_mutex_lock(&table_lock);

	ret = drmIoctl(dev->fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &req);
	if (ret) {
		goto fail;
	}

	/* hmm, would be nice if we had a way to figure out the size.. */
	size = 0;

	bo = lookup_bo(dev, req.handle);
	if (!bo) {
		bo = bo_from_handle(dev, size, req.handle);
	}

	pthread_mutex_unlock(&table_lock);

	return bo;

fail:
	pthread_mutex_unlock(&table_lock);
	free(bo);
	return NULL;
}

/* destroy a buffer object */
void etna_bo_del(struct etna_bo *bo)
{
	if (!bo)
		return;

	if (!atomic_dec_and_test(&bo->refcnt))
		return;

	if (bo->map)
		drm_munmap(bo->map, bo->size);

	if (bo->fd)
		close(bo->fd);

	if (bo->handle) {
		struct drm_gem_close req = {
				.handle = bo->handle,
		};
		pthread_mutex_lock(&table_lock);
		drmHashDelete(bo->dev->handle_table, bo->handle);
		drmIoctl(bo->dev->fd, DRM_IOCTL_GEM_CLOSE, &req);
		pthread_mutex_unlock(&table_lock);
	}

	etna_device_del(bo->dev);

	free(bo);
}

/* get the global flink/DRI2 buffer name */
int etna_bo_get_name(struct etna_bo *bo, uint32_t *name)
{
	if (!bo->name) {
		struct drm_gem_flink req = {
				.handle = bo->handle,
		};
		int ret;

		ret = drmIoctl(bo->dev->fd, DRM_IOCTL_GEM_FLINK, &req);
		if (ret) {
			return ret;
		}

		bo->name = req.name;
	}

	*name = bo->name;

	return 0;
}

uint32_t etna_bo_handle(struct etna_bo *bo)
{
	return bo->handle;
}

/* caller owns the dmabuf fd that is returned and is responsible
 * to close() it when done
 */
int etna_bo_dmabuf(struct etna_bo *bo)
{
	if (!bo->fd) {
		struct drm_prime_handle req = {
				.handle = bo->handle,
				.flags = DRM_CLOEXEC,
		};
		int ret;

		ret = drmIoctl(bo->dev->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &req);
		if (ret) {
			return ret;
		}

		bo->fd = req.fd;
	}
	return dup(bo->fd);
}

uint32_t etna_bo_size(struct etna_bo *bo)
{
	if (!bo->size) {
		get_buffer_info(bo);
	}
	return bo->size;
}

void * etna_bo_map(struct etna_bo *bo)
{
	if (!bo->map) {
		if (!bo->offset) {
			get_buffer_info(bo);
		}

		bo->map = drm_mmap(0, bo->size, PROT_READ | PROT_WRITE,
				MAP_SHARED, bo->dev->fd, bo->offset);
		if (bo->map == MAP_FAILED) {
			bo->map = NULL;
		}
	}
	return bo->map;
}

int etna_bo_cpu_prep(struct etna_bo *bo, uint32_t op)
{
	struct drm_etnaviv_gem_cpu_prep req = {
			.handle = bo->handle,
			.op = op,
	};

	get_abs_timeout(&req.timeout, 5000);

	return drmCommandWrite(bo->dev->fd, DRM_ETNAVIV_GEM_CPU_PREP,
			&req, sizeof(req));
}

void etna_bo_cpu_fini(struct etna_bo *bo)
{
	struct drm_etnaviv_gem_cpu_fini req = {
			.handle = bo->handle,
	};

	drmCommandWrite(bo->dev->fd, DRM_ETNAVIV_GEM_CPU_FINI,
			&req, sizeof(req));
}

