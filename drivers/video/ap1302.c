/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT onnn_ap1302

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>

LOG_MODULE_REGISTER(video_ap1302, CONFIG_VIDEO_LOG_LEVEL);

#if !DT_HAS_CHOSEN(zephyr_sensor_fw)
#error "AP1302 driver requires definition of reserved memory for loading firmware"
#endif

/* Constants derived from device tree */
#define FW_NODE		DT_CHOSEN(zephyr_sensor_fw)
#define FW_START_ADDR	DT_REG_ADDR(FW_NODE)
#define FW_SIZE		DT_REG_SIZE(FW_NODE)

#define AP1302_FW_WINDOW_OFFSET			0x8000
#define AP1302_FW_WINDOW_SIZE			0x2000

#define AP1302_REG_16BIT(n)			((2 << 24) | (n))
#define AP1302_REG_32BIT(n)			((4 << 24) | (n))
#define AP1302_REG_SIZE(n)			((n) >> 24)
#define AP1302_REG_ADDR(n)			((n) & 0x0000ffff)

#define AP1302_CHIP_VERSION			AP1302_REG_16BIT(0x0000)
#define AP1302_CHIP_ID				0x0265

#define AP1302_PREVIEW_WIDTH			AP1302_REG_16BIT(0x2000)
#define AP1302_PREVIEW_HEIGHT			AP1302_REG_16BIT(0x2002)
#define AP1302_PREVIEW_MAX_FPS			AP1302_REG_16BIT(0x2020)

#define AP1302_AF_CTRL				AP1302_REG_16BIT(0x5058)

#define AP1302_BOOTDATA_STAGE			AP1302_REG_16BIT(0x6002)
#define AP1302_BOOTDATA_CHECKSUM		AP1302_REG_16BIT(0x6134)

#define AP1302_SYS_START			AP1302_REG_16BIT(0x601a)
#define AP1302_SYS_START_PLL_LOCK		BIT(15)
#define AP1302_SYS_START_STALL_STATUS	BIT(9)
#define AP1302_SYS_START_STALL_EN		BIT(8)
#define AP1302_SYS_START_STALL_MODE_DISABLED	(1U << 6)

#define AP1302_SIPS_CRC				AP1302_REG_16BIT(0xf052)

#define ABS(a, b) (a > b ? a - b : b - a)

/* Must be kept in ascending order */
enum ap1302_frame_rate {
	AP1302_15_FPS = 15,
	AP1302_30_FPS = 30,
	AP1302_45_FPS = 45,
	AP1302_60_FPS = 60,
};

struct ap1302_config {
	struct i2c_dt_spec i2c;
	const struct device **regulator_array;
	uint32_t regulator_num;
	const struct gpio_dt_spec isp_en_gpio;
	const struct gpio_dt_spec reset_gpio;
};

struct ap1302_mode_config {
	uint16_t width;
	uint16_t height;
	uint16_t max_frmrate;
	uint16_t def_frmrate;
};

struct ap1302_data {
	struct video_format fmt;
	uint16_t cur_frmrate;
	const struct ap1302_mode_config *cur_mode;
};

struct ap1302_firmware {
	uint32_t crc;
	uint32_t checksum;
	uint32_t pll_init_size;
	uint32_t total_size;
};

static const struct ap1302_mode_config ap1302_modes[] = {
	{
		.width = 640,
		.height = 480,
		.max_frmrate = AP1302_60_FPS,
		.def_frmrate = AP1302_60_FPS,
	},
	{
		.width = 1024,
		.height = 600,
		.max_frmrate = AP1302_60_FPS,
		.def_frmrate = AP1302_60_FPS,
	},
	{
		.width = 1280,
		.height = 720,
		.max_frmrate = AP1302_60_FPS,
		.def_frmrate = AP1302_60_FPS,
	},
	{
		.width = 1280,
		.height = 800,
		.max_frmrate = AP1302_60_FPS,
		.def_frmrate = AP1302_60_FPS,
	},
	{
		.width = 1920,
		.height = 1080,
		.max_frmrate = AP1302_45_FPS,
		.def_frmrate = AP1302_45_FPS,
	}};

