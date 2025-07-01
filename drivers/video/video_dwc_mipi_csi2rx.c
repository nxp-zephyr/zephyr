/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_dwc_mipi_csi2rx

#include <zephyr/drivers/video.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <fsl_clock.h>

LOG_MODULE_REGISTER(dwc_mipi_csi2rx, CONFIG_VIDEO_LOG_LEVEL);

#define DEV_CFG(_dev) ((const struct dwc_mipi_csi2rx_config *)(_dev)->config)
#define DEV_DATA(_dev) ((struct dwc_mipi_csi2rx_data *)(_dev)->data)

/*
 * DWC MIPI CSI2RX registers
 */
#define DWC_MIPI_CSI2RX_VERSION						0x0
#define DWC_MIPI_CSI2RX_N_LANES						0x4
#define DWC_MIPI_CSI2RX_N_LANES_N_LANES(x)				((x) & 0x7)
#define DWC_MIPI_CSI2RX_HOST_RESETN					0x8
#define DWC_MIPI_CSI2RX_HOST_RESETN_ENABLE				(0x1)
#define DWC_MIPI_CSI2RX_DPHY_SHUTDOWNZ					0x40
#define DWC_MIPI_CSI2RX_DPHY_SHUTDOWNZ_ENABLE				(0x1)
#define DWC_MIPI_CSI2RX_DPHY_RSTZ					0x44
#define DWC_MIPI_CSI2RX_DPHY_RSTZ_ENABLE				(0x1)
#define DWC_MIPI_CSI2RX_DPHY_RX_STATUS					0x48
#define DWC_MIPI_CSI2RX_DPHY_RX_STATUS_CLK_LANE_HS			(0x1 << 17)
#define DWC_MIPI_CSI2RX_DPHY_STOPSTATE					0x4C
#define DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0					0x50
#define DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0_TEST_CLR			(0x1)
#define DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0_TEST_CLKEN			(0x1 << 1)

#define DWC_MIPI_CSI2RX_IPI_MODE					0x80
#define DWC_MIPI_CSI2RX_IPI_MODE_CAMERA					0x0
#define DWC_MIPI_CSI2RX_IPI_MODE_CONTROLLER				0x1
#define DWC_MIPI_CSI2RX_IPI_MODE_COLOR_MODE16				(0x1 << 8)
#define DWC_MIPI_CSI2RX_IPI_MODE_COLOR_MODE48				(0x0 << 8)
#define DWC_MIPI_CSI2RX_IPI_MODE_CUT_THROUGH				(0x1 << 16)
#define DWC_MIPI_CSI2RX_IPI_MODE_ENABLE					(0x1 << 24)
#define DWC_MIPI_CSI2RX_IPI_VCID					0x84
#define DWC_MIPI_CSI2RX_IPI_VCID_VC(x)					((x)  & 0x3)
#define DWC_MIPI_CSI2RX_IPI_DATA_TYPE					0x88
#define DWC_MIPI_CSI2RX_IPI_DATA_TYPE_DT(x)				((x) & 0x3F)
#define DWC_MIPI_CSI2RX_IPI_MEM_FLUSH					0x8C
#define DWC_MIPI_CSI2RX_IPI_MEM_FLUSH_AUTO				(0x1 << 8)
#define DWC_MIPI_CSI2RX_IPI_SOFTRSTN					0xA0

#define DWC_MIPI_CSI2RX_INT_MSK_DPHY_FATAL				0xE4
#define DWC_MIPI_CSI2RX_INT_MSK_PKT_FATAL				0xF4
#define DWC_MIPI_CSI2RX_INT_MSK_DPHY					0x114
#define DWC_MIPI_CSI2RX_INT_MSK_LINE					0x134
#define DWC_MIPI_CSI2RX_INT_MSK_IPI_FATAL				0x144
#define DWC_MIPI_CSI2RX_INT_MSK_AP_GENERIC				0x184
#define DWC_MIPI_CSI2RX_INT_MSK_AP_IPI_FATAL				0x194

