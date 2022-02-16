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

static int imx_usdhc_dat3_pull(const struct usdhc_config *cfg, bool pullup)
{
	int ret = 0U;

	/* Call board specific function to pull down DAT3 */
	imxrt_usdhc_dat3_pull(pullup);
#ifdef CONFIG_IMX_USDHC_DAT3_PWR_TOGGLE
	if (!pullup) {
		/* Power off the card to clear DAT3 legacy status */
		if (cfg->pwr_gpio) {
			ret = gpio_pin_set(cfg->pwr_gpio, cfg->pwr_pin, 0);
			if (ret) {
				return ret;
			}
			/* Delay for card power off to complete */
			k_msleep(cfg->power_delay_ms);
			ret = gpio_pin_set(cfg->pwr_gpio, cfg->pwr_pin, 1);
			if (ret) {
				return ret;
			}
		}
	}
#endif
	return ret;
}

/*
 * Initialize SDHC host properties for use in get_host_props api call
 */
static void imx_usdhc_init_host_props(const struct device *dev)
{
	const struct usdhc_config *cfg = dev->config;
	struct usdhc_data *data = dev->data;
	usdhc_capability_t caps;
	struct sdhc_host_props *props = &data->props;

	memset(props, 0, sizeof(struct sdhc_host_props));
	props->f_max = cfg->max_bus_freq;
	props->f_min = cfg->min_bus_freq;
	props->max_current_330 = cfg->max_current_330;
	props->max_current_180 = cfg->max_current_180;
	props->power_delay = cfg->power_delay_ms;
	/* Read host capabilities */
	USDHC_GetCapability(cfg->base, &caps);
	props->host_caps.vol_180_support = (bool)(caps.flags & kUSDHC_SupportV180Flag);
	props->host_caps.vol_300_support = (bool)(caps.flags & kUSDHC_SupportV300Flag);
	props->host_caps.vol_330_support = (bool)(caps.flags & kUSDHC_SupportV330Flag);
	props->host_caps.suspend_res_support = (bool)(caps.flags & kUSDHC_SupportSuspendResumeFlag);
	props->host_caps.sdma_support = (bool)(caps.flags & kUSDHC_SupportDmaFlag);
	props->host_caps.high_spd_support = (bool)(caps.flags & kUSDHC_SupportHighSpeedFlag);
	props->host_caps.adma_2_support = (bool)(caps.flags & kUSDHC_SupportAdmaFlag);
	props->host_caps.max_blk_len = (bool)(caps.maxBlockLength);
	props->host_caps.ddr50_support = (bool)(caps.flags & kUSDHC_SupportDDR50Flag);
	props->host_caps.sdr104_support = (bool)(caps.flags & kUSDHC_SupportSDR104Flag);
	props->host_caps.sdr50_support = (bool)(caps.flags & kUSDHC_SupportSDR50Flag);
}

/*
 * Reset USDHC controller
 */
static int imx_usdhc_reset(const struct device *dev)
{
	const struct usdhc_config *cfg = dev->config;
	/* Switch to default I/O voltage of 3.3V */
	UDSHC_SelectVoltage(cfg->base, false);
	USDHC_EnableDDRMode(cfg->base, false, 0U);
#if defined(FSL_FEATURE_USDHC_HAS_SDR50_MODE) && (FSL_FEATURE_USDHC_HAS_SDR50_MODE)
	USDHC_EnableStandardTuning(cfg->base, 0, 0, false);
	USDHC_EnableAutoTuning(cfg->base, false);
#endif

#if FSL_FEATURE_USDHC_HAS_HS400_MODE
	/* Disable HS400 mode */
	USDHC_EnableHS400Mode(cfg->base, false);
	/* Disable DLL */
	USDHC_EnableStrobeDLL(cfg->base, false);
#endif

	/* Reset data/command/tuning circuit */
	return USDHC_Reset(cfg->base, kUSDHC_ResetAll, 100U) == true ? 0 : -ETIMEDOUT;
}

/* Wait for USDHC to gate clock when it is disabled */
static inline void imx_usdhc_wait_clock_gate(USDHC_Type *base)
{
	uint32_t timeout = 1000;

	while (timeout--) {
		if (base->PRES_STATE & USDHC_PRES_STATE_SDOFF_MASK) {
			break;
		}
	}
	if (timeout == 0) {
		LOG_WRN("SD clock did not gate in time");
	}
}

/*
 * Set SDHC io properties
 */
