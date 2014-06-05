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

#include "etnaviv_priv.h"

int etna_pipe_get_param(struct etna_pipe *pipe,
		enum etna_param_id param, uint64_t *value)
{
	switch(param) {
	case ETNA_GPU_MODEL:
		*value = pipe->specs.model;
		return 0;
	case ETNA_GPU_REVISION:
		*value = pipe->specs.revision;
		return 0;
	case ETNA_GPU_FEATURES_0:
		*value = pipe->specs.features[0];
		return 0;
	case ETNA_GPU_FEATURES_1:
		*value = pipe->specs.features[1];
		return 0;
	case ETNA_GPU_FEATURES_2:
		*value = pipe->specs.features[2];
		return 0;
	case ETNA_GPU_FEATURES_3:
		*value = pipe->specs.features[3];
		return 0;
	case ETNA_GPU_FEATURES_4:
		*value = pipe->specs.features[4];
		return 0;
	case ETNA_GPU_STREAM_COUNT:
		*value = pipe->specs.stream_count;
		return 0;
	case ETNA_GPU_REGISTER_MAX:
		*value = pipe->specs.register_max;
		return 0;
	case ETNA_GPU_THREAD_COUNT:
		*value = pipe->specs.thread_count;
		return 0;
	case ETNA_GPU_VERTEX_CACHE_SIZE:
		*value = pipe->specs.vertex_cache_size;
		return 0;
	case ETNA_GPU_SHADER_CORE_COUNT:
		*value = pipe->specs.shader_core_count;
		return 0;
	case ETNA_GPU_PIXEL_PIPES:
		*value = pipe->specs.pixel_pipes;
		return 0;
	case ETNA_GPU_VERTEX_OUTPUT_BUFFER_SIZE:
		*value = pipe->specs.vertex_output_buffer_size;
		return 0;
	case ETNA_GPU_BUFFER_SIZE:
		*value = pipe->specs.buffer_size;
		return 0;
	case ETNA_GPU_INSTRUCTION_COUNT:
		*value = pipe->specs.instruction_count;
		return 0;
	case ETNA_GPU_NUM_CONSTANTS:
		*value = pipe->specs.num_constants;
		return 0;

	default:
		ERROR_MSG("invalid param id: %d", param);
		return -1;
	}

	return 0;
}

int etna_pipe_wait(struct etna_pipe *pipe, uint32_t timestamp, uint32_t ms)
{
	struct etna_device *dev = pipe->dev;
	struct drm_etnaviv_wait_fence req = {
			.pipe = pipe->id,
			.fence = timestamp,
	};
	int ret;

	get_abs_timeout(&req.timeout, ms);

	ret = drmCommandWrite(dev->fd, DRM_ETNAVIV_WAIT_FENCE, &req, sizeof(req));
	if (ret) {
		ERROR_MSG("wait-fence failed! %d (%s)", ret, strerror(errno));
		return ret;
	}

	return 0;
}

void etna_pipe_del(struct etna_pipe *pipe)
{
	free(pipe);
}

static uint64_t get_param(struct etna_device *dev, uint32_t pipe, uint32_t param)
{
	struct drm_etnaviv_param req = {
			.pipe = pipe,
			.param = param,
	};
	int ret;

	ret = drmCommandWriteRead(dev->fd, DRM_ETNAVIV_GET_PARAM, &req, sizeof(req));
	if (ret) {
		ERROR_MSG("get-param failed! %d (%s)", ret, strerror(errno));
		return 0;
	}

	return req.value;
}

struct etna_pipe * etna_pipe_new(struct etna_device *dev, enum etna_pipe_id id)
{
	struct etna_pipe *pipe = NULL;

	pipe = calloc(1, sizeof(*pipe));
	if (!pipe) {
		ERROR_MSG("allocation failed");
		goto fail;
	}

	pipe->id = id;
	pipe->dev = dev;

	/* get specs from kernel space */
	pipe->specs.model    	= get_param(dev, id, ETNAVIV_PARAM_GPU_MODEL);
	pipe->specs.revision 	= get_param(dev, id, ETNAVIV_PARAM_GPU_REVISION);
	pipe->specs.features[0] = get_param(dev, id, ETNAVIV_PARAM_GPU_FEATURES_0);
	pipe->specs.features[1] = get_param(dev, id, ETNAVIV_PARAM_GPU_FEATURES_1);
	pipe->specs.features[2] = get_param(dev, id, ETNAVIV_PARAM_GPU_FEATURES_2);
	pipe->specs.features[3] = get_param(dev, id, ETNAVIV_PARAM_GPU_FEATURES_3);
	pipe->specs.stream_count = get_param(dev, id, ETNA_GPU_STREAM_COUNT);
	pipe->specs.register_max = get_param(dev, id, ETNA_GPU_REGISTER_MAX);
	pipe->specs.thread_count = get_param(dev, id, ETNA_GPU_THREAD_COUNT);
	pipe->specs.vertex_cache_size = get_param(dev, id, ETNA_GPU_VERTEX_CACHE_SIZE);
	pipe->specs.shader_core_count = get_param(dev, id, ETNA_GPU_SHADER_CORE_COUNT);
	pipe->specs.pixel_pipes = get_param(dev, id, ETNA_GPU_PIXEL_PIPES);
	pipe->specs.vertex_output_buffer_size = get_param(dev, id, ETNA_GPU_VERTEX_OUTPUT_BUFFER_SIZE);
	pipe->specs.buffer_size = get_param(dev, id, ETNA_GPU_BUFFER_SIZE);
	pipe->specs.instruction_count = get_param(dev, id, ETNA_GPU_INSTRUCTION_COUNT);
	pipe->specs.num_constants = get_param(dev, id, ETNA_GPU_NUM_CONSTANTS);


	if (!pipe->specs.model)
		goto fail;

	INFO_MSG("Pipe Info:");
	INFO_MSG(" GPU model:          0x%x (rev %x)", pipe->specs.model, pipe->specs.revision);

	return pipe;
fail:
	if (pipe)
		etna_pipe_del(pipe);
	return NULL;
}