/* mediamix_GPR register */
#define DISP_MIX_CAMERA_MUX						0x30
#define DISP_MIX_CAMERA_MUX_DATA_TYPE(x)				(((x) & 0x3f) << 3)
#define DISP_MIX_CAMERA_MUX_GASKET_ENABLE				(1 << 16)
#define DISP_MIX_CAMERA_MUX_LEFT_JUST_MODE				(1 << 14)

#define DISP_MIX_CSI_REG						0x48
#define DISP_MIX_CSI_REG_CFGFREQRANGE(x)				((x)  & 0x3f)
#define DISP_MIX_CSI_REG_HSFREQRANGE(x)					(((x) & 0x7f) << 8)

#define dwc_mipi_csi2rx_write(__csi2rx, __r, __v)	sys_write32(__v, __csi2rx->base_regs + __r)
#define dwc_mipi_csi2rx_read(__csi2rx, __r)		sys_read32(__csi2rx->base_regs + __r)

enum csi2rx_encoding {
	CSI2H_ENC_RAW,
	CSI2H_ENC_RGB,
	CSI2H_ENC_YUV,
};

enum data_type {
	DT_YUV420_8	= 0x18,
	DT_YUV420_10	= 0x19,
	DT_YUV422_8	= 0x1E,
	DT_YUV422_10	= 0x1F,
	DT_RGB565	= 0x22,
	DT_RGB888	= 0x24,
	DT_RAW8		= 0x2A,
	DT_RAW10	= 0x2B,
	DT_RAW12	= 0x2C,
};

struct ipi_config {
	uint8_t data_type;
	uint8_t color_mode_16;
};

struct csi2rx_pix_format {
	uint32_t code;
	uint32_t fmt_reg;
	enum csi2rx_encoding encoding;
};

struct dwc_mipi_csi2rx_config {
	DEVICE_MMIO_NAMED_ROM(dwc_csi2rx_mmio);
	uintptr_t gasket_base;
	uint32_t num_lanes;
	uint32_t cfgclkfreqrange;
	uint32_t hsclkfreqrange;

	const struct device *sensor_dev;

	const struct device *cam_pix_clk_dev;
	clock_control_subsys_t cam_pix_clk_subsys;
	uint32_t cam_pix_clk_rate;
	const struct device *mipi_phy_cfg_clk_dev;
	clock_control_subsys_t mipi_phy_cfg_clk_subsys;
	uint32_t mipi_phy_cfg_clk_rate;
};

struct dwc_mipi_csi2rx_data {
	DEVICE_MMIO_NAMED_RAM(dwc_csi2rx_mmio);
	uintptr_t base_regs;
	uintptr_t gasket;
	struct ipi_config ipi_cfg;
	uint32_t num_lanes;
	uint32_t cfgclkfreqrange;
	uint32_t hsclkfreqrange;
	struct csi2rx_pix_format csi2rx_fmt;
};

