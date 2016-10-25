/*
 * Copyright (C) 2016 Etnaviv Project
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
#  include "config.h"
#endif

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include "xf86drm.h"
#include "etnaviv_drmif.h"
#include "etnaviv_drm.h"

#define PRINT_PARAM(x) \
	if (etna_gpu_get_param(gpu, x, &val)) { \
		fprintf(stderr, "could not get " #x); \
		goto out_gpu; \
	} \
	printf(#x ": 0x%" PRIx64 "\n", val);

int main(int argc, char *argv[])
{
	struct etna_device *dev;
	struct etna_gpu *gpu;

	drmVersionPtr version;
	int fd, ret = 0;
	uint64_t val;

	fd = open(argv[1], O_RDWR);
	if (fd < 0)
		return 1;

	version = drmGetVersion(fd);
	if (version) {
		printf("Version: %d.%d.%d\n", version->version_major,
		       version->version_minor, version->version_patchlevel);
		printf("  Name: %s\n", version->name);
		printf("  Date: %s\n", version->date);
		printf("  Description: %s\n", version->desc);
		drmFreeVersion(version);
	}

	dev = etna_device_new(fd);
	if (!dev) {
		ret = 2;
		goto out;
	}

	/* TODO: we assume that core 1 is a 3D capable one */
	gpu = etna_gpu_new(dev, 1);
	if (!gpu) {
		ret = 3;
		goto out_device;
	}

	PRINT_PARAM(ETNA_GPU_MODEL);
	PRINT_PARAM(ETNA_GPU_REVISION);
	PRINT_PARAM(ETNA_GPU_FEATURES_0);
	PRINT_PARAM(ETNA_GPU_FEATURES_1);
	PRINT_PARAM(ETNA_GPU_FEATURES_2);
	PRINT_PARAM(ETNA_GPU_FEATURES_3);
	PRINT_PARAM(ETNA_GPU_FEATURES_4);
	PRINT_PARAM(ETNA_GPU_FEATURES_5);
	PRINT_PARAM(ETNA_GPU_FEATURES_6);
	PRINT_PARAM(ETNA_GPU_STREAM_COUNT);
	PRINT_PARAM(ETNA_GPU_REGISTER_MAX);
	PRINT_PARAM(ETNA_GPU_THREAD_COUNT);
	PRINT_PARAM(ETNA_GPU_VERTEX_CACHE_SIZE);
	PRINT_PARAM(ETNA_GPU_SHADER_CORE_COUNT);
	PRINT_PARAM(ETNA_GPU_PIXEL_PIPES);
	PRINT_PARAM(ETNA_GPU_VERTEX_OUTPUT_BUFFER_SIZE);
	PRINT_PARAM(ETNA_GPU_BUFFER_SIZE);
	PRINT_PARAM(ETNA_GPU_INSTRUCTION_COUNT);
	PRINT_PARAM(ETNA_GPU_NUM_CONSTANTS);
	PRINT_PARAM(ETNA_GPU_NUM_VARYINGS);

out_gpu:
	etna_gpu_del(gpu);

out_device:
	etna_device_del(dev);

out:
	close(fd);

	return ret;
}
