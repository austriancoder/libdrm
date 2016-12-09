/*
 * Copyright (c) 2012-2016 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include "xf86drm.h"
#include "etnaviv_drmif.h"
#include "etnaviv_drm.h"

#include "state.xml.h"
#include "state_2d.xml.h"
#include "cmdstream.xml.h"

static const char clear_screen[] = {0x1b, '[', 'H',
                                    0x1b, '[', 'J',
                                    0x0};
static const char color_num[] = "\x1b[1;33m";
static const char color_head[] = "\x1b[1;37;100m";
static const char color_reset[] = "\x1b[0m";

static void read_register(struct etna_cmd_stream *stream, struct etna_readback *r,
		uint32_t address, uint32_t perf_reg, uint32_t perf_value)
{
	r->offset = 0;
	r->reg = address;
	r->perf_reg = perf_reg;
	r->perf_value = perf_value;
	r->flags = ETNA_READBACK_PERF;

	etna_cmd_stream_readback(stream, r);
	etna_cmd_stream_finish(stream);
}

struct debug_register
{
    const char *module;
    uint32_t select_reg;
    uint32_t select_shift;
    uint32_t read_reg;
    uint32_t count;
    uint32_t signature;
};

/* XXX possible to select/clear four debug registers at a time? this would
 * avoid writes.
 */
static struct debug_register debug_registers[] =
{
    { "RA", 0x474, 16, 0x448, 16, 0x12344321 },
    { "TX", 0x474, 24, 0x44C, 16, 0x12211221 },
    { "FE", 0x470,  0, 0x450, 16, 0xBABEF00D },
    { "PE", 0x470, 16, 0x454, 16, 0xBABEF00D },
    { "DE", 0x470,  8, 0x458, 16, 0xBABEF00D },
    { "SH", 0x470, 24, 0x45C, 16, 0xDEADBEEF },
    { "PA", 0x474,  0, 0x460, 16, 0x0000AAAA },
    { "SE", 0x474,  8, 0x464, 16, 0x5E5E5E5E },
    { "MC", 0x478,  0, 0x468, 16, 0x12345678 },
    { "HI", 0x478,  8, 0x46C, 16, 0xAAAAAAAA }
};
#define NUM_MODULES (sizeof(debug_registers) / sizeof(struct debug_register))
#define MAX_COUNT 16

static void loop(struct etna_cmd_stream *stream, struct etna_bo *bo)
{
    uint32_t counters[NUM_MODULES][MAX_COUNT] = {{}};
    uint32_t counters_prev[NUM_MODULES][MAX_COUNT] = {{}};
    int interval = 1000000;
    int reset = 0; /* reset counters after read */

	struct etna_readback r = {
		.bo = bo
	};

	uint32_t *data = etna_bo_map(r.bo);

    int has_prev = 0;
    while (true) {
        printf("%s", clear_screen);

        for (unsigned rid = 0; rid < NUM_MODULES; rid++) {
            struct debug_register *rdesc = &debug_registers[rid];

            for (unsigned sid = 0; sid < 15; sid++) {
                read_register(stream, &r, rdesc->read_reg, rdesc->select_reg, sid << rdesc->select_shift);
                counters[rid][sid] = *data;
            }

            if (reset) {
                read_register(stream, &r, rdesc->read_reg, rdesc->select_reg, 15 << rdesc->select_shift);
                counters[rid][15] = *data;
            }
        }

        printf("%s  ", color_head);
        for (unsigned rid = 0; rid < NUM_MODULES; rid++)
            printf("   %-2s    ", debug_registers[rid].module);

        printf("%s\n",color_reset);
        for (unsigned sid = 0; sid < MAX_COUNT; sid++) {
            printf("%s%01x%s ", color_head, sid, color_reset);

            for(unsigned rid = 0; rid < NUM_MODULES; rid++) {
                const char *color = "";

                if (has_prev && counters[rid][sid] != counters_prev[rid][sid])
                    color = color_num;
                printf("%s%08x%s ", color, counters[rid][sid], color_reset);
            }
            printf("\n");
        }
        usleep(interval);

        for(unsigned int rid=0; rid<NUM_MODULES; ++rid)
            for(unsigned int sid=0; sid<MAX_COUNT; ++sid)
                counters_prev[rid][sid] = counters[rid][sid];
        has_prev = 1;
    }
}

int main(int argc, char *argv[])
{
	struct etna_device *dev;
	struct etna_gpu *gpu;
	struct etna_pipe *pipe;
	struct etna_bo *bo;
	struct etna_cmd_stream *stream;
	uint32_t *values;

	drmVersionPtr version;
	int fd, ret, core = 0;

	fd = open(argv[1], O_RDWR);
	if (fd < 0)
		return 1;

	if (argc == 3)
		core = atoi(argv[2]);

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

	gpu = etna_gpu_new(dev, core);
	if (!gpu) {
		ret = 3;
		goto out_device;
	}

	pipe = etna_pipe_new(gpu, ETNA_PIPE_3D);
	if (!pipe) {
		ret = 4;
		goto out_gpu;
	}

	bo = etna_bo_new(dev, 0x4, ETNA_BO_UNCACHED);
	if (!bo) {
		ret = 5;
		goto out_pipe;
	}
	memset(etna_bo_map(bo), 0, 0x4);

	stream = etna_cmd_stream_new(pipe, 0x300, NULL, NULL);
	if (!stream) {
		ret = 6;
		goto out_bo;
	}

	loop(stream, bo);

	etna_cmd_stream_del(stream);

	out_bo:
	    etna_bo_del(bo);

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