static const struct csi2rx_pix_format dwc_csi2rx_formats[] = {
	{
		.code = VIDEO_PIX_FMT_YUYV,
		.fmt_reg = DT_YUV422_8,
		.encoding = CSI2H_ENC_YUV,
	}, {
		.code = VIDEO_PIX_FMT_RGB24,
		.fmt_reg = DT_RGB888,
		.encoding = CSI2H_ENC_RGB,
	}, {
		.code = VIDEO_PIX_FMT_RGB565,
		.fmt_reg = DT_RGB565,
		.encoding = CSI2H_ENC_RGB,
	}, {
		.code = VIDEO_PIX_FMT_SBGGR8,
		.fmt_reg = DT_RAW8,
		.encoding = CSI2H_ENC_RAW,
	}, {
		.code = VIDEO_PIX_FMT_SGBRG8,
		.fmt_reg = DT_RAW8,
		.encoding = CSI2H_ENC_RAW,
	}, {
		.code = VIDEO_PIX_FMT_SGRBG8,
		.fmt_reg = DT_RAW8,
		.encoding = CSI2H_ENC_RAW,
	}, {
		.code = VIDEO_PIX_FMT_SRGGB8,
		.fmt_reg = DT_RAW8,
		.encoding = CSI2H_ENC_RAW,
	}, {
		.code = VIDEO_PIX_FMT_SBGGR10,
		.fmt_reg = DT_RAW10,
		.encoding = CSI2H_ENC_RAW,
	}, {
		.code = VIDEO_PIX_FMT_SGBRG10,
		.fmt_reg = DT_RAW10,
		.encoding = CSI2H_ENC_RAW,
	}, {
		.code = VIDEO_PIX_FMT_SGRBG10,
		.fmt_reg = DT_RAW10,
		.encoding = CSI2H_ENC_RAW,
	}, {
		.code = VIDEO_PIX_FMT_SRGGB10,
		.fmt_reg = DT_RAW10,
		.encoding = CSI2H_ENC_RAW,
	}, {
		.code = VIDEO_PIX_FMT_SBGGR12,
		.fmt_reg = DT_RAW12,
		.encoding = CSI2H_ENC_RAW,
	}, {
		.code = VIDEO_PIX_FMT_SGBRG12,
		.fmt_reg = DT_RAW12,
		.encoding = CSI2H_ENC_RAW,
	}, {
		.code = VIDEO_PIX_FMT_SGRBG12,
		.fmt_reg = DT_RAW12,
		.encoding = CSI2H_ENC_RAW,
	}, {
		.code = VIDEO_PIX_FMT_SRGGB12,
		.fmt_reg = DT_RAW12,
		.encoding = CSI2H_ENC_RAW,
	}, {
		/* Sentinel */
	}
};

static const struct csi2rx_pix_format *find_csi2rx_format(uint32_t code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dwc_csi2rx_formats); i++) {
		if (code == dwc_csi2rx_formats[i].code) {
			return &dwc_csi2rx_formats[i];
		}
	}

	return NULL;
}

static void dwc_mipi_csi2rx_dphy_reset(struct dwc_mipi_csi2rx_data *csi2rx)
{
	/* Put DPHY into reset state */
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_DPHY_RSTZ, 0x0);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_DPHY_SHUTDOWNZ, 0x0);
	k_sleep(K_USEC(50));

	/* Remove the DPHY from reset state */
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_DPHY_SHUTDOWNZ, 0x1);
	k_sleep(K_USEC(50));
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_DPHY_RSTZ, 0x1);
}

static void dwc_mipi_csi2rx_dphy_test_code_reset(struct dwc_mipi_csi2rx_data *csi2rx)
{
	uint32_t val;

	/* Set PHY test codes from reset */
	val = dwc_mipi_csi2rx_read(csi2rx, DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0);
	val |= DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0_TEST_CLR;
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0, val);

	/* Release PHY test codes from reset */
	val = dwc_mipi_csi2rx_read(csi2rx, DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0);
	val &= ~DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0_TEST_CLR;
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0, val);
}

static void dwc_mipi_csi2rx_reset(struct dwc_mipi_csi2rx_data *csi2rx)
{
	uint32_t val;

	val = 0;
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_HOST_RESETN, val);

	val = DWC_MIPI_CSI2RX_HOST_RESETN_ENABLE;
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_HOST_RESETN, val);
}