static const int ap1302_frame_rates[] = {AP1302_15_FPS, AP1302_30_FPS, AP1302_45_FPS, AP1302_60_FPS};

#define AP1302_VIDEO_FORMAT_CAP(width, height, format)                                     \
	{                                                                                      \
		.pixelformat = (format), .width_min = (width), .width_max = (width),               \
		.height_min = (height), .height_max = (height), .width_step = 0, .height_step = 0  \
	}

static const struct video_format_cap ap1302_fmts[] = {
	AP1302_VIDEO_FORMAT_CAP(640, 480, VIDEO_PIX_FMT_UYVY),
	AP1302_VIDEO_FORMAT_CAP(1024, 600, VIDEO_PIX_FMT_UYVY),
	AP1302_VIDEO_FORMAT_CAP(1280, 720, VIDEO_PIX_FMT_UYVY),
	AP1302_VIDEO_FORMAT_CAP(1280, 800, VIDEO_PIX_FMT_UYVY),
	AP1302_VIDEO_FORMAT_CAP(1920, 1080, VIDEO_PIX_FMT_UYVY),
	{0}};

static inline int i2c_burst_read16_dt(const struct i2c_dt_spec *spec, uint16_t start_addr,
				      uint8_t *buf, uint32_t num_bytes)
{
	uint8_t addr_buffer[2];

	addr_buffer[1] = start_addr & 0xFF;
	addr_buffer[0] = start_addr >> 8;
	return i2c_write_read_dt(spec, addr_buffer, sizeof(addr_buffer), buf, num_bytes);
}

static inline int i2c_burst_write16_dt(const struct i2c_dt_spec *spec, uint16_t start_addr,
				       const uint8_t *buf, uint32_t num_bytes)
{
	uint8_t addr_buffer[2];
	struct i2c_msg msg[2];

	addr_buffer[1] = start_addr & 0xFF;
	addr_buffer[0] = start_addr >> 8;
	msg[0].buf = addr_buffer;
	msg[0].len = 2U;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = (uint8_t *)buf;
	msg[1].len = num_bytes;
	msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer_dt(spec, msg, 2);
}

static int __ap1302_write_reg(const struct device *dev, uint16_t reg_addr, uint8_t reg_size,
			     uint32_t val)
{
	const struct ap1302_config *cfg = dev->config;
	void *value = &val;

	switch (reg_size) {
	case 2:
		*(uint16_t *)value = sys_cpu_to_be16(*(uint16_t *)value);
		break;
	case 4:
		*(uint32_t *)value = sys_cpu_to_be32(*(uint32_t *)value);
		break;
	case 1:
		break;
	default:
		return -ENOTSUP;
	}

	return i2c_burst_write16_dt(&cfg->i2c, reg_addr, value, reg_size);
}

static int ap1302_write_reg(const struct device *dev, uint32_t reg_addr, uint32_t val)
{
	return __ap1302_write_reg(dev, AP1302_REG_ADDR(reg_addr), AP1302_REG_SIZE(reg_addr), val);
}

