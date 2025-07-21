/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_isi

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/clock_control.h>

#include <zephyr/cache.h>

#include <fsl_isi.h>
#ifdef CONFIG_HAS_MCUX_CACHE
#include <fsl_cache.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(isi, CONFIG_VIDEO_LOG_LEVEL);

#define DEV_CFG(_dev)  ((const struct video_mcux_isi_config *)(_dev)->config)
#define DEV_DATA(_dev) ((struct video_mcux_isi_data *)(_dev)->data)

#define ISI_MAX_ACTIVE_BUF 2U

enum isi_encoding {
	ISI_ENC_RAW,
	ISI_ENC_RGB,
	ISI_ENC_YUV,
};

struct video_mcux_isi_config {
	DEVICE_MMIO_NAMED_ROM(isi_mmio);
	const struct device *source_dev;
	const struct device *media_axi_clk_dev;
	clock_control_subsys_t media_axi_clk_subsys;
	uint32_t media_axi_clk_rate;
	const struct device *media_apb_clk_dev;
	clock_control_subsys_t media_apb_clk_subsys;
	uint32_t media_apb_clk_rate;
};

struct video_mcux_isi_data {
	DEVICE_MMIO_NAMED_RAM(isi_mmio);
	const struct device *dev;
	isi_config_t isi_config;
	struct video_format src_fmt;
	struct video_format dst_fmt;
	isi_output_format_t isi_format;
	enum isi_encoding in_encoding;
	enum isi_encoding out_encoding;

	struct k_fifo fifo_in;
	struct k_fifo fifo_out;
	volatile bool is_transfer_started;
	volatile uint8_t buffer_index;
	uint32_t drop_frame;
	uint32_t active_buffer[ISI_MAX_ACTIVE_BUF];
	uint8_t active_buf_cnt;
	struct video_buffer *active_vbuf[ISI_MAX_ACTIVE_BUF];
};

static ISI_Type *get_base(const struct device *dev)
{
	return (ISI_Type *)DEVICE_MMIO_NAMED_GET(dev, isi_mmio);
}

/* Map for the fourcc pixelformat to ISI format. */
struct isi_output_format {
	uint32_t pixelformat;
	isi_output_format_t isi_format;
	enum isi_encoding encoding;
};

struct isi_input_format {
	uint32_t pixelformat;
	enum isi_encoding encoding;
};

static const struct isi_output_format isi_output_formats[] = {
	{
		.pixelformat = VIDEO_PIX_FMT_RGB565,
		.isi_format = kISI_OutputRGB565,
		.encoding = ISI_ENC_RGB,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_ABGR32,
		.isi_format = kISI_OutputARGB8888,
		.encoding = ISI_ENC_RGB,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_YUYV,
		.isi_format = kISI_OutputYUV422_1P8P,
		.encoding = ISI_ENC_YUV,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_SRGGB8,
		.isi_format = kISI_OutputRaw8,
		.encoding = ISI_ENC_RAW,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_SRGGB10,
		.isi_format = kISI_OutputRaw16P,
		.encoding = ISI_ENC_RAW,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_SRGGB12,
		.isi_format = kISI_OutputRaw16P,
		.encoding = ISI_ENC_RAW,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_GREY,
		.isi_format = kISI_OutputRaw8,
		.encoding = ISI_ENC_RAW,
	},
};

static const struct isi_input_format isi_input_formats[] = {
	{
		.pixelformat = VIDEO_PIX_FMT_UYVY,
		.encoding = ISI_ENC_YUV,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_SRGGB8,
		.encoding = ISI_ENC_RAW,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_SRGGB10,
		.encoding = ISI_ENC_RAW,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_SRGGB12,
		.encoding = ISI_ENC_RAW,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_GREY,
		.encoding = ISI_ENC_RAW,
	},
};

#define ISI_VIDEO_FORMAT_CAP(width, height, format)                                                \
	{.pixelformat = (format),                                                                  \
	 .width_min = 1,                                                                           \
	 .width_max = (width),                                                                     \
	 .height_min = 1,                                                                          \
	 .height_max = (height),                                                                   \
	 .width_step = 1,                                                                          \
	 .height_step = 1}