static int dwc_mipi_csi2rx_dphy_init(struct dwc_mipi_csi2rx_data *csi2rx)
{
	uint32_t val;
	int timeout = 1000;

	/* Release Synopsys DPHY test codes from reset */
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_DPHY_RSTZ, 0x0);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_DPHY_SHUTDOWNZ, 0x0);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_HOST_RESETN, 0);

	/* Set TESTCLR = 1'b1 */
	val = dwc_mipi_csi2rx_read(csi2rx, DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0);
	val |= DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0_TEST_CLR;
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0, val);

	/* Wait for at least 15ns */
	k_sleep(K_USEC(1));

	/* Set TESTCLR = 1'b0 */
	val = dwc_mipi_csi2rx_read(csi2rx, DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0);
	val &= ~DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0_TEST_CLR;
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_DPHY_TEST_CTRL0, val);

	/* Configure DPHY hsfreqrange and clk, hsfreqrange operation
	 * ranging from 80 Mbps to 2.5Gbps.
	 */
	val = DISP_MIX_CSI_REG_CFGFREQRANGE(csi2rx->cfgclkfreqrange);
	val |= DISP_MIX_CSI_REG_HSFREQRANGE(csi2rx->hsclkfreqrange);
	sys_write32(val, csi2rx->gasket + DISP_MIX_CSI_REG);

	/* Config the number of active lanes for CSI host controller */
	val = DWC_MIPI_CSI2RX_N_LANES_N_LANES(csi2rx->num_lanes - 1);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_N_LANES, val);

	/* Release DPHY from reset */
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_DPHY_SHUTDOWNZ, 0x1);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_DPHY_RSTZ, 0x1);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_HOST_RESETN, 0x1);

	/* Wait timeout until clock lane at stop state */
	while (timeout) {
		val = dwc_mipi_csi2rx_read(csi2rx, DWC_MIPI_CSI2RX_DPHY_STOPSTATE);
		if (val == 0x10003) {
			break;
		}

		timeout--;
		k_sleep(K_USEC(50));
	}

	if (!timeout) {
		LOG_ERR("dwc data lane not in stop state, state=%#x", val);
		return -ETIMEDOUT;
	}

	return 0;
}

static void dwc_mipi_csi2rx_ipi_enable(struct dwc_mipi_csi2rx_data *csi2rx)
{
	uint32_t val;

	val = dwc_mipi_csi2rx_read(csi2rx, DWC_MIPI_CSI2RX_IPI_MODE);
	val |= DWC_MIPI_CSI2RX_IPI_MODE_ENABLE;
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_IPI_MODE, val);
}

static void dwc_mipi_csi2rx_ipi_disable(struct dwc_mipi_csi2rx_data *csi2rx)
{
	uint32_t val;

	val = dwc_mipi_csi2rx_read(csi2rx, DWC_MIPI_CSI2RX_IPI_MODE);
	val &= ~DWC_MIPI_CSI2RX_IPI_MODE_ENABLE;
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_IPI_MODE, val);
}

static void mediamix_camera_gasket_config(struct dwc_mipi_csi2rx_data *csi2rx)
{
	uint32_t fmt_val;
	uint32_t val;

	fmt_val = csi2rx->csi2rx_fmt.fmt_reg;

	sys_write32(0x0, csi2rx->gasket + DISP_MIX_CAMERA_MUX);

	val = sys_read32(csi2rx->gasket + DISP_MIX_CAMERA_MUX);
	val |= DISP_MIX_CAMERA_MUX_DATA_TYPE(fmt_val);
	if (csi2rx->csi2rx_fmt.encoding == CSI2H_ENC_RAW) {
		val &= ~DISP_MIX_CAMERA_MUX_LEFT_JUST_MODE;
	}
	val |= DISP_MIX_CAMERA_MUX_GASKET_ENABLE;
	sys_write32(val, csi2rx->gasket + DISP_MIX_CAMERA_MUX);
}

static int dwc_mipi_csi2rx_ipi_config(struct dwc_mipi_csi2rx_data *csi2rx)
{
	struct ipi_config *ipi_cfg = &csi2rx->ipi_cfg;
	uint32_t val;

	/* Do IPI soft reset */
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_IPI_SOFTRSTN, 0x0);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_IPI_SOFTRSTN, 0xfffff);

	/* Select data type to be processed by IPI */
	val = DWC_MIPI_CSI2RX_IPI_DATA_TYPE_DT(ipi_cfg->data_type);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_IPI_DATA_TYPE, val);

	/* Set virtual channel 0 as default */
	val = DWC_MIPI_CSI2RX_IPI_VCID_VC(0);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_IPI_VCID, val);

	/* 1. Select IPI camera timing mode
	 * 2. PPI color mode, mode16/48
	 * 3. Enable ipi_cut_through
	 */
	val = dwc_mipi_csi2rx_read(csi2rx, DWC_MIPI_CSI2RX_IPI_MODE);
	val &= ~DWC_MIPI_CSI2RX_IPI_MODE_CONTROLLER;
	val &= ~DWC_MIPI_CSI2RX_IPI_MODE_COLOR_MODE16;
	if (ipi_cfg->color_mode_16) {
		val |= DWC_MIPI_CSI2RX_IPI_MODE_COLOR_MODE16;
	}
	val |= DWC_MIPI_CSI2RX_IPI_MODE_CUT_THROUGH;
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_IPI_MODE, val);

	return 0;
}