static int __ap1302_read_reg(const struct device *dev, uint16_t reg_addr, uint8_t reg_size,
			    void *value)
{
	const struct ap1302_config *cfg = dev->config;
	int err;

	if (reg_size > 4) {
		return -ENOTSUP;
	}

	err = i2c_burst_read16_dt(&cfg->i2c, reg_addr, value, reg_size);
	if (err) {
		return err;
	}

	switch (reg_size) {
	case 2:
		*(uint16_t *)value = sys_be16_to_cpu(*(uint16_t *)value);
		break;
	case 4:
		*(uint32_t *)value = sys_be32_to_cpu(*(uint32_t *)value);
		break;
	case 1:
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int ap1302_read_reg(const struct device *dev, uint32_t reg_addr, void *value)
{
	return __ap1302_read_reg(dev, AP1302_REG_ADDR(reg_addr), AP1302_REG_SIZE(reg_addr), value);
}

static int ap1302_reset(const struct device *dev)
{
	const struct ap1302_config *cfg = dev->config;
	int ret;

	ret = gpio_pin_set_dt(&cfg->reset_gpio, 1);
	k_sleep(K_MSEC(5));

	ret = gpio_pin_set_dt(&cfg->reset_gpio, 0);
	k_sleep(K_MSEC(20));

	return ret;
}

static int ap1302_power_up(const struct device *dev)
{
	const struct ap1302_config *cfg = dev->config;
	int ret;
	int i;

	for (i = 0; i < cfg->regulator_num; ++i) {
		ret = regulator_enable(cfg->regulator_array[i]);
		if (ret != 0) {
			LOG_ERR("regulator \"%s\" enable fail [%d]", cfg->regulator_array[i]->name, ret);
			return ret;
		}

		k_sleep(K_MSEC(2));
	}

	return ret;
}

static int ap1302_stall(const struct device *dev, bool stall)
{
	int ret;

	if (stall) {
		ret = ap1302_write_reg(dev, AP1302_SYS_START,
				AP1302_SYS_START_PLL_LOCK |
				AP1302_SYS_START_STALL_MODE_DISABLED);
		ret |= ap1302_write_reg(dev, AP1302_SYS_START,
				AP1302_SYS_START_PLL_LOCK |
				AP1302_SYS_START_STALL_EN |
				AP1302_SYS_START_STALL_MODE_DISABLED);
		k_sleep(K_MSEC(200));
	} else {
		ret = ap1302_write_reg(dev, AP1302_SYS_START,
				AP1302_SYS_START_PLL_LOCK |
				AP1302_SYS_START_STALL_STATUS |
				AP1302_SYS_START_STALL_EN |
				AP1302_SYS_START_STALL_MODE_DISABLED);
	}

	return ret;
}

/* When loading firmware, host writes firmware data from address 0x8000.
 * When the address reaches 0x9FFF, the next address should return to 0x8000.
 * This function handles this address window and load firmware data to AP1302.
 * win_pos indicates the offset within this window. Firmware loading procedure
 * may call this function several times. win_pos records the current position
 * that has been written to.
 */

static int ap1302_write_fw_window(const struct device *dev,
				  uint16_t *win_pos, const uint8_t *buf, uint32_t len)
{
	const struct ap1302_config *cfg = dev->config;
	int ret;
	uint32_t pos;
	uint32_t sub_len;

	for (pos = 0; pos < len; pos += sub_len) {
		if (len - pos < AP1302_FW_WINDOW_SIZE - *win_pos) {
			sub_len = len - pos;
		} else {
			sub_len = AP1302_FW_WINDOW_SIZE - *win_pos;
		}

		ret = i2c_burst_write16_dt(&cfg->i2c, *win_pos + AP1302_FW_WINDOW_OFFSET,
					buf + pos, sub_len);
		if (ret) {
			return ret;
		}

		*win_pos += sub_len;

		if (*win_pos >= AP1302_FW_WINDOW_SIZE) {
			*win_pos = 0;
		}
	}

	return 0;
}

static int ap1302_load_firmware(const struct device *dev)
{
	mm_reg_t fw_addr;
	struct ap1302_firmware *ap1302_fw;
	const uint8_t *fw_data;
	uint16_t val, win_pos = 0;
	int ret;

	device_map(&fw_addr, FW_START_ADDR, FW_SIZE, K_MEM_DIRECT_MAP);

	ap1302_fw = (struct ap1302_firmware *)fw_addr;

	/* The firmware binary contains a header of struct ap1302_firmware.
	 * Following the header is the bootdata of AP1302.
	 * The bootdata pointer can be referenced as &fw[1].
	 */
	fw_data = (uint8_t *)&ap1302_fw[1];

	/* Clear crc register. */
	ret = ap1302_write_reg(dev, AP1302_SIPS_CRC, 0xffff);
	if (ret) {
		LOG_ERR("Fail to write AP1302[0x%x], ret=%d\n",
			AP1302_SIPS_CRC, ret);
		goto err;
	}

	/* Load FW data for PLL init stage. */
	ret = ap1302_write_fw_window(dev, &win_pos, fw_data,
				     ap1302_fw->pll_init_size);
	if (ret) {
		LOG_ERR("Fail to write AP1302 firmware window ret=%d\n", ret);
		goto err;
	}

	/* Write 2 to bootdata_stage register to apply basic_init_hp
	 * settings and enable PLL.
	 */
	ret = ap1302_write_reg(dev, AP1302_BOOTDATA_STAGE, 0x0002);
	if (ret) {
		LOG_ERR("Fail to write AP1302[0x%x], ret=%d\n",
			AP1302_BOOTDATA_STAGE, ret);
		goto err;
	}

	/* Wait 1ms for PLL to lock. */
	k_sleep(K_MSEC(20));

	/* Load the rest of bootdata content. */
	ret = ap1302_write_fw_window(dev, &win_pos,
				     fw_data + ap1302_fw->pll_init_size,
				     ap1302_fw->total_size - ap1302_fw->pll_init_size);
	if (ret) {
		LOG_ERR("Fail to write AP1302 firmware window, ret=%d\n", ret);
		goto err;
	}

	/* Write 0xFFFF to bootdata_stage register to indicate AP1302 that
	 * the whole bootdata content has been loaded.
	 */
	ret = ap1302_write_reg(dev, AP1302_BOOTDATA_STAGE, 0xFFFF);
	if (ret) {
		LOG_ERR("Fail to write AP1302[0x%x], ret=%d\n",
			AP1302_BOOTDATA_STAGE, ret);
		goto err;
	}

	/* Delay 50ms */
	k_sleep(K_MSEC(50));

	ret = ap1302_read_reg(dev, AP1302_BOOTDATA_CHECKSUM, &val);
	if (ret) {
		LOG_ERR("Fail to read AP1302[0x%x], ret=%d\n",
			AP1302_BOOTDATA_CHECKSUM, ret);
		goto err;
	}

	if (val != ap1302_fw->checksum || !val) {
		LOG_ERR("Checksum does not match. T:0x%04X F:0x%04X\n",
			ap1302_fw->checksum, val);
		ret = -ENODEV;
		goto err;
	}

	ret = ap1302_stall(dev, true);
	if (ret) {
		goto err;
	}

err:
	return ret;
}

static int ap1302_set_frmival(const struct device *dev, enum video_endpoint_id ep,
				struct video_frmival *frmival)
{
	struct ap1302_data *drv_data = dev->data;
	int ret;
	uint8_t i, ind = 0;
	uint32_t desired_frmrate, best_match = ap1302_frame_rates[ind];

	desired_frmrate = DIV_ROUND_CLOSEST(frmival->denominator, frmival->numerator);

	/* Find the supported frame rate closest to the desired one */
	for (i = 0; i < ARRAY_SIZE(ap1302_frame_rates); i++) {
		if (ap1302_frame_rates[i] <= drv_data->cur_mode->max_frmrate &&
		    ABS(desired_frmrate, ap1302_frame_rates[i]) <
			    ABS(desired_frmrate, best_match)) {
			best_match = ap1302_frame_rates[i];
			ind = i;
		}
	}

	ret = ap1302_write_reg(dev, AP1302_PREVIEW_MAX_FPS, best_match << 8);
	if (ret) {
		LOG_ERR("Unable to set frame interval");
		return ret;
	}

	drv_data->cur_frmrate = best_match;

	frmival->numerator = 1;
	frmival->denominator = best_match;

	return 0;
}

static int ap1302_get_frmival(const struct device *dev, enum video_endpoint_id ep,
							struct video_frmival *frmival)
{
	struct ap1302_data *drv_data = dev->data;

	frmival->numerator = 1;
	frmival->denominator = drv_data->cur_frmrate;

	return 0;
}

static int ap1302_enum_frmival(const struct device *dev, enum video_endpoint_id ep,
								struct video_frmival_enum *fie)
{
	uint8_t i;

	for (i = 0; i < ARRAY_SIZE(ap1302_modes); i++) {
		if (fie->format->width == ap1302_modes[i].width &&
		    fie->format->height == ap1302_modes[i].height) {
			break;
		}
	}

	if (i == ARRAY_SIZE(ap1302_modes) || fie->index >= ARRAY_SIZE(ap1302_frame_rates) ||
	    ap1302_frame_rates[fie->index] > ap1302_modes[i].max_frmrate) {
		return -EINVAL;
	}

	fie->type = VIDEO_FRMIVAL_TYPE_DISCRETE;
	fie->discrete.numerator = 1;
	fie->discrete.denominator = ap1302_frame_rates[fie->index];

	return 0;
}

static int ap1302_set_fmt(const struct device *dev, enum video_endpoint_id ep,
			   struct video_format *fmt)
{
	int i, ret;
	struct video_frmival def_frmival;
	struct ap1302_data *drv_data = dev->data;
	size_t num_fmts = ARRAY_SIZE(ap1302_fmts);
	size_t array_size_modes = ARRAY_SIZE(ap1302_modes);

	for (i = 0; i < num_fmts; ++i) {
		if (fmt->pixelformat == ap1302_fmts[i].pixelformat && fmt->width >= ap1302_fmts[i].width_min &&
		    fmt->width <= ap1302_fmts[i].width_max && fmt->height >= ap1302_fmts[i].height_min &&
		    fmt->height <= ap1302_fmts[i].height_max) {
			break;
		}
	}

	if (i == num_fmts) {
		LOG_ERR("Unsupported pixel format or resolution");
		return -ENOTSUP;
	}

	if (!memcmp(&drv_data->fmt, fmt, sizeof(drv_data->fmt))) {
		return 0;
	}

	drv_data->fmt = *fmt;

	/* Set resolution */
	for (i = 0; i < array_size_modes; i++) {
		if (fmt->width == ap1302_modes[i].width && fmt->height == ap1302_modes[i].height) {
			ret = ap1302_write_reg(dev, AP1302_PREVIEW_WIDTH, fmt->width);
			ret |= ap1302_write_reg(dev, AP1302_PREVIEW_HEIGHT, fmt->height);
			if (ret) {
				LOG_ERR("Unable to set resolution parameters");
				return ret;
			}

			drv_data->cur_mode = &ap1302_modes[i];
			break;
		}
	}

	/* Set frame rate */
	def_frmival.denominator = drv_data->cur_mode->def_frmrate;
	def_frmival.numerator = 1;

	return ap1302_set_frmival(dev, ep, &def_frmival);
}

static int ap1302_get_fmt(const struct device *dev, enum video_endpoint_id ep,
			   struct video_format *fmt)
{
	struct ap1302_data *drv_data = dev->data;

	*fmt = drv_data->fmt;

	return 0;
}

static int ap1302_set_stream(const struct device *dev, bool enable)
{
	return ap1302_stall(dev, !enable);
}

static int ap1302_get_caps(const struct device *dev, enum video_endpoint_id ep,
			    struct video_caps *caps)
{
	caps->format_caps = ap1302_fmts;

	return 0;
}

static DEVICE_API(video, ap1302_driver_api) = {
	.set_format = ap1302_set_fmt,
	.get_format = ap1302_get_fmt,
	.get_caps = ap1302_get_caps,
	.set_stream = ap1302_set_stream,
	.set_frmival = ap1302_set_frmival,
	.get_frmival = ap1302_get_frmival,
	.enum_frmival = ap1302_enum_frmival,
};

static int ap1302_init(const struct device *dev)
{
	const struct ap1302_config *cfg = dev->config;
	struct video_format fmt;
	uint16_t chip_id;
	int ret;

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&cfg->reset_gpio)) {
		LOG_ERR("%s: device %s is not ready", dev->name, cfg->reset_gpio.port->name);
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&cfg->isp_en_gpio)) {
		LOG_ERR("%s: device %s is not ready", dev->name, cfg->isp_en_gpio.port->name);
		return -ENODEV;
	}

	if (cfg->isp_en_gpio.port != NULL) {
		ret = gpio_pin_configure_dt(&cfg->isp_en_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret) {
			return ret;
		}
	}

	if (cfg->reset_gpio.port != NULL) {
		ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret) {
			return ret;
		}
	}

	k_sleep(K_MSEC(5));

	/* Enable ISP by default */
	if (cfg->isp_en_gpio.port != NULL) {
		gpio_pin_set_dt(&cfg->isp_en_gpio, 1);
	}

	ap1302_power_up(dev);

	ap1302_reset(dev);

	/* Check sensor chip id */
	ret = ap1302_read_reg(dev, AP1302_CHIP_VERSION, &chip_id);
	if (ret) {
		LOG_ERR("Unable to read chip ID ret=%d", ret);
		return -ENODEV;
	}

	if (chip_id == AP1302_CHIP_ID) {
		LOG_INF("AP1302 is found");
	} else {
		LOG_ERR("Wrong ID: %04x (exp %04x)", chip_id, AP1302_CHIP_ID);
		return -ENODEV;
	}

	ret = ap1302_load_firmware(dev);
	if (ret) {
		return ret;
	}

	LOG_INF("Load firmware successfully.\n");

	/* Disable AF mode by default */
	ap1302_write_reg(dev, AP1302_AF_CTRL, 0);

	/* Set default format to 1280x800 */
	fmt.pixelformat = VIDEO_PIX_FMT_UYVY;
	fmt.width = 1280;
	fmt.height = 800;
	fmt.pitch = fmt.width * 2;
	ret = ap1302_set_fmt(dev, VIDEO_EP_OUT, &fmt);
	if (ret) {
		LOG_ERR("Unable to configure default format");
		return -EIO;
	}

	return 0;
}

