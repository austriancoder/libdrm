/*
 * Copyright (C) 2019 Etnaviv Project
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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xf86drm.h"
#include "drm_fourcc.h"
#include "etnaviv_drmif.h"
#include "etnaviv_drm.h"

#include "state.xml.h"
#include "cmdstream.xml.h"

#include "write_bmp.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static const unsigned char nv12_y[] =
{
	50,  70,  90, 110,
	50,  70,  90, 110,
	50,  70,  90, 110,
	50,  70,  90, 110,
};
static const unsigned char nv12_uv[] =
{
	120, 130, 140, 130,
	120, 160, 140, 160,
};

static const unsigned char yuv420_y[] = {
	50,  70,  90, 110,
	50,  70,  90, 110,
	50,  70,  90, 110,
	50,  70,  90, 110,
};
static const unsigned char yuv420_u[] = {
	120, 140,
	120, 140,
};
static const unsigned char yuv420_v[] = {
	130, 130,
	160, 160,
};

static const unsigned char yvu420_y[] = {
	50,  70,  90, 110,
	50,  70,  90, 110,
	50,  70,  90, 110,
	50,  70,  90, 110,
};
static const unsigned char yvu420_v[] = {
	130, 130,
	160, 160,
};
static const unsigned char yvu420_u[] = {
	120, 140,
	120, 140,
};

static const struct yuv {
	const char *name;
	const int fourcc;
	const unsigned char *p0;
	unsigned p0_size;
	const unsigned char *p1;
	unsigned p1_size;
	const unsigned char *p2;
	unsigned p2_size;
} data[] = {
	/* 2 plane YCbCr */
	{ "NV12", DRM_FORMAT_NV12, nv12_y, ARRAY_SIZE(nv12_y), nv12_uv, ARRAY_SIZE(nv12_uv), NULL, 0 },
	/* 3 plane YCbCr */
	{ "YUV420", DRM_FORMAT_YUV420, yuv420_y, ARRAY_SIZE(yuv420_y), yuv420_u, ARRAY_SIZE(yuv420_u), yuv420_v, ARRAY_SIZE(yuv420_v) },
	{ "YVU420", DRM_FORMAT_YVU420, yvu420_y, ARRAY_SIZE(yvu420_y), yvu420_u, ARRAY_SIZE(yvu420_u), yvu420_v, ARRAY_SIZE(yvu420_v) },
};

static const unsigned char expected[4 * 4 * 4] = {
	44,  41,  25, 255,
	67,  64,  48, 255,
	90,  79, 111, 255,
	114, 103, 135, 255,

	44,  41,  25, 255,
	67,  64,  48, 255,
	90,  79, 111, 255,
	114, 103, 135, 255,

	92,   16,  25, 255,
	115,  39,  48, 255,
	138,  55, 111, 255,
	161,  78, 135, 255,

	92,   16,  25, 255,
	115,  39,  48, 255,
	138,  55, 111, 255,
	161,  78, 135, 255,
};

static inline void etna_emit_load_state(struct etna_cmd_stream *stream,
		const uint16_t offset, const uint16_t count)
{
	uint32_t v;

	v = 	(VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE | VIV_FE_LOAD_STATE_HEADER_OFFSET(offset) |
			(VIV_FE_LOAD_STATE_HEADER_COUNT(count) & VIV_FE_LOAD_STATE_HEADER_COUNT__MASK));

	etna_cmd_stream_emit(stream, v);
}

static inline void etna_set_state(struct etna_cmd_stream *stream, uint32_t address, uint32_t value)
{
	etna_cmd_stream_reserve(stream, 2);
	etna_emit_load_state(stream, address >> 2, 1);
	etna_cmd_stream_emit(stream, value);
}

static inline void etna_set_state_from_bo(struct etna_cmd_stream *stream,
		uint32_t address, struct etna_bo *bo)
{
	etna_cmd_stream_reserve(stream, 2);
	etna_emit_load_state(stream, address >> 2, 1);

	etna_cmd_stream_reloc(stream, &(struct etna_reloc){
		.bo = bo,
		.flags = ETNA_RELOC_READ,
		.offset = 0,
	});
}