static int dwc_mipi_csi2rx_hs_rx_start(struct dwc_mipi_csi2rx_data *csi2rx)
{
	uint32_t val;

	/* Memory is automatically flushed at each Frame Start */
	val = DWC_MIPI_CSI2RX_IPI_MEM_FLUSH_AUTO;
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_IPI_MEM_FLUSH, val);

	/* Enable IPI */
	dwc_mipi_csi2rx_ipi_enable(csi2rx);

	return 0;
}

static int dwc_mipi_csi2rx_hs_rx_stop(struct dwc_mipi_csi2rx_data *csi2rx)
{
	uint32_t val;

	dwc_mipi_csi2rx_ipi_disable(csi2rx);
	dwc_mipi_csi2rx_dphy_reset(csi2rx);

	/* Check clock lane is not in High Speed Mode */
	val = dwc_mipi_csi2rx_read(csi2rx, DWC_MIPI_CSI2RX_DPHY_RX_STATUS);
	if (val & DWC_MIPI_CSI2RX_DPHY_RX_STATUS_CLK_LANE_HS) {
		LOG_ERR("DWC MIPI CSI clock lanes still in HS mode");
		return -EINVAL;
	}

	return 0;
}

static int dwc_mipi_csi2rx_param_init(const struct device *dev)
{
	const struct dwc_mipi_csi2rx_config *config = dev->config;
	struct dwc_mipi_csi2rx_data *csi2rx = dev->data;
	struct ipi_config *ipi_cfg = &csi2rx->ipi_cfg;

	ipi_cfg->data_type = DT_YUV422_8;
	ipi_cfg->color_mode_16 = 0;

	csi2rx->num_lanes = config->num_lanes;
	csi2rx->cfgclkfreqrange = config->cfgclkfreqrange;
	csi2rx->hsclkfreqrange = config->hsclkfreqrange;

	return 0;
}

static int dwc_mipi_csi2rx_set_fmt(const struct device *dev, enum video_endpoint_id ep,
			       struct video_format *fmt)
{
	const struct dwc_mipi_csi2rx_config *config = dev->config;
	struct dwc_mipi_csi2rx_data *csi2rx = dev->data;
	struct csi2rx_pix_format const *csi2rx_fmt;
	struct ipi_config *ipi_cfg = &csi2rx->ipi_cfg;

	csi2rx_fmt = find_csi2rx_format(fmt->pixelformat);
	if (!csi2rx_fmt) {
		csi2rx_fmt = &dwc_csi2rx_formats[0];
	}

	ipi_cfg->data_type = csi2rx_fmt->fmt_reg;

	if (csi2rx_fmt->encoding == CSI2H_ENC_RAW) {
		ipi_cfg->color_mode_16 = 1;
	}

	csi2rx->csi2rx_fmt = *csi2rx_fmt;

	if (video_set_format(config->sensor_dev, ep, fmt)) {
		return -EIO;
	}

	return 0;
}

static int dwc_mipi_csi2rx_get_fmt(const struct device *dev, enum video_endpoint_id ep,
			       struct video_format *fmt)
{
	const struct dwc_mipi_csi2rx_config *config = dev->config;

	if (fmt == NULL || ep != VIDEO_EP_OUT) {
		return -EINVAL;
	}

	if (video_get_format(config->sensor_dev, ep, fmt)) {
		return -EIO;
	}

	return 0;
}