/* Limit maximum width to 2K as CSC/resize may be enabled for yuv/rgb formats */
static const struct video_format_cap isi_fmts[] = {
	ISI_VIDEO_FORMAT_CAP(2048, 8191, VIDEO_PIX_FMT_RGB565),
	ISI_VIDEO_FORMAT_CAP(2048, 8191, VIDEO_PIX_FMT_ABGR32),
	ISI_VIDEO_FORMAT_CAP(2048, 8191, VIDEO_PIX_FMT_YUYV),
	ISI_VIDEO_FORMAT_CAP(8191, 8191, VIDEO_PIX_FMT_SRGGB8),
	ISI_VIDEO_FORMAT_CAP(8191, 8191, VIDEO_PIX_FMT_SRGGB10),
	ISI_VIDEO_FORMAT_CAP(8191, 8191, VIDEO_PIX_FMT_SRGGB12),
	ISI_VIDEO_FORMAT_CAP(8191, 8191, VIDEO_PIX_FMT_GREY),
	{0},
};

static const isi_csc_config_t csc_yuv2rgb = {
	.mode = kISI_CscYCbCr2RGB,
	.A1 = 1.164f,
	.A2 = 0.0f,
	.A3 = 1.596f,
	.B1 = 1.164f,
	.B2 = -0.392f,
	.B3 = -0.813f,
	.C1 = 1.164f,
	.C2 = 2.017f,
	.C3 = 0.0f,
	.D1 = -16,
	.D2 = -128,
	.D3 = -128,
};

static const isi_csc_config_t csc_rgb2yuv = {
	.mode = kISI_CscRGB2YCbCr,
	.A1 = 0.257f,
	.A2 = 0.504f,
	.A3 = 0.098f,
	.B1 = -0.148f,
	.B2 = -0.291f,
	.B3 = 0.439f,
	.C1 = 0.439f,
	.C2 = -0.368f,
	.C3 = -0.071f,
	.D1 = 16,
	.D2 = 128,
	.D3 = 128,
};

#ifdef DEBUG
static void dump_isi_regs(ISI_Type *base)
{
	LOG_DBG("CHNL_CTRL[0x0]: 0x%08x", base->CHNL_CTRL);
	LOG_DBG("CHNL_IMG_CTRL[0x4]: 0x%08x", base->CHNL_IMG_CTRL);
	LOG_DBG("CHNL_OUT_BUF_CTRL[0x8]: 0x%08x", base->CHNL_OUT_BUF_CTRL);
	LOG_DBG("CHNL_IMG_CFG[0x10]: 0x%08x", base->CHNL_IMG_CFG);
	LOG_DBG("CHNL_IER[0x10]: 0x%08x", base->CHNL_IER);
	LOG_DBG("CHNL_SCALE_FACTOR[0x18]: 0x%08x", base->CHNL_SCALE_FACTOR);
	LOG_DBG("CHNL_SCALE_OFFSET[0x1C]: 0x%08x", base->CHNL_SCALE_OFFSET);
	LOG_DBG("CHNL_OUT_BUF1_ADDR_Y[0x70]: 0x%08x", base->CHNL_OUT_BUF1_ADDR_Y);
	LOG_DBG("CHNL_OUT_BUF_PITCH[0x7C]: 0x%08x", base->CHNL_OUT_BUF_PITCH);
	LOG_DBG("CHNL_OUT_BUF2_ADDR_Y[0x8C]: 0x%08x", base->CHNL_OUT_BUF2_ADDR_Y);
	LOG_DBG("CHNL_SCL_IMG_CFG[0x98]: 0x%08x", base->CHNL_SCL_IMG_CFG);
}
#else
static void dump_isi_regs(ISI_Type *base)
{
}
#endif

static int find_isi_output_format(uint32_t pixelformat)
{
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(isi_output_formats); i++) {
		if (isi_output_formats[i].pixelformat == pixelformat) {
			return i;
		}
	}

	LOG_ERR("output pixelformat %c%c%c%c not supported", (char)pixelformat,
		(char)(pixelformat >> 8), (char)(pixelformat >> 16), (char)(pixelformat >> 24));
	return -1;
}

