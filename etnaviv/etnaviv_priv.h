/*
 * Copyright (C) 2014-2015 Etnaviv Project
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

#ifndef ETNAVIV_PRIV_H_
#define ETNAVIV_PRIV_H_

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>

#include "libdrm_macros.h"
#include "xf86drm.h"
#include "xf86atomic.h"

#include "util_double_list.h"

#include "etnaviv_drmif.h"
//#include "drm.h"
#include "etnaviv_drm.h"

enum etna_features_word
{
	etna_chipFeatures = 0,
	etna_chipMinorFeatures0 = 1,
	etna_chipMinorFeatures1 = 2,
	etna_chipMinorFeatures2 = 3,
	etna_chipMinorFeatures3 = 4,
	VIV_FEATURES_WORD_COUNT /* Must be last */
};

struct etna_specs {
	uint32_t model;
	uint32_t revision;
	uint32_t features[VIV_FEATURES_WORD_COUNT];
	uint32_t stream_count;
	uint32_t register_max;
	uint32_t thread_count;
	uint32_t shader_core_count;
	uint32_t vertex_cache_size;
	uint32_t vertex_output_buffer_size;
	uint32_t pixel_pipes;
	uint32_t instruction_count;
	uint32_t num_constants;
	uint32_t buffer_size;
	uint32_t varyings_count;
};

struct etna_device {
	int fd;
	atomic_t refcnt;

	/* The handle_table is used to track GEM bo handles associated w/
	 * this fd.  This is needed, in particular, when importing
	 * dmabuf's because we don't want multiple 'struct etna_bo's
	 * floating around with the same handle.  Otherwise, when the
	 * first one is etna_bo_del()'d the handle becomes no longer
	 * valid, and the remaining 'struct etna_bo's are left pointing
	 * to an invalid handle (and possible a GEM bo that is already
	 * free'd).
	 */
	void *handle_table;
};

/* a GEM buffer object allocated from the DRM device */
struct etna_bo {
	struct etna_device      *dev;
	void            *map;           /* userspace mmap'ing (if there is one) */
	uint32_t        size;
	uint32_t        handle;
	uint32_t        name;           /* flink global handle (DRI2 name) */
	uint64_t        offset;         /* offset to mmap() */
	atomic_t        refcnt;

	uint32_t indexp1[ETNA_MAX_PIPES]; /* index plus 1 */
	struct list_head list[ETNA_MAX_PIPES];
};

struct etna_gpu {
	struct etna_device *dev;
	struct etna_specs specs;
	uint32_t core;
};

struct etna_pipe {
	enum etna_pipe_id id;
	struct etna_gpu *gpu;
};

struct etna_cmd_stream_priv {
	struct etna_cmd_stream base;
	struct etna_pipe *pipe;

	struct list_head submit_list;
	uint32_t last_timestamp;

	/* bo's table: */
	struct drm_etnaviv_gem_submit_bo *bos;
	uint32_t nr_bos, max_bos;

	/* reloc's table: */
	struct drm_etnaviv_gem_submit_reloc *relocs;
	uint32_t nr_relocs, max_relocs;

	/* notify callback if buffer reset happend */
	void (*reset_notify)(struct etna_cmd_stream *stream, void *priv);
	void *reset_notify_priv;
};

#define ALIGN(v,a) (((v) + (a) - 1) & ~((a) - 1))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define enable_debug 1  /* TODO make dynamic */

#define INFO_MSG(fmt, ...) \
		do { drmMsg("[I] "fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)
#define DEBUG_MSG(fmt, ...) \
		do if (enable_debug) { drmMsg("[D] "fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)
#define WARN_MSG(fmt, ...) \
		do { drmMsg("[W] "fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)
#define ERROR_MSG(fmt, ...) \
		do { drmMsg("[E] " fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)

#define U642VOID(x) ((void *)(unsigned long)(x))
#define VOID2U64(x) ((uint64_t)(unsigned long)(x))

static inline void get_abs_timeout(struct drm_etnaviv_timespec *tv, uint32_t ms)
{
	struct timespec t;
	uint32_t s = ms / 1000;
	clock_gettime(CLOCK_MONOTONIC, &t);
	tv->tv_sec = t.tv_sec + s;
	tv->tv_nsec = t.tv_nsec + ((ms - (s * 1000)) * 1000000);
}

#endif /* ETNAVIV_PRIV_H_ */