static int dwc_mipi_csi2rx_set_stream(const struct device *dev, bool enable)
{
	const struct dwc_mipi_csi2rx_config *config = dev->config;
	struct dwc_mipi_csi2rx_data *csi2rx = dev->data;

	if (enable) {
		dwc_mipi_csi2rx_ipi_config(csi2rx);
		mediamix_camera_gasket_config(csi2rx);
		dwc_mipi_csi2rx_hs_rx_start(csi2rx);

		if (video_stream_start(config->sensor_dev)) {
			return -EIO;
		}
	} else {
		if (video_stream_stop(config->sensor_dev)) {
			return -EIO;
		}

		dwc_mipi_csi2rx_hs_rx_stop(csi2rx);
	}

	return 0;
}

static int dwc_mipi_csi2rx_get_caps(const struct device *dev, enum video_endpoint_id ep,
				struct video_caps *caps)
{
	const struct dwc_mipi_csi2rx_config *config = dev->config;

	if (ep != VIDEO_EP_OUT) {
		return -EINVAL;
	}

	/* Just forward to sensor dev for now */
	return video_get_caps(config->sensor_dev, ep, caps);
}

static int dwc_mipi_csi2rx_set_frmival(const struct device *dev, enum video_endpoint_id ep,
				   struct video_frmival *frmival)
{
	const struct dwc_mipi_csi2rx_config *config = dev->config;
	int ret;

	ret = video_set_frmival(config->sensor_dev, ep, frmival);
	if (ret) {
		LOG_ERR("Cannot set sensor_dev frmival");
		return ret;
	}

	return ret;
}

static int dwc_mipi_csi2rx_get_frmival(const struct device *dev, enum video_endpoint_id ep,
				   struct video_frmival *frmival)
{
	const struct dwc_mipi_csi2rx_config *config = dev->config;

	return video_get_frmival(config->sensor_dev, ep, frmival);
}

static int dwc_mipi_csi2rx_enum_frmival(const struct device *dev, enum video_endpoint_id ep,
				    struct video_frmival_enum *fie)
{
	const struct dwc_mipi_csi2rx_config *config = dev->config;
	int ret;

	ret = video_enum_frmival(config->sensor_dev, ep, fie);
	if (ret) {
		return ret;
	}

	return 0;
}

static DEVICE_API(video, dwc_mipi_csi2rx_driver_api) = {
	.get_caps = dwc_mipi_csi2rx_get_caps,
	.get_format = dwc_mipi_csi2rx_get_fmt,
	.set_format = dwc_mipi_csi2rx_set_fmt,
	.set_stream = dwc_mipi_csi2rx_set_stream,
	.set_frmival = dwc_mipi_csi2rx_set_frmival,
	.get_frmival = dwc_mipi_csi2rx_get_frmival,
	.enum_frmival = dwc_mipi_csi2rx_enum_frmival,
};

static int dwc_mipi_csi2rx_configure_clock(const struct device *dev)
{
	const struct dwc_mipi_csi2rx_config *config = dev->config;
	uint32_t clk_freq;

	/* configure cam_pix_clk */
	if (!device_is_ready(config->cam_pix_clk_dev)) {
		LOG_ERR("cam_pix clock control device not ready");
		return -ENODEV;
	}

	clock_control_set_rate(config->cam_pix_clk_dev, config->cam_pix_clk_subsys, (clock_control_subsys_rate_t)config->cam_pix_clk_rate);
	if (clock_control_get_rate(config->cam_pix_clk_dev, config->cam_pix_clk_subsys, &clk_freq)) {
		return -EINVAL;
	}

	LOG_DBG("cam_pix clock frequency %d", clk_freq);

	/* configure mipi_phy_cfg_clk */
	if (!device_is_ready(config->mipi_phy_cfg_clk_dev)) {
		LOG_ERR("mipi_phy_cfg clock control device not ready");
		return -ENODEV;
	}

	clock_control_set_rate(config->mipi_phy_cfg_clk_dev, config->mipi_phy_cfg_clk_subsys, (clock_control_subsys_rate_t)config->mipi_phy_cfg_clk_rate);
	if (clock_control_get_rate(config->mipi_phy_cfg_clk_dev, config->mipi_phy_cfg_clk_subsys, &clk_freq)) {
		return -EINVAL;
	}

	LOG_DBG("mipi_phy_cfg clock frequency %d", clk_freq);

	return 0;
}