static void resolve(struct etna_cmd_stream *stream, int index, struct etna_bo *dest, struct etna_bo *plane[3])
{
	const struct yuv *d = &data[index];

	/* copy data into bos */
	unsigned char *p0 = etna_bo_map(plane[0]);
	unsigned char *p1 = etna_bo_map(plane[1]);
	unsigned char *p2 = etna_bo_map(plane[2]);

	memcpy(p0, d->p0, d->p0_size);
	memcpy(p1, d->p1, d->p1_size);

	if (d->p2)
		memcpy(p2, d->p2, d->p2_size);

	/* config */
	switch (d->fourcc) {
	case DRM_FORMAT_YVU420:
		etna_set_state(stream, 0x01678, 0x100 | 0x1);
		break;
	case DRM_FORMAT_YUV420:
		etna_set_state(stream, 0x01678, 0x0 | 0x1);
		break;
	case DRM_FORMAT_NV12:
		etna_set_state(stream, 0x01678, 0x10 | 0x1);
		break;
	default:
		printf("oops\n");
		exit(1);
		break;
	}

	/* size */
	etna_set_state(stream, 0x0167C, 4 << 16 | 4);

	/* plane 0 + stride */
	etna_set_state_from_bo(stream, 0x01680, plane[0]);
	etna_set_state(stream, 0x01684, 0xa0);

	/* plane 1 + stride */
	etna_set_state_from_bo(stream, 0x01688, plane[1]);
	etna_set_state(stream, 0x0168C, 0xa0);

	/* plane 2 + stride */
	if (d->p2) {
		etna_set_state_from_bo(stream, 0x01690, plane[2]);
		etna_set_state(stream, 0x01694, 0xa0);
	} else  {
		etna_set_state(stream, 0x01690, 0);
		etna_set_state(stream, 0x01694, 0x0);
	}

	/* dest + stride */
	etna_set_state_from_bo(stream, 0x01698, dest);
	etna_set_state(stream, 0x0169C, 0x140);

	/* configure RS */
	etna_set_state(stream, 0x0163C, 0);
	etna_set_state(stream, 0x0160C, 0);

	/* trigger resolve */
	etna_set_state(stream, 0x01600, 0xbadabeeb);

	/* disable yuv tiller */
	etna_set_state(stream, 0x01678, 0x0);
}

int main(int argc, char *argv[])
{
	const int width = 4;
	const int height = 4;
	const size_t bmp_size = width * height * 4;

	struct etna_device *dev;
	struct etna_gpu *gpu;
	struct etna_pipe *pipe;
	struct etna_bo *plane[3];
	struct etna_bo *bmp;
	struct etna_cmd_stream *stream;

	drmVersionPtr version;
	int fd, ret = 0;

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

	/* TODO: we assume that core 0 is a 3D capable one */
	gpu = etna_gpu_new(dev, 0);
	if (!gpu) {
		ret = 3;
		goto out_device;
	}

	pipe = etna_pipe_new(gpu, ETNA_PIPE_3D);
	if (!pipe) {
		ret = 4;
		goto out_gpu;
	}

	for (unsigned i = 0; i < 3; i++) {
		plane[i] = etna_bo_new(dev, bmp_size, ETNA_BO_UNCACHED);
		if (!plane[i]) {
			return -99;
		}
	}

	bmp = etna_bo_new(dev, bmp_size, ETNA_BO_UNCACHED);
	if (!bmp) {
		ret = 5;
		goto out_pipe;
	}
	memset(etna_bo_map(bmp), 0, bmp_size);

	stream = etna_cmd_stream_new(pipe, 0x300, NULL, NULL);
	if (!stream) {
		ret = 6;
		goto out_bo;
	}

	/* generate command sequence */
	for (unsigned i = 0; i < ARRAY_SIZE(data); i++) {
		resolve(stream, i, bmp, plane);

		etna_cmd_stream_finish(stream);

		char name[255];
		snprintf(name, sizeof(name), "/tmp/etna_yuv_%s.bmp", data[i].name);
		printf("%s\n", name);

		bmp_dump32(etna_bo_map(bmp), width, height, false, name);

		/* compare */
		{
			unsigned char *pixel = etna_bo_map(bmp);

			for (int y = 0; y < 4; y++) {
				for (int x = 0; x < 4; x++) {

					const unsigned char *excp = &expected[(y * 4 + x) * 4];
					const unsigned char *probe = &pixel[(y * 4 + x) * 4];

					printf("excp: %u\n", *excp);
					printf("pro: %u\n", *probe);
				}
			}
		}
	}

	etna_cmd_stream_del(stream);

out_bo:
	etna_bo_del(bmp);

out_pipe:
	etna_pipe_del(pipe);

out_gpu:
	etna_gpu_del(gpu);

out_device:
	etna_device_del(dev);

out:
	close(fd);

	return ret;
}