static int find_isi_input_format(uint32_t pixelformat)
{
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(isi_input_formats); i++) {
		if (isi_input_formats[i].pixelformat == pixelformat) {
			return i;
		}
	}

	LOG_ERR("input pixelformat %c%c%c%c not supported", (char)pixelformat,
		(char)(pixelformat >> 8), (char)(pixelformat >> 16), (char)(pixelformat >> 24));
	return -1;
}

static void __frame_done_handler(const struct device *dev)
{
	struct video_mcux_isi_data *data = dev->data;
	ISI_Type *base = get_base(dev);
	struct video_buffer *vbuf = NULL;
	uint32_t buffer_addr;
	uint32_t intStatus;

	intStatus = ISI_GetInterruptStatus(base);
	ISI_ClearInterruptStatus(base, intStatus);

	if ((uint32_t)kISI_FrameReceivedInterrupt !=
	    ((uint32_t)kISI_FrameReceivedInterrupt & intStatus)) {
		return;
	}

	buffer_addr = data->active_buffer[data->buffer_index];

	if (buffer_addr != data->drop_frame) {
		vbuf = data->active_vbuf[data->buffer_index];
		vbuf->timestamp = k_uptime_get_32();
		vbuf->bytesused = data->isi_config.outputLinePitchBytes * data->dst_fmt.height;
#ifdef CONFIG_HAS_MCUX_CACHE
		DCACHE_InvalidateByRange(buffer_addr, vbuf->bytesused);
#endif
		k_fifo_put(&data->fifo_out, vbuf);
	}

	vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT);
	/*
	 * If no available input buffer, then the frame will be dropped.
	 */
	if (vbuf == NULL) {
		buffer_addr = data->drop_frame;
		LOG_ERR("No available input buffer, drop frame.");
	} else {
		buffer_addr = POINTER_TO_UINT(vbuf->buffer);
		data->active_vbuf[data->buffer_index] = vbuf;
	}

	data->active_buffer[data->buffer_index] = buffer_addr;
	ISI_SetOutputBufferAddr(base, data->buffer_index, buffer_addr, 0, 0);
	data->buffer_index ^= 1U;
}

/*
 * VIDEO_EP_OUT to set video format for isi output
 * VIDEO_EP_IN to set video format for isi input
 * VIDEO_EP_ALL to set video format for both isi input and output
 *
 * For YUV/RGB cameras, the output format can be different from the input.
 * For RAW cameras, the output format must be the same as the input.
 */
static int video_mcux_isi_set_fmt(const struct device *dev, enum video_endpoint_id ep,
				  struct video_format *fmt)
{
	const struct video_mcux_isi_config *config = dev->config;
	struct video_mcux_isi_data *data = dev->data;
	int i;

