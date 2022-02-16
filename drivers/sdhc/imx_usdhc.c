/*
 * Copyright (c) 2022 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#define DT_DRV_COMPAT nxp_imx_sdhc

#include <zephyr.h>
#include <devicetree.h>
#include <drivers/sdhc.h>
#include <sd/sd_spec.h>
#include <drivers/clock_control.h>
#include <drivers/gpio.h>
#include <logging/log.h>
#include <soc.h>

LOG_MODULE_REGISTER(usdhc, CONFIG_SDHC_LOG_LEVEL);

#include <fsl_usdhc.h>
#include <fsl_cache.h>

enum transfer_callback_status {
	TRANSFER_CMD_COMPLETE = (0x1 << 0),
	TRANSFER_CMD_FAILED = (0x1 << 1),
	TRANSFER_DATA_COMPLETE = (0x1 << 2),
	TRANSFER_DATA_FAILED = (0x1 << 3)
};

#define TRANSFER_CMD_FLAGS (TRANSFER_CMD_COMPLETE | TRANSFER_CMD_FAILED)
#define TRANSFER_DATA_FLAGS (TRANSFER_DATA_COMPLETE | TRANSFER_DATA_FAILED)

/* USDHC tuning constants */
#define IMX_USDHC_STANDARD_TUNING_START (10U)
#define IMX_USDHC_TUNING_STEP (2U)
#define IMX_USDHC_STANDARD_TUNING_COUNTER (60U)
/* Default transfer timeout in ms for tuning */
#define IMX_USDHC_DEFAULT_TIMEOUT (5000U)

struct usdhc_host_transfer {
	usdhc_transfer_t *transfer;
	k_timeout_t command_timeout;
	k_timeout_t data_timeout;
};

struct usdhc_config {
	USDHC_Type *base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	uint8_t nusdhc;
	const struct device *pwr_gpio;
	uint8_t pwr_pin;
	gpio_dt_flags_t pwr_flags;
	const struct device *detect_gpio;
	uint8_t detect_pin;
	gpio_dt_flags_t detect_flags;
	bool detect_dat3;
	uint32_t data_timeout;
	uint32_t read_watermark;
	uint32_t write_watermark;
	uint32_t max_current_330;
	uint32_t max_current_300;
	uint32_t max_current_180;
	uint32_t power_delay_ms;
	uint32_t min_bus_freq;
	uint32_t max_bus_freq;
	void (*irq_config_func)(const struct device *dev);
};

struct usdhc_data {
	struct sdhc_host_props props;
	bool card_present;
	struct k_sem transfer_sem;
	volatile uint32_t transfer_status;
	usdhc_handle_t transfer_handle;
	struct sdhc_io host_io;
	uint8_t usdhc_rx_dummy[64] __aligned(32);
	uint32_t *usdhc_dma_descriptor; /* ADMA descriptor table (noncachable) */
	uint32_t dma_descriptor_len; /* DMA descriptor table length in words */
};

/*
 * Return 0 if card is not busy, 1 if it is
 */
static int imx_usdhc_card_busy(const struct device *dev)
{
	const struct usdhc_config *cfg = dev->config;

	return (USDHC_GetPresentStatusFlags(cfg->base)
		& (kUSDHC_Data0LineLevelFlag |
		kUSDHC_Data1LineLevelFlag |
		kUSDHC_Data2LineLevelFlag |
		kUSDHC_Data3LineLevelFlag))
		? 0 : 1;
}

/*
 * Perform early system init for SDHC
 */
static int imx_usdhc_init(const struct device *dev)
{
	const struct usdhc_config *cfg = dev->config;
	struct usdhc_data *data = dev->data;
	usdhc_config_t host_config = {0};
	int ret;
	const usdhc_transfer_callback_t callbacks = {
		.TransferComplete = transfer_complete_cb,
	};

	USDHC_TransferCreateHandle(cfg->base, &data->transfer_handle,
		&callbacks, (void *)dev);
	cfg->irq_config_func(dev);


	host_config.dataTimeout = cfg->data_timeout;
	host_config.endianMode = kUSDHC_EndianModeLittle;
	host_config.readWatermarkLevel = cfg->read_watermark;
	host_config.writeWatermarkLevel = cfg->write_watermark;
	USDHC_Init(cfg->base, &host_config);
	/* Set power GPIO low, so card starts powered off */
	if (cfg->pwr_gpio) {
		ret = gpio_pin_configure(cfg->pwr_gpio, cfg->pwr_pin,
			cfg->pwr_flags | GPIO_OUTPUT_INACTIVE);
		if (ret) {
			return ret;
		}
	}
	if (cfg->detect_gpio) {
		ret = gpio_pin_configure(cfg->detect_gpio, cfg->detect_pin,
			cfg->detect_flags | GPIO_INPUT);
		if (ret) {
			return ret;
		}
	}
	memset(&data->host_io, 0, sizeof(data->host_io));
	return k_sem_init(&data->transfer_sem, 0, 1);
}

