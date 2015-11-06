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

static inline struct etna_cmd_stream_priv *
etna_cmd_stream_priv(struct etna_cmd_stream *stream)
{
    return (struct etna_cmd_stream_priv *)stream;
}

struct etna_cmd_stream *etna_cmd_stream_new(struct etna_pipe *pipe, uint32_t size,
		void (*reset_notify)(struct etna_cmd_stream *stream, void *priv),
		void *priv)
{
	struct etna_cmd_stream_priv *stream = NULL;

	if (size == 0) {
		ERROR_MSG("invalid size of 0");
		goto fail;
	}

	stream = calloc(1, sizeof(*stream));
	if (!stream) {
		ERROR_MSG("allocation failed");
		goto fail;
	}

	/* allocate even number of 32-bit words */
	size = ALIGN(size, 2);

	stream->base.buffer = malloc(size * sizeof(uint32_t));
	if (!stream->base.buffer) {
		ERROR_MSG("allocation failed");
		goto fail;
	}

	list_inithead(&stream->submit_list);

	stream->base.size = size;
	stream->pipe = pipe;
	stream->reset_notify = reset_notify;
	stream->reset_notify_priv = priv;

	return &stream->base;

fail:
	if (stream)
		etna_cmd_stream_del(&stream->base);
	return NULL;
}

void etna_cmd_stream_del(struct etna_cmd_stream *stream)
{
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);

	free(stream->buffer);
	free(priv->relocs);
	free(priv);
}

static void reset_buffer(struct etna_cmd_stream *stream)
{
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);

	stream->offset = 0;
	priv->nr_bos = 0;
	priv->nr_relocs = 0;

	if (priv->reset_notify)
		priv->reset_notify(stream, priv->reset_notify_priv);
}

uint32_t etna_cmd_stream_timestamp(struct etna_cmd_stream *stream)
{
	return etna_cmd_stream_priv(stream)->last_timestamp;
}

/* add (if needed) bo, return idx: */
static uint32_t bo2idx(struct etna_cmd_stream *stream, struct etna_bo *bo,
		uint32_t flags)
{
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);
	int id = priv->pipe->id;
	uint32_t idx;

	if (!bo->indexp1[id]) {
		struct list_head *list = &bo->list[id];
		idx = APPEND(priv, bos);
		priv->bos[idx].flags = 0;
		priv->bos[idx].handle = bo->handle;
		bo->indexp1[id] = idx + 1;

		assert(LIST_IS_EMPTY(list));
		etna_bo_ref(bo);
		list_addtail(list, &priv->submit_list);
	} else {
		idx = bo->indexp1[id] - 1;
	}
	if (flags & ETNA_RELOC_READ)
		priv->bos[idx].flags |= ETNA_SUBMIT_BO_READ;
	if (flags & ETNA_RELOC_WRITE)
		priv->bos[idx].flags |= ETNA_SUBMIT_BO_WRITE;
	return idx;
}

static void flush(struct etna_cmd_stream *stream)
{
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);
	int ret, id = priv->pipe->id;
	struct etna_gpu *gpu = priv->pipe->gpu;
	struct etna_bo *etna_bo = NULL, *tmp;
	struct drm_etnaviv_gem_submit req;

	req.pipe = gpu->core;
	req.exec_state = id;
	req.bos = VOID2U64(priv->bos);
	req.nr_bos = priv->nr_bos;
	req.relocs = VOID2U64(priv->relocs);
	req.nr_relocs = priv->nr_relocs;
	req.stream = VOID2U64(stream->buffer);
	req.stream_size = stream->offset * 4; /* in bytes */

	ret = drmCommandWriteRead(gpu->dev->fd, DRM_ETNAVIV_GEM_SUBMIT,
			&req, sizeof(req));

	if (ret) {
		ERROR_MSG("submit failed: %d (%s)", ret, strerror(errno));
	} else {
		priv->last_timestamp = req.fence;
	}

	LIST_FOR_EACH_ENTRY_SAFE(etna_bo, tmp, &priv->submit_list, list[id]) {
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
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);

	flush(stream);
	etna_pipe_wait(priv->pipe, priv->last_timestamp, 5000);
	reset_buffer(stream);
}

void etna_cmd_stream_reloc(struct etna_cmd_stream *stream, const struct etna_reloc *r)
{
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);
	struct drm_etnaviv_gem_submit_reloc *reloc;
	uint32_t idx = APPEND(priv, relocs);
	uint32_t addr = 0;

	reloc = &priv->relocs[idx];

	reloc->reloc_idx = bo2idx(stream, r->bo, r->flags);
	reloc->reloc_offset = r->offset;
	reloc->submit_offset = stream->offset * 4; /* in bytes */

	etna_cmd_stream_emit(stream, addr);
}