static int dwc_mipi_csi2rx_init(const struct device *dev)
{
	const struct dwc_mipi_csi2rx_config *config = dev->config;
	struct dwc_mipi_csi2rx_data *csi2rx = dev->data;
	int ret;

	DEVICE_MMIO_NAMED_MAP(dev, dwc_csi2rx_mmio, K_MEM_CACHE_NONE | K_MEM_DIRECT_MAP);

	/* Check if there is any sensor device */
	if (!device_is_ready(config->sensor_dev)) {
		return -ENODEV;
	}

	ret = dwc_mipi_csi2rx_configure_clock(dev);
	if (ret) {
		LOG_ERR("%s configure clock failed", dev->name);
		return ret;
	}

	csi2rx->base_regs = DEVICE_MMIO_NAMED_GET(dev, dwc_csi2rx_mmio);
	csi2rx->gasket = config->gasket_base;

	ret = dwc_mipi_csi2rx_param_init(dev);
	if (ret < 0) {
		return ret;
	}

	dwc_mipi_csi2rx_reset(csi2rx);
	dwc_mipi_csi2rx_dphy_reset(csi2rx);
	dwc_mipi_csi2rx_dphy_test_code_reset(csi2rx);
	dwc_mipi_csi2rx_dphy_init(csi2rx);

	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_INT_MSK_DPHY_FATAL, 0xffff);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_INT_MSK_PKT_FATAL,  0xffff);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_INT_MSK_IPI_FATAL,  0xffff);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_INT_MSK_AP_GENERIC, 0xffff);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_INT_MSK_DPHY,  0xffff);
	dwc_mipi_csi2rx_write(csi2rx, DWC_MIPI_CSI2RX_INT_MSK_LINE,  0xffff);

	return 0;
}

#define DWC_MIPI_CSI2RX_INIT(n)                                                                              \
	static struct dwc_mipi_csi2rx_data dwc_mipi_csi2rx_data_##n;                                         \
                                                                                                             \
	static const struct dwc_mipi_csi2rx_config dwc_mipi_csi2rx_config_##n = {                            \
		DEVICE_MMIO_NAMED_ROM_INIT(dwc_csi2rx_mmio, DT_DRV_INST(n)),                                 \
		.gasket_base = DT_REG_ADDR(DT_INST_PHANDLE(n, gasket)),                                      \
		.sensor_dev = DEVICE_DT_GET(DT_NODE_REMOTE_DEVICE(DT_INST_ENDPOINT_BY_ID(n, 1, 0))),         \
		.num_lanes = DT_INST_PROP(n, data_lanes),                                                    \
		.cfgclkfreqrange = DT_INST_PROP(n, cfg_clk_range),                                           \
		.hsclkfreqrange = DT_INST_PROP(n, hs_clk_range),                                             \
		.cam_pix_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(n, 0)),                          \
		.cam_pix_clk_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(n, 0, name),        \
		.cam_pix_clk_rate = DT_INST_PROP(n, cam_pix_clk_rate),                                       \
		.mipi_phy_cfg_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(n, 1)),                     \
		.mipi_phy_cfg_clk_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(n, 1, name),   \
		.mipi_phy_cfg_clk_rate = DT_INST_PROP(n, mipi_phy_cfg_clk_rate),                             \
	};                                                                                                   \
                                                                                                             \
	DEVICE_DT_INST_DEFINE(n, &dwc_mipi_csi2rx_init, NULL, &dwc_mipi_csi2rx_data_##n,                     \
			&dwc_mipi_csi2rx_config_##n, POST_KERNEL, CONFIG_VIDEO_INIT_PRIORITY,                \
			&dwc_mipi_csi2rx_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DWC_MIPI_CSI2RX_INIT)