static int imx_usdhc_set_io(const struct device *dev, struct sdhc_io *ios)
{
	const struct usdhc_config *cfg = dev->config;
	struct usdhc_data *data = dev->data;
	uint32_t src_clk_hz, bus_clk;
	struct sdhc_io *host_io = &data->host_io;

	LOG_DBG("SDHC I/O: bus width %d, clock %dHz, card power %s, voltage %s",
		ios->bus_width,
		ios->clock,
		ios->power_mode == SDHC_POWER_ON ? "ON" : "OFF",
		ios->signal_voltage == SD_VOL_1_8_V ? "1.8V" : "3.3V"
		);

	if (clock_control_get_rate(cfg->clock_dev,
				cfg->clock_subsys,
				&src_clk_hz)) {
		return -EINVAL;
	}

	if (ios->clock && (ios->clock > data->props.f_max || ios->clock < data->props.f_min)) {
		return -EINVAL;
	}

	/* Set host clock */
	if (host_io->clock != ios->clock) {
		if (ios->clock != 0) {
			/* Enable the clock output */
			bus_clk = USDHC_SetSdClock(cfg->base, src_clk_hz, ios->clock);
			if (bus_clk == 0) {
				return -ENOTSUP;
			}
		}
		host_io->clock = ios->clock;
	}


	/* Set bus width */
	if (host_io->bus_width != ios->bus_width) {
		switch (ios->bus_width) {
		case SDHC_BUS_WIDTH1BIT:
			USDHC_SetDataBusWidth(cfg->base, kUSDHC_DataBusWidth1Bit);
			break;
		case SDHC_BUS_WIDTH4BIT:
			USDHC_SetDataBusWidth(cfg->base, kUSDHC_DataBusWidth4Bit);
			break;
		case SDHC_BUS_WIDTH8BIT:
			USDHC_SetDataBusWidth(cfg->base, kUSDHC_DataBusWidth8Bit);
			break;
		default:
			return -ENOTSUP;
		}
		host_io->bus_width = ios->bus_width;
	}

	/* Set host signal voltage */
	if (ios->signal_voltage != host_io->signal_voltage) {
		switch (ios->signal_voltage) {
		case SD_VOL_3_3_V:
		case SD_VOL_3_0_V:
			UDSHC_SelectVoltage(cfg->base, false);
			break;
		case SD_VOL_1_8_V:
			/**
			 * USDHC peripheral deviates from SD spec here.
			 * The host controller specification claims
			 * the "SD clock enable" bit can be used to gate the SD
			 * clock by clearing it. The USDHC controller does not
			 * provide this bit, only a way to force the SD clock
			 * on. We will instead delay 10 ms to allow the clock
			 * to be gated for enough time, then force it on for
			 * 10 ms, then allow it to be gated again.
			 */
			/* Switch to 1.8V */
			UDSHC_SelectVoltage(cfg->base, true);
			/* Wait 10 ms- clock will be gated during this period */
			k_msleep(10);
			/* Force the clock on */
			USDHC_ForceClockOn(cfg->base, true);
			/* Keep the clock on for a moment, so SD will recognize it */
			k_msleep(10);
			/* Stop forcing clock on */
			USDHC_ForceClockOn(cfg->base, false);
			break;
		default:
			return -ENOTSUP;
		}
		/* Save new host voltage */
		host_io->signal_voltage = ios->signal_voltage;
	}

	/* Set card power */
	if (host_io->power_mode != ios->power_mode) {
		if (ios->power_mode == SDHC_POWER_OFF) {
			gpio_pin_set(cfg->pwr_gpio, cfg->pwr_pin, 0);
		} else if (ios->power_mode == SDHC_POWER_ON) {
			gpio_pin_set(cfg->pwr_gpio, cfg->pwr_pin, 1);
		}
		host_io->power_mode = ios->power_mode;
	}

	/* Set I/O timing */
	if (host_io->timing != ios->timing) {
		switch (ios->timing) {
		case SDHC_TIMING_LEGACY:
		case SDHC_TIMING_HS:
			break;
		case SDHC_TIMING_SDR12:
		case SDHC_TIMING_SDR25:
			imxrt_usdhc_pinmux(cfg->nusdhc, false, 0, 7);
			break;
		case SDHC_TIMING_SDR50:
			imxrt_usdhc_pinmux(cfg->nusdhc, false, 2, 7);
			break;
		case SDHC_TIMING_SDR104:
		case SDHC_TIMING_DDR50:
		case SDHC_TIMING_DDR52:
		case SDHC_TIMING_HS200:
		case SDHC_TIMING_HS400:
			imxrt_usdhc_pinmux(cfg->nusdhc, false, 3, 7);
			break;
		default:
			return -ENOTSUP;
		}
		host_io->timing = ios->timing;
	}

	return 0;
}

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
 * Get card presence
 */
static int imx_usdhc_get_card_present(const struct device *dev)
{
	const struct usdhc_config *cfg = dev->config;
	bool card_inserted = false;

	if (cfg->detect_dat3) {
		/* Detect card presence with DAT3 line pull */
		USDHC_CardDetectByData3(cfg->base, true);
		imx_usdhc_dat3_pull(cfg, false);
		card_inserted = USDHC_DetectCardInsert(cfg->base);
		/* Clear card detection and pull */
		imx_usdhc_dat3_pull(cfg, true);
		USDHC_CardDetectByData3(cfg->base, false);
	} else if (cfg->detect_gpio) {
		card_inserted = gpio_pin_get(cfg->detect_gpio, cfg->detect_pin) > 0;
	} else {
		LOG_WRN("No card presence method configured, assuming card is present");
		card_inserted = true;
	}
	return ((int)card_inserted);
}

/*
 * Get host properties
 */
static int imx_usdhc_get_host_props(const struct device *dev,
	struct sdhc_host_props *props)
{
	struct usdhc_data *data = dev->data;

	memcpy(props, &data->props, sizeof(struct sdhc_host_props));
	return 0;
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
	/* Read host controller properties */
	imx_usdhc_init_host_props(dev);
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
	.reset = imx_usdhc_reset,
	.set_io = imx_usdhc_set_io,
	.get_card_present = imx_usdhc_get_card_present,
	.card_busy = imx_usdhc_card_busy,
	.get_host_props = imx_usdhc_get_host_props,
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