	switch (ep) {
	case VIDEO_EP_OUT:
		i = find_isi_output_format(fmt->pixelformat);
		if (i < 0) {
			return -ENOTSUP;
		}
		memcpy(&data->dst_fmt, fmt, sizeof(*fmt));
		data->isi_format = isi_output_formats[i].isi_format;
		data->out_encoding = isi_output_formats[i].encoding;
		break;
	case VIDEO_EP_IN:
		i = find_isi_input_format(fmt->pixelformat);
		if (i < 0) {
			return -ENOTSUP;
		}
		ep = VIDEO_EP_OUT;
		if (config->source_dev && video_set_format(config->source_dev, ep, fmt)) {
			return -EIO;
		}
		memcpy(&data->src_fmt, fmt, sizeof(*fmt));
		data->in_encoding = isi_input_formats[i].encoding;
		break;
	case VIDEO_EP_ALL:
		i = find_isi_output_format(fmt->pixelformat);
		if (i < 0) {
			return -ENOTSUP;
		}
		memcpy(&data->dst_fmt, fmt, sizeof(*fmt));
		data->isi_format = isi_output_formats[i].isi_format;
		data->out_encoding = isi_output_formats[i].encoding;
		i = find_isi_input_format(fmt->pixelformat);
		if (i < 0) {
			return -ENOTSUP;
		}
		ep = VIDEO_EP_OUT;
		if (config->source_dev && video_set_format(config->source_dev, ep, fmt)) {
			return -EIO;
		}
		memcpy(&data->src_fmt, fmt, sizeof(*fmt));
		data->in_encoding = isi_input_formats[i].encoding;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * VIDEO_EP_OUT to get video format for isi output
 * VIDEO_EP_IN to get video format for isi input
 */
static int video_mcux_isi_get_fmt(const struct device *dev, enum video_endpoint_id ep,
				  struct video_format *fmt)
{
	const struct video_mcux_isi_config *config = dev->config;
	struct video_mcux_isi_data *data = dev->data;

	if (data->src_fmt.pixelformat == 0 || data->dst_fmt.pixelformat == 0) {
		LOG_ERR("Video pipeline format not configured");
		return -EPIPE;
	}

	switch (ep) {
	case VIDEO_EP_OUT:
		memcpy(fmt, &data->dst_fmt, sizeof(*fmt));
		break;
	case VIDEO_EP_IN:
		ep = VIDEO_EP_OUT;
		if (config->source_dev && video_get_format(config->source_dev, ep, fmt)) {
			return -EIO;
		}
		if (fmt->pixelformat != data->src_fmt.pixelformat) {
			return -EPIPE;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int isi_channel_config(const struct device *dev)
{
	struct video_mcux_isi_data *data = dev->data;
	ISI_Type *base = get_base(dev);

	if (data->src_fmt.pixelformat == 0 || data->dst_fmt.pixelformat == 0) {
		LOG_ERR("Video pipeline format not configured");
		return -EPIPE;
	}

	LOG_DBG("isi input format: %c%c%c%c, wxh: %dx%d", (char)data->src_fmt.pixelformat,
		(char)(data->src_fmt.pixelformat >> 8), (char)(data->src_fmt.pixelformat >> 16),
		(char)(data->src_fmt.pixelformat >> 24), data->src_fmt.width, data->src_fmt.height);

	LOG_DBG("isi output format: %c%c%c%c, wxh: %dx%d", (char)data->dst_fmt.pixelformat,
		(char)(data->dst_fmt.pixelformat >> 8), (char)(data->dst_fmt.pixelformat >> 16),
		(char)(data->dst_fmt.pixelformat >> 24), data->dst_fmt.width, data->dst_fmt.height);

	/* For RAW formats, the input and output format/size must match */
	if (data->in_encoding == ISI_ENC_RAW || data->out_encoding == ISI_ENC_RAW) {
		if (data->dst_fmt.pixelformat != data->src_fmt.pixelformat ||
		    data->dst_fmt.width != data->src_fmt.width ||
		    data->dst_fmt.height != data->src_fmt.height) {
			LOG_ERR("Video pipeline misconfiguration for RAW formats");
			return -EPIPE;
		}
	}

	if (data->dst_fmt.width > data->src_fmt.width ||
	    data->dst_fmt.height > data->src_fmt.height) {
		LOG_ERR("Not support upscale");
		return -EPIPE;
	}

	data->isi_config.inputWidth = data->src_fmt.width;
	data->isi_config.inputHeight = data->src_fmt.height;
	data->isi_config.outputFormat = data->isi_format;
	data->isi_config.outputLinePitchBytes = data->dst_fmt.width *
						video_bits_per_pixel(data->dst_fmt.pixelformat) /
						BITS_PER_BYTE;

	/* bypass isi channel for RAW input */
	if (data->in_encoding == ISI_ENC_RAW) {
		data->isi_config.isChannelBypassed = true;
	} else {
		data->isi_config.isChannelBypassed = false;
	}

	ISI_Init(base);
	ISI_SetConfig(base, &data->isi_config);

	/* downscale */
	ISI_SetScalerConfig(base, data->src_fmt.width, data->src_fmt.height, data->dst_fmt.width,
			    data->dst_fmt.height);

	/* color space conversion*/
	if (data->in_encoding == ISI_ENC_RGB && data->out_encoding == ISI_ENC_YUV) {
		ISI_SetColorSpaceConversionConfig(base, &csc_rgb2yuv);
		ISI_EnableColorSpaceConversion(base, true);
	} else if (data->in_encoding == ISI_ENC_YUV && data->out_encoding == ISI_ENC_RGB) {
		ISI_SetColorSpaceConversionConfig(base, &csc_yuv2rgb);
		ISI_EnableColorSpaceConversion(base, true);
	} else {
		ISI_EnableColorSpaceConversion(base, false);
	}

	return 0;
}

static int video_mcux_isi_stream_start(const struct device *dev)
{
	const struct video_mcux_isi_config *config = dev->config;
	struct video_mcux_isi_data *data = dev->data;
	ISI_Type *base = get_base(dev);
	uint8_t i;
	uint32_t buffer_addr;
	struct video_buffer *vbuf;
	int ret;

	ret = isi_channel_config(dev);
	if (ret < 0) {
		return ret;
	}

	if (data->active_buf_cnt != 2) {
		LOG_ERR("ISI requires at least two active frame buffers");
		return -EIO;
	}

	/* Only support single planar for now */
	for (i = 0; i < ISI_MAX_ACTIVE_BUF; i++) {
		buffer_addr = data->active_buffer[i];
		ISI_SetOutputBufferAddr(base, i, buffer_addr, 0, 0);
		vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT);
	}

	data->buffer_index = 0;
	data->is_transfer_started = true;

	ISI_ClearInterruptStatus(base, (uint32_t)kISI_FrameReceivedInterrupt);
	ISI_EnableInterrupts(base, (uint32_t)kISI_FrameReceivedInterrupt);
	ISI_Start(base);
	dump_isi_regs(base);

	if (config->source_dev && video_stream_start(config->source_dev)) {
		LOG_ERR("isi source dev start stream failed");
		return -EIO;
	}

	return 0;
}

static int video_mcux_isi_stream_stop(const struct device *dev)
{
	const struct video_mcux_isi_config *config = dev->config;
	struct video_mcux_isi_data *data = dev->data;
	ISI_Type *base = get_base(dev);

	if (config->source_dev && video_stream_stop(config->source_dev)) {
		LOG_ERR("isi source dev stop stream failed");
		return -EIO;
	}

	ISI_Stop(base);
	ISI_DisableInterrupts(base, (uint32_t)kISI_FrameReceivedInterrupt);
	ISI_ClearInterruptStatus(base, (uint32_t)kISI_FrameReceivedInterrupt);

	data->is_transfer_started = false;
	data->active_buf_cnt = 0;

	return 0;
}

static int video_mcux_isi_set_stream(const struct device *dev, bool enable)
{
	if (enable) {
		return video_mcux_isi_stream_start(dev);
	} else {
		return video_mcux_isi_stream_stop(dev);
	}
}

static int video_mcux_isi_enqueue(const struct device *dev, enum video_endpoint_id ep,
				  struct video_buffer *vbuf)
{
	struct video_mcux_isi_data *data = dev->data;
	ISI_Type *base = get_base(dev);
	uint32_t interrupts;

	vbuf->bytesused = data->isi_config.outputLinePitchBytes * data->dst_fmt.height;

	if (!(data->is_transfer_started)) {
		if (data->drop_frame == 0U) {
			data->drop_frame = POINTER_TO_UINT(vbuf->buffer);
		} else {
			if (data->active_buf_cnt < ISI_MAX_ACTIVE_BUF) {
				data->active_buffer[data->active_buf_cnt] =
					POINTER_TO_UINT(vbuf->buffer);
				data->active_vbuf[data->active_buf_cnt] = vbuf;
				data->active_buf_cnt += 1;
			}
			k_fifo_put(&data->fifo_in, vbuf);
		}
	} else {
		/* Disable the interrupt to protect the index information */
		interrupts = ISI_DisableInterrupts(base, (uint32_t)kISI_FrameReceivedInterrupt);

		k_fifo_put(&data->fifo_in, vbuf);

		if (0UL != (interrupts & (uint32_t)kISI_FrameReceivedInterrupt)) {
			ISI_EnableInterrupts(base, (uint32_t)kISI_FrameReceivedInterrupt);
		}
	}

	return 0;
}

static int video_mcux_isi_dequeue(const struct device *dev, enum video_endpoint_id ep,
				  struct video_buffer **vbuf, k_timeout_t timeout)
{
	struct video_mcux_isi_data *data = dev->data;

	*vbuf = k_fifo_get(&data->fifo_out, timeout);
	if (*vbuf == NULL) {
		return -EAGAIN;
	}

	return 0;
}

/*
 * VIDEO_EP_OUT to get video format capabilities for isi output
 * VIDEO_EP_IN to get video format capabilities for isi input
 */
static int video_mcux_isi_get_caps(const struct device *dev, enum video_endpoint_id ep,
				   struct video_caps *caps)
{
	const struct video_mcux_isi_config *config = dev->config;

	switch (ep) {
	case VIDEO_EP_OUT:
		caps->format_caps = isi_fmts;
		/* ISI request at least 3 buffers before starting */
		caps->min_vbuf_count = 3;
		caps->min_line_count = caps->max_line_count = LINE_COUNT_HEIGHT;
		break;
	case VIDEO_EP_IN:
		ep = VIDEO_EP_OUT;
		if (config->source_dev && video_get_caps(config->source_dev, ep, caps)) {
			return -EIO;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int video_mcux_isi_set_frmival(const struct device *dev, enum video_endpoint_id ep,
				      struct video_frmival *frmival)
{
	const struct video_mcux_isi_config *config = dev->config;

	/* Just forward to source dev */
	if (config->source_dev && video_set_frmival(config->source_dev, ep, frmival)) {
		return -EIO;
	}

	return 0;
}

static int video_mcux_isi_get_frmival(const struct device *dev, enum video_endpoint_id ep,
				      struct video_frmival *frmival)
{
	const struct video_mcux_isi_config *config = dev->config;

	/* Just forward to source dev */
	if (config->source_dev && video_get_frmival(config->source_dev, ep, frmival)) {
		return -EIO;
	}

	return 0;
}

static int video_mcux_isi_enum_frmival(const struct device *dev, enum video_endpoint_id ep,
				       struct video_frmival_enum *fie)
{
	const struct video_mcux_isi_config *config = dev->config;

	/* Just forward to source dev */
	if (config->source_dev && video_enum_frmival(config->source_dev, ep, fie)) {
		return -EIO;
	}

	return 0;
}

static DEVICE_API(video, video_mcux_isi_driver_api) = {
	.set_format = video_mcux_isi_set_fmt,
	.get_format = video_mcux_isi_get_fmt,
	.set_stream = video_mcux_isi_set_stream,
	.enqueue = video_mcux_isi_enqueue,
	.dequeue = video_mcux_isi_dequeue,
	.get_caps = video_mcux_isi_get_caps,
	.set_frmival = video_mcux_isi_set_frmival,
	.get_frmival = video_mcux_isi_get_frmival,
	.enum_frmival = video_mcux_isi_enum_frmival,
};

#if 1 /* Unique Instance */
#define SOURCE_DEV(n) DEVICE_DT_GET(DT_NODE_REMOTE_DEVICE(DT_INST_ENDPOINT_BY_ID(n, 0, 0)))

static const struct video_mcux_isi_config video_mcux_isi_config_0 = {
	DEVICE_MMIO_NAMED_ROM_INIT(isi_mmio, DT_DRV_INST(0)),
	.source_dev = SOURCE_DEV(0),
	.media_axi_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(0, 0)),
	.media_axi_clk_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(0, 0, name),
	.media_axi_clk_rate = DT_INST_PROP(0, media_axi_clk_rate),
	.media_apb_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(0, 1)),
	.media_apb_clk_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(0, 1, name),
	.media_apb_clk_rate = DT_INST_PROP(0, media_apb_clk_rate),
};

static struct video_mcux_isi_data video_mcux_isi_data_0;

static void video_mcux_isi_isr(const struct device *dev)
{
	__frame_done_handler(dev);
}

static int video_mcux_isi_configure_clock(const struct device *dev)
{
	const struct video_mcux_isi_config *config = dev->config;
	uint32_t clk_freq;

	/* configure media_axi_clk */
	if (!device_is_ready(config->media_axi_clk_dev)) {
		LOG_ERR("media_axi clock control device not ready");
		return -ENODEV;
	}

	clock_control_set_rate(config->media_axi_clk_dev, config->media_axi_clk_subsys,
			       (clock_control_subsys_rate_t)config->media_axi_clk_rate);
	if (clock_control_get_rate(config->media_axi_clk_dev, config->media_axi_clk_subsys,
				   &clk_freq)) {
		return -EINVAL;
	}

	LOG_DBG("media_axi clock frequency %d", clk_freq);

	/* configure media_apb_clk */
	if (!device_is_ready(config->media_apb_clk_dev)) {
		LOG_ERR("media_apb clock control device not ready");
		return -ENODEV;
	}

	clock_control_set_rate(config->media_apb_clk_dev, config->media_apb_clk_subsys,
			       (clock_control_subsys_rate_t)config->media_apb_clk_rate);
	if (clock_control_get_rate(config->media_apb_clk_dev, config->media_apb_clk_subsys,
				   &clk_freq)) {
		return -EINVAL;
	}

	LOG_DBG("media_apb clock frequency %d", clk_freq);

	return 0;
}

static int video_mcux_isi_init_0(const struct device *dev)
{
	struct video_mcux_isi_data *data = dev->data;
	const struct video_mcux_isi_config *config = dev->config;
	int ret;

	DEVICE_MMIO_NAMED_MAP(dev, isi_mmio, K_MEM_CACHE_NONE | K_MEM_DIRECT_MAP);

	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), video_mcux_isi_isr,
		    DEVICE_DT_INST_GET(0), 0);

	irq_enable(DT_INST_IRQN(0));

	data->dev = dev;

	/* check if there is any input device */
	if (!device_is_ready(config->source_dev)) {
		LOG_ERR("%s init failed as source device not ready", dev->name);
		return -ENODEV;
	}

	ret = video_mcux_isi_configure_clock(dev);
	if (ret < 0) {
		LOG_ERR("%s configure clock failed", dev->name);
		return ret;
	}

	k_fifo_init(&data->fifo_in);
	k_fifo_init(&data->fifo_out);

	ISI_GetDefaultConfig(&data->isi_config);
	data->isi_config.inputWidth = 1280;
	data->isi_config.inputHeight = 800;
	data->isi_config.outputFormat = kISI_OutputYUV422_1P8P;

	data->src_fmt.width = data->isi_config.inputWidth;
	data->src_fmt.height = data->isi_config.inputWidth;
	data->src_fmt.pixelformat = VIDEO_PIX_FMT_UYVY;
	data->src_fmt.pitch = data->src_fmt.width *
			      video_bits_per_pixel(data->src_fmt.pixelformat) /
			      BITS_PER_BYTE;

	data->dst_fmt.width = data->isi_config.inputWidth;
	data->dst_fmt.height = data->isi_config.inputHeight;
	data->dst_fmt.pixelformat = VIDEO_PIX_FMT_YUYV;
	data->dst_fmt.pitch = data->dst_fmt.width *
			      video_bits_per_pixel(data->dst_fmt.pixelformat) /
			      BITS_PER_BYTE;

	data->is_transfer_started = false;
	data->buffer_index = 0;
	data->active_buf_cnt = 0;

	LOG_INF("%s init succeeded, source from %s", dev->name, config->source_dev->name);

	return 0;
}

DEVICE_DT_INST_DEFINE(0, &video_mcux_isi_init_0, NULL, &video_mcux_isi_data_0,
		      &video_mcux_isi_config_0, POST_KERNEL, CONFIG_VIDEO_MCUX_ISI_INIT_PRIORITY,
		      &video_mcux_isi_driver_api);
#endif