#define AP1302_GET_REGULATOR(node_id, prop, idx) \
	DEVICE_DT_GET(DT_PROP_BY_IDX(node_id, prop, idx))

#define AP1302_INIT(n)                                                                             \
	static const struct device *power_regulators_##n[] = {                                     \
		DT_FOREACH_PROP_ELEM_SEP(DT_DRV_INST(n), regulators, AP1302_GET_REGULATOR, (,))    \
	};                                                                                         \
                                                                                                   \
	static struct ap1302_data ap1302_data_##n;                                                 \
                                                                                                   \
	static const struct ap1302_config ap1302_cfg_##n = {                                       \
		.i2c = I2C_DT_SPEC_INST_GET(n),                                                    \
		.regulator_array = power_regulators_##n,                                           \
		.regulator_num = DT_INST_PROP_LEN(n, regulators),                                  \
		.isp_en_gpio = GPIO_DT_SPEC_INST_GET(n, isp_en_gpios),                             \
		.reset_gpio = GPIO_DT_SPEC_INST_GET(n, reset_gpios),                               \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, &ap1302_init, NULL, &ap1302_data_##n, &ap1302_cfg_##n,            \
			      POST_KERNEL, CONFIG_VIDEO_AP1302_INIT_PRIORITY, &ap1302_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AP1302_INIT)
