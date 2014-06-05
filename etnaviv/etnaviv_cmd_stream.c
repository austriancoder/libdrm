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

struct etna_cmd_stream * etna_cmd_stream_new(struct etna_pipe *pipe)
{
	struct etna_cmd_stream *stream;
	int i;

	stream = calloc(1, sizeof(*stream));
	if (!stream) {
		ERROR_MSG("allocation failed");
		goto fail;
	}

	stream->submit_cmd = calloc(1, sizeof(*stream->submit_cmd));
	if (!stream->submit_cmd) {
		ERROR_MSG("allocation failed");
		goto fail;
	}

	for (i = 0; i < NUM_CMD_STREAMS; i++) {
		void *tmp;
		stream->stream[i] = etna_bo_new(pipe->dev, CMD_STREAM_SIZE, ETNA_BO_CMDSTREAM);

		if (!stream->stream[i]) {
			ERROR_MSG("allocation failed");
			goto fail;
		}

		tmp = etna_bo_map(stream->stream[i]);
		if (!tmp) {
			ERROR_MSG("mmap failed");
			goto fail;
		}
	}

	list_inithead(&stream->submit_list);

	stream->cmd = stream->stream[0]->map;
	stream->pipe = pipe;

	return stream;

fail:
	if (stream)
		etna_cmd_stream_del(stream);
	return NULL;
}

void etna_cmd_stream_del(struct etna_cmd_stream *stream)
{
	int i;

	for (i = 0; i < NUM_CMD_STREAMS; i++) {
		etna_bo_del(stream->stream[i]);
	}

	free(stream->submit_cmd);
	free(stream->relocs);
	free(stream);
}

static void switch_to_next_buffer(struct etna_cmd_stream *stream)
{
	int cmd_steam_idx = (stream->current_stream + 1) % NUM_CMD_STREAMS;

	stream->current_stream = cmd_steam_idx;
	stream->offset = 0;
	stream->cmd = stream->stream[cmd_steam_idx]->map;
	stream->nr_bos = 0;
	stream->nr_relocs = 0;

	/* make sure we can access the new cmd stream bo */
	etna_bo_cpu_prep(stream->stream[cmd_steam_idx], ETNA_PREP_WRITE);
}

void etna_cmd_stream_reserve(struct etna_cmd_stream *stream, size_t n)
{
	stream->offset = ALIGN(stream->offset, 2);

	if ((stream->offset + n) * 4 + CMD_STREAM_END_CLEARANCE <= CMD_STREAM_SIZE)
	{
		return;
	}

	etna_cmd_stream_flush(stream);
	switch_to_next_buffer(stream);
}

void etna_cmd_stream_emit(struct etna_cmd_stream *stream, uint32_t data)
{
	stream->cmd[stream->offset++] = data;
}

uint32_t etna_cmd_stream_get(struct etna_cmd_stream *stream, uint32_t offset)
{
	return stream->cmd[offset];
}

void etna_cmd_stream_set(struct etna_cmd_stream *stream, uint32_t offset,
		uint32_t data)
{
	stream->cmd[offset] = data;
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
	struct etna_bo *etna_bo = NULL, *tmp;
	struct drm_etnaviv_gem_submit_cmd *cmd = NULL;

	struct drm_etnaviv_gem_submit req = {
			.pipe = stream->pipe->id,
	};

	/* we are done with cpu access */
	etna_bo_cpu_fini(stream->stream[stream->current_stream]);

	cmd = stream->submit_cmd;
	cmd->submit_idx = bo2idx(stream, stream->stream[stream->current_stream], ETNA_RELOC_READ);
	cmd->size = stream->offset * 4; /* in bytes */
	cmd->relocs = VOID2U64(stream->relocs);
	cmd->nr_relocs = stream->nr_relocs;

	req.cmd = VOID2U64(cmd);
	req.bos = VOID2U64(stream->bos);
	req.nr_bos = stream->nr_bos;

	ret = drmCommandWriteRead(stream->pipe->dev->fd, DRM_ETNAVIV_GEM_SUBMIT,
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
	switch_to_next_buffer(stream);
}

void etna_cmd_stream_finish(struct etna_cmd_stream *stream)
{
	flush(stream);
	etna_pipe_wait(stream->pipe, stream->last_timestamp, 5000);
	switch_to_next_buffer(stream);
}

void etna_cmd_stream_reloc(struct etna_cmd_stream *stream, const struct etna_reloc *r)
{
	struct drm_etnaviv_gem_submit_reloc *reloc;
	uint32_t idx = APPEND(stream, relocs);
	uint32_t addr = 0;

	reloc = &stream->relocs[idx];

	reloc->reloc_idx = bo2idx(stream, r->bo, r->flags);
	reloc->reloc_offset = r->offset;
	reloc->or = r->or;
	reloc->shift = r->shift;
	reloc->submit_offset = stream->offset * 4; /* in bytes */

	if (r->shift < 0)
		addr >>= -r->shift;
	else
		addr <<= r->shift;
	etna_cmd_stream_emit(stream, addr | r->or);
}