static struct sdhc_driver_api usdhc_api = {
	.card_busy = imx_usdhc_card_busy,
};

#define IMX_USDHC_INIT_NONE(n)

#define IMX_USDHC_INIT_PWR_PROPS(n)						\
	.pwr_gpio = DEVICE_DT_GET(DT_INST_PHANDLE(n, pwr_gpios)),		\
	.pwr_pin = DT_INST_GPIO_PIN(n, pwr_gpios),				\
	.pwr_flags = DT_INST_GPIO_FLAGS(n, pwr_gpios),

#define IMX_USDHC_INIT_CD_PROPS(n)						\
	.detect_gpio = DEVICE_DT_GET(DT_INST_PHANDLE(n, cd_gpios)),		\
	.detect_pin = DT_INST_GPIO_PIN(n, cd_gpios),				\
	.detect_flags = DT_INST_GPIO_FLAGS(n, cd_gpios),

#define IMX_USDHC_INIT_PWR(n)							\
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, pwr_gpios),			\
		    (IMX_USDHC_INIT_PWR_PROPS(n)),				\
		    (IMX_USDHC_INIT_NONE(n)))

#define IMX_USDHC_INIT_CD(n)							\
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, cd_gpios),				\
		    (IMX_USDHC_INIT_CD_PROPS(n)),				\
		    (IMX_USDHC_INIT_NONE(n)))

#define IMX_USDHC_INIT(n)							\
	static void usdhc_##n##_irq_config_func(const struct device *dev)	\
	{									\
	}									\
										\
	static const struct usdhc_config usdhc_##n##_config = {			\
		.base = (USDHC_Type *) DT_INST_REG_ADDR(n),			\
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),		\
		.clock_subsys =							\
			(clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, name),	\
		.nusdhc = n,							\
		IMX_USDHC_INIT_PWR(n)						\
		IMX_USDHC_INIT_CD(n)						\
		.data_timeout = DT_INST_PROP(n, data_timeout),			\
		.detect_dat3 = DT_INST_PROP(n, detect_dat3),			\
		.read_watermark = DT_INST_PROP(n, read_watermark),		\
		.write_watermark = DT_INST_PROP(n, write_watermark),		\
		.max_current_330 = DT_INST_PROP(n, max_current_330),		\
		.max_current_180 = DT_INST_PROP(n, max_current_180),		\
		.min_bus_freq = DT_INST_PROP(n, min_bus_freq),			\
		.max_bus_freq = DT_INST_PROP(n, max_bus_freq),			\
		.power_delay_ms = DT_INST_PROP(n, power_delay_ms),		\
		.irq_config_func = usdhc_##n##_irq_config_func,			\
	};									\
										\
										\
	static uint32_t	__aligned(32)						\
		usdhc_##n##_dma_descriptor[CONFIG_IMX_USDHC_DMA_BUFFER_SIZE / 4]\
		__attribute__((__section__(".nocache")));			\
										\
	static struct usdhc_data usdhc_##n##_data = {				\
		.card_present = false,						\
		.usdhc_dma_descriptor = usdhc_##n##_dma_descriptor,		\
		.dma_descriptor_len = CONFIG_IMX_USDHC_DMA_BUFFER_SIZE / 4,	\
	};									\
										\
	DEVICE_DT_INST_DEFINE(n,						\
			&imx_usdhc_init,					\
			NULL,							\
			&usdhc_##n##_data,					\
			&usdhc_##n##_config,					\
			POST_KERNEL,						\
			CONFIG_SDHC_INIT_PRIORITY,				\
			&usdhc_api);

DT_INST_FOREACH_STATUS_OKAY(IMX_USDHC_INIT)
