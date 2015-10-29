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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>

#include "etnaviv_drmif.h"
#include "etnaviv_priv.h"

#define CMD_STREAM_SIZE				0x8000
#define CMD_STREAM_END_CLEARANCE	24		/* LINK op code */

static void *grow(void *ptr, uint32_t nr, uint32_t *max, uint32_t sz)
{
	if ((nr + 1) > *max) {
		if ((*max * 2) < (nr + 1))
			*max = nr + 5;
		else
			*max = *max * 2;
		ptr = realloc(ptr, *max * sz);
	}
	return ptr;
}

#define APPEND(x, name) ({ \
	(x)->name = grow((x)->name, (x)->nr_ ## name, &(x)->max_ ## name, sizeof((x)->name[0])); \
	(x)->nr_ ## name ++; \
})

struct etna_cmd_stream *etna_cmd_stream_new(struct etna_pipe *pipe,
		void (*reset_notify)(struct etna_cmd_stream *stream, void *priv),
		void *priv)
{
	struct etna_cmd_stream *stream;

	stream = calloc(1, sizeof(*stream));
	if (!stream) {
		ERROR_MSG("allocation failed");
		goto fail;
	}

	stream->buffer = malloc(CMD_STREAM_SIZE);
	if (!stream->buffer) {
		ERROR_MSG("allocation failed");
		goto fail;
	}

	list_inithead(&stream->submit_list);

	stream->pipe = pipe;
	stream->reset_notify = reset_notify;
	stream->reset_notify_priv = priv;

	return stream;

fail:
	if (stream)
		etna_cmd_stream_del(stream);
	return NULL;
}

void etna_cmd_stream_del(struct etna_cmd_stream *stream)
{
	free(stream->buffer);
	free(stream->relocs);
	free(stream);
}

static void reset_buffer(struct etna_cmd_stream *stream)
{
	stream->offset = 0;
	stream->nr_bos = 0;
	stream->nr_relocs = 0;

	if (stream->reset_notify)
		stream->reset_notify(stream, stream->reset_notify_priv);
}

void etna_cmd_stream_reserve(struct etna_cmd_stream *stream, size_t n)
{
	stream->offset = ALIGN(stream->offset, 2);

	if ((stream->offset + n) * 4 + CMD_STREAM_END_CLEARANCE <= CMD_STREAM_SIZE)
	{
		return;
	}

	etna_cmd_stream_flush(stream);
}

void etna_cmd_stream_emit(struct etna_cmd_stream *stream, uint32_t data)
{
	stream->buffer[stream->offset++] = data;
}

uint32_t etna_cmd_stream_get(struct etna_cmd_stream *stream, uint32_t offset)
{
	return stream->buffer[offset];
}

void etna_cmd_stream_set(struct etna_cmd_stream *stream, uint32_t offset,
		uint32_t data)
{
	stream->buffer[offset] = data;
}

uint32_t etna_cmd_stream_offset(struct etna_cmd_stream *stream)
{
	return stream->offset;
}

uint32_t etna_cmd_stream_timestamp(struct etna_cmd_stream *stream)
{
	return stream->last_timestamp;
}

/* add (if needed) bo, return idx: */
static uint32_t bo2idx(struct etna_cmd_stream *stream, struct etna_bo *bo,
		uint32_t flags)
{
	int id = stream->pipe->id;
	uint32_t idx;
	if (!bo->indexp1[id]) {
		struct list_head *list = &bo->list[id];
		idx = APPEND(stream, bos);
		stream->bos[idx].flags = 0;
		stream->bos[idx].handle = bo->handle;
		bo->indexp1[id] = idx + 1;

		assert(LIST_IS_EMPTY(list));
		etna_bo_ref(bo);
		list_addtail(list, &stream->submit_list);
	} else {
		idx = bo->indexp1[id] - 1;
	}
	if (flags & ETNA_RELOC_READ)
		stream->bos[idx].flags |= ETNA_SUBMIT_BO_READ;
	if (flags & ETNA_RELOC_WRITE)
		stream->bos[idx].flags |= ETNA_SUBMIT_BO_WRITE;
	return idx;
}

static void flush(struct etna_cmd_stream *stream)
{
	int ret, id = stream->pipe->id;
	struct etna_gpu *gpu = stream->pipe->gpu;
	struct etna_bo *etna_bo = NULL, *tmp;
	struct drm_etnaviv_gem_submit req;

	req.pipe = gpu->core;
	req.exec_state = id;
	req.bos = VOID2U64(stream->bos);
	req.nr_bos = stream->nr_bos;
	req.relocs = VOID2U64(stream->relocs);
	req.nr_relocs = stream->nr_relocs;
	req.stream = VOID2U64(stream->buffer);
	req.stream_size = stream->offset * 4; /* in bytes */

	ret = drmCommandWriteRead(gpu->dev->fd, DRM_ETNAVIV_GEM_SUBMIT,
			&req, sizeof(req));

	if (ret) {
		ERROR_MSG("submit failed: %d (%s)", ret, strerror(errno));
	} else {
		stream->last_timestamp = req.fence;
	}

	LIST_FOR_EACH_ENTRY_SAFE(etna_bo, tmp, &stream->submit_list, list[id]) {
		struct list_head *list = &etna_bo->list[id];
		list_delinit(list);
		etna_bo->indexp1[id] = 0;
	}
}

void etna_cmd_stream_flush(struct etna_cmd_stream *stream)
{
	flush(stream);
	reset_buffer(stream);
}

void etna_cmd_stream_finish(struct etna_cmd_stream *stream)
{
	flush(stream);
	etna_pipe_wait(stream->pipe, stream->last_timestamp, 5000);
	reset_buffer(stream);
}

void etna_cmd_stream_reloc(struct etna_cmd_stream *stream, const struct etna_reloc *r)
{
	struct drm_etnaviv_gem_submit_reloc *reloc;
	uint32_t idx = APPEND(stream, relocs);
	uint32_t addr = 0;

	reloc = &stream->relocs[idx];

	reloc->reloc_idx = bo2idx(stream, r->bo, r->flags);
	reloc->reloc_offset = r->offset;
	reloc->submit_offset = stream->offset * 4; /* in bytes */

	etna_cmd_stream_emit(stream, addr);
}
