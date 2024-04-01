/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#define DT_DRV_COMPAT nxp_flexio_pwm

#include <errno.h>
#include <zephyr/drivers/pwm.h>
#include <fsl_flexio.h>
#include <fsl_clock.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>

#include <zephyr/logging/log.h>
#include <zephyr/drivers/misc/nxp_flexio/nxp_flexio.h>


LOG_MODULE_REGISTER(pwm_mcux_flexio, CONFIG_PWM_LOG_LEVEL);

#define FLEXIO_PWM_TIMER_CMP_MAX_VALUE		(0xFFFFU)
#define FLEXIO_PWM_TIMCMP_CMP_UPPER_MASK	(0x0000FF00UL)
#define FLEXIO_PWM_TIMCMP_CMP_LOWER_MASK	(0x000000FFUL)
#define FLEXIO_PWM_TIMCMP_CMP_UPPER_SHIFT	(0x8U)
#define FLEXIO_PWM_TIMCMP_CMP_LOWER_SHIFT	(0x0U)
#define FLEXIO_PWM_TIMCMP_CMP_UPPER_WIDTH	(0x8U)
#define FLEXIO_PWM_TIMCMP_CMP_LOWER_WIDTH	(0x8U)
#define FLEXIO_PWM_TIMCMP_CMP_UPPER(x)		(((uint32_t)(((uint32_t)(x)) << FLEXIO_PWM_TIMCMP_CMP_UPPER_SHIFT)) & FLEXIO_PWM_TIMCMP_CMP_UPPER_MASK)
#define FLEXIO_PWM_TIMCMP_CMP_LOWER(x)		(((uint32_t)(((uint32_t)(x)) << FLEXIO_PWM_TIMCMP_CMP_LOWER_SHIFT)) & FLEXIO_PWM_TIMCMP_CMP_LOWER_MASK)

enum flexio_pwm_polarity
{
	FLEXIO_PWM_ACTIVE_HIGH   = 0x0U,
	FLEXIO_PWM_ACTIVE_LOW    = 0x1U
};

enum flexio_pwm_timerinit
{
	/** @brief Timer Initial output is logic one */
	FLEXIO_PWM_TIMER_INIT_HIGH   = 0x00U,
	/** @brief Timer Initial output is logic zero */
	FLEXIO_PWM_TIMER_INIT_LOW    = 0x1U
};

enum flexio_pwm_prescaler
{
	/* Decrement counter on Flexio clock */
	FLEXIO_PWM_CLK_DIV_1     = 0U,
	/* Decrement counter on Flexio clock divided by 16 */
	FLEXIO_PWM_CLK_DIV_16    = 4U,
	/* Decrement counter on Flexio clock divided by 256 */
	FLEXIO_PWM_CLK_DIV_256   = 5U
};

enum flexio_pwm_timer_mode
{
	/** @brief Timer disabled */
	FLEXIO_PWM_TIMER_DISABLED  = 0x00U,
	/** @brief Timer in 8 bit Pwm High mode */
	FLEXIO_PWM_TIMER_PWM_HIGH  = 0x02U,
	/** @brief Timer in 8 bit Pwm Low mode */
	FLEXIO_PWM_TIMER_PWM_LOW   = 0x06U
};

enum flexio_pwm_timer_pin
{
	/** @brief Timer Pin output disabled */
	FLEXIO_PWM_TIMER_PIN_OUTPUT_DISABLE  = 0x00U,
	/** @brief Timer Pin Output mode */
	FLEXIO_PWM_TIMER_PIN_OUTPUT_ENABLE   = 0x03U
};

typedef struct
{
	/** Flexio used pin index */
	uint8_t pin_id;
	/** Counter decrement clock prescaler */
	enum flexio_pwm_prescaler prescaler;
	/** Pwm period in ticks */
	uint16_t period;
	/** Pwm duty cycle in ticks */
	uint16_t duty_cycle;
	/** Pwm output polarity */
	enum flexio_pwm_polarity polarity;
} flexio_pwm_channel_config_type;

struct flexio_pwm_pulse_info {
	uint8_t pwm_pulse_channels;
	flexio_pwm_channel_config_type *pwm_info;
};

struct pwm_mcux_flexio_config {
	const struct device *flexio_dev;
	FLEXIO_Type *flexio_base;
	const struct pinctrl_dev_config *pincfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct flexio_pwm_pulse_info *pulse_info;
	const struct nxp_flexio_child *child;
};

struct pwm_mcux_flexio_data {
	uint32_t period_cycles;
	uint32_t flexio_clk;
};

static void Flexio_Pwm_Ip_SetPinLevel(FLEXIO_Type * Base, uint8_t Pin, bool Level)
{
	Base->PINOUTD = (Base->PINOUTD & ~((uint32_t)((uint32_t)1U << Pin))) | (FLEXIO_PINOUTD_OUTD((uint32_t)((true == Level) ? (uint32_t)0x1U : (uint32_t)0x0U) << Pin));
}

static bool Flexio_Pwm_Ip_GetPinOverride(const FLEXIO_Type *const Base, uint8_t Pin)
{
	return ((Base->PINOUTE & (uint32)((uint32)1U << Pin)) != 0UL);
}

static void Flexio_Pwm_Ip_ConfigurePinOverride(FLEXIO_Type * Base, uint8_t Pin, bool Enabled)
{
	Base->PINOUTE = (Base->PINOUTE & ~((uint32)((uint32)1U << Pin))) | FLEXIO_PINOUTE_OUTE((uint32)((true == Enabled) ? (uint32)0x1U : (uint32)0x0U) << Pin);
}

static int mcux_flexio_get_prescaler_div(const struct device *dev, uint32_t channel)
{
	const struct pwm_mcux_flexio_config *config = dev->config;
	flexio_pwm_channel_config_type *pwm_info;
	int ret = 1;
	pwm_info = &config->pulse_info->pwm_info[channel];

	switch (pwm_info->prescaler) {
		case FLEXIO_PWM_CLK_DIV_16:
			ret = 16;
			break;
		case FLEXIO_PWM_CLK_DIV_256:
			ret = 256;
			break;
		case FLEXIO_PWM_CLK_DIV_1:
			__fallthrough;
		default:
			ret = 1;
			break;
	}

	return ret;
}

static int mcux_flexio_pwm_set_cycles(const struct device *dev,
				       uint32_t channel, uint32_t period_cycles,
				       uint32_t pulse_cycles, pwm_flags_t flags)
{
	const struct pwm_mcux_flexio_config *config = dev->config;
	flexio_timer_config_t timerConfig;
	flexio_pwm_channel_config_type *pwm_info;
	FLEXIO_Type *flexio_base = (FLEXIO_Type *)(config->flexio_base);
	struct nxp_flexio_child *child = (struct nxp_flexio_child *)(config->child); 

	/* Check received parameters for sanity */
	if (channel >= config->pulse_info->pwm_pulse_channels) {
		LOG_ERR("Invalid channel");
		return -EINVAL;
	}

	if (period_cycles == 0) {
		LOG_ERR("Channel can not be set to inactive level");
		return -ENOTSUP;
	}
    
	if(FLEXIO_PWM_TIMER_CMP_MAX_VALUE <= (uint16_t)pulse_cycles) {
		LOG_ERR("Duty cycle is out of range");
		return -EINVAL;
	}

	if(FLEXIO_PWM_TIMER_CMP_MAX_VALUE <= (uint16_t)(period_cycles - pulse_cycles)) {
		LOG_ERR("low period of the cycle is out of range");
		return -EINVAL;
	}
	
	if(pulse_cycles > period_cycles) {
		LOG_ERR("Duty cycle cannot be greater than 100 percent");
		return -EINVAL;
	}

	pwm_info = &config->pulse_info->pwm_info[channel];

	if ((flags & PWM_POLARITY_INVERTED) == 0) {
		pwm_info->polarity = FLEXIO_PWM_ACTIVE_HIGH;
	} else {
		pwm_info->polarity = FLEXIO_PWM_ACTIVE_LOW;
	}

	if (pwm_info->polarity == FLEXIO_PWM_ACTIVE_HIGH) {
		timerConfig.timerOutput = kFLEXIO_TimerOutputOneNotAffectedByReset;
		timerConfig.timerMode = kFLEXIO_TimerModeDual8BitPWM;

	}
	else {
		timerConfig.timerOutput = kFLEXIO_TimerOutputZeroNotAffectedByReset;
		timerConfig.timerMode = kFLEXIO_TimerModeDual8BitPWMLow;
	}

	pwm_info->duty_cycle = pulse_cycles;

	pwm_info->period = period_cycles;

	timerConfig.timerCompare = ((uint8_t)(pwm_info->duty_cycle - 1U)) |
		((uint8_t)(pwm_info->period - pwm_info->duty_cycle - 1U) << FLEXIO_PWM_TIMCMP_CMP_UPPER_WIDTH);

	timerConfig.timerDecrement = pwm_info->prescaler;
	timerConfig.timerStop = kFLEXIO_TimerStopBitDisabled;
	timerConfig.timerEnable = kFLEXIO_TimerEnabledAlways;
	timerConfig.timerDisable = kFLEXIO_TimerDisableNever;
	timerConfig.timerStart = kFLEXIO_TimerStartBitDisabled;
	timerConfig.timerReset = kFLEXIO_TimerResetNever;
	timerConfig.triggerSource = kFLEXIO_TimerTriggerSourceInternal;

	/* Enable the pin out for the selected timer */
	timerConfig.pinConfig = FLEXIO_PWM_TIMER_PIN_OUTPUT_ENABLE;
	timerConfig.pinPolarity = pwm_info->polarity;

	/* Select the pin that the selected timer will output the signal on */
	timerConfig.pinSelect = pwm_info->pin_id;

	FLEXIO_SetTimerConfig(flexio_base, child->res.timer_index[channel], &timerConfig);

	/* Disable pin override if active to support channels working in cases not 0% 100% */
	if (Flexio_Pwm_Ip_GetPinOverride(flexio_base, pwm_info->pin_id))
	{
		Flexio_Pwm_Ip_ConfigurePinOverride(flexio_base, pwm_info->pin_id, false);
	}

	return 0;
}

static int mcux_flexio_pwm_get_cycles_per_sec(const struct device *dev,
						uint32_t channel,
						uint64_t *cycles)
{
	int prescaler_div = 1;
	const struct pwm_mcux_flexio_config *config = dev->config;
	struct pwm_mcux_flexio_data *data = dev->data;
	flexio_pwm_channel_config_type *pwm_info;

	pwm_info = &config->pulse_info->pwm_info[channel];
	prescaler_div = mcux_flexio_get_prescaler_div(dev, channel);
	*cycles = (uint64_t)((data->flexio_clk *2) / ((pwm_info->period) * (prescaler_div)));

	return 0;
}

static int mcux_flexio_pwm_init(const struct device *dev)
{
	const struct pwm_mcux_flexio_config *config = dev->config;
	struct pwm_mcux_flexio_data *data = dev->data;
	flexio_timer_config_t timerConfig;
	uint8_t ch_id = 0;
	int err;
	flexio_pwm_channel_config_type *pwm_info;
	FLEXIO_Type *flexio_base = (FLEXIO_Type *)(config->flexio_base);
	struct nxp_flexio_child *child = (struct nxp_flexio_child *)(config->child); 

	if (!device_is_ready(config->clock_dev)) {
		return -ENODEV;
	}

	if (clock_control_get_rate(config->clock_dev, config->clock_subsys,
					&data->flexio_clk)) {
		return -EINVAL;
	}

	err = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
	if (err) {
		return err;
	}

	err = nxp_flexio_child_attach(config->flexio_dev, child);
	if (err < 0) {
		return err;
	}

	for (ch_id = 0; ch_id < config->pulse_info->pwm_pulse_channels; ch_id++) {
		pwm_info = &config->pulse_info->pwm_info[ch_id];

		/* Reset timer settings */
		(void)memset(&timerConfig, 0, sizeof(timerConfig));
		FLEXIO_SetTimerConfig(flexio_base, child->res.timer_index[ch_id], &timerConfig);

		/* Reset the value driven on the corresponding pin */
		Flexio_Pwm_Ip_SetPinLevel(flexio_base, pwm_info->pin_id, false);
		Flexio_Pwm_Ip_ConfigurePinOverride(flexio_base, pwm_info->pin_id, false);

		/* Timer output is logic one when enabled and is not affected by timer reset */
		timerConfig.timerOutput = kFLEXIO_TimerOutputOneNotAffectedByReset;

		/* Set the timer mode to dual 8bit counter PWM high */
		timerConfig.timerMode = kFLEXIO_TimerModeDual8BitPWM;

		/* Timer scaling factor w.r.t Flexio Clock */
		timerConfig.timerDecrement = pwm_info->prescaler;
		
		/* Program the PWM pulse */
		timerConfig.timerCompare = ((uint8_t)(pwm_info->duty_cycle - 1U)) |
			((uint8_t)(pwm_info->period - pwm_info->duty_cycle - 1U) 
			 << FLEXIO_PWM_TIMCMP_CMP_UPPER_WIDTH);

		/* Configure Timer CFG and CTL bits to support PWM mode */
		timerConfig.timerStop = kFLEXIO_TimerStopBitDisabled;
		timerConfig.timerEnable = kFLEXIO_TimerEnabledAlways;
		timerConfig.timerDisable = kFLEXIO_TimerDisableNever;
		timerConfig.timerStart = kFLEXIO_TimerStartBitDisabled;
		timerConfig.timerReset = kFLEXIO_TimerResetNever;
		timerConfig.triggerSource = kFLEXIO_TimerTriggerSourceInternal;
	
		/* Enable the pin out for the selected timer */
		timerConfig.pinConfig = FLEXIO_PWM_TIMER_PIN_OUTPUT_ENABLE;
		timerConfig.pinPolarity = pwm_info->polarity;

		/* Select the pin that the selected timer will output the signal on */
		timerConfig.pinSelect = pwm_info->pin_id;

		FLEXIO_SetTimerConfig(flexio_base, child->res.timer_index[ch_id], &timerConfig);
	}

	return 0;
}

static const struct pwm_driver_api pwm_mcux_flexio_driver_api = {
	.set_cycles = mcux_flexio_pwm_set_cycles,
	.get_cycles_per_sec = mcux_flexio_pwm_get_cycles_per_sec,
};

#define _FLEXIO_PWM_PULSE_GEN_CONFIG(n)								\
	{											\
		.pin_id = DT_PROP(n, pin_id),							\
		.prescaler = DT_PROP(n, prescaler),						\
		.period = DT_PROP(n, period),							\
		.duty_cycle = DT_PROP(n, duty_cycle),						\
		.polarity = DT_PROP(n, polarity),						\
	},

#define FLEXIO_PWM_PULSE_GEN_CONFIG(n)								\
	flexio_pwm_channel_config_type flexio_pwm_##n##_channel_init[] = {			\
		DT_INST_FOREACH_CHILD_STATUS_OKAY(n, _FLEXIO_PWM_PULSE_GEN_CONFIG)		\
	};											\
	const struct flexio_pwm_pulse_info flexio_pwm_##n##_info = {				\
		.pwm_pulse_channels = ARRAY_SIZE(flexio_pwm_##n##_channel_init),		\
		.pwm_info = (flexio_pwm_channel_config_type *)flexio_pwm_##n##_channel_init,	\
	};

#define FLEXIO_PWM_TIMER_INDEX_INIT(n)								\
	uint8_t flexio_pwm_##n##_timer_index[ARRAY_SIZE(flexio_pwm_##n##_channel_init)];

#define FLEXIO_PWM_CHILD_CONFIG(n)								\
	static const struct nxp_flexio_child mcux_flexio_pwm_child_##n = {			\
		.isr = NULL,									\
		.user_data = NULL,								\
		.res = {									\
			.shifter_index = NULL,							\
			.shifter_count = 0,							\
			.timer_index = (uint8_t *)flexio_pwm_##n##_timer_index,			\
			.timer_count = ARRAY_SIZE(flexio_pwm_##n##_channel_init)		\
		}										\
	};

#define FLEXIO_PWM_PULSE_GEN_GET_CONFIG(n)							\
	.pulse_info = (struct flexio_pwm_pulse_info *)&flexio_pwm_##n##_info,


#define PWM_MCUX_FLEXIO_PWM_INIT(n)								\
	PINCTRL_DT_INST_DEFINE(n);								\
	FLEXIO_PWM_PULSE_GEN_CONFIG(n)								\
	FLEXIO_PWM_TIMER_INDEX_INIT(n)								\
	FLEXIO_PWM_CHILD_CONFIG(n)								\
	static const struct pwm_mcux_flexio_config pwm_mcux_flexio_config_##n = {		\
		.flexio_dev = DEVICE_DT_GET(DT_INST_PARENT(n)),					\
		.flexio_base = (FLEXIO_Type *)DT_REG_ADDR(DT_INST_PARENT(n)),			\
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),					\
		.clock_dev = DEVICE_DT_GET(DT_CLOCKS_CTLR(DT_INST_PARENT(n))),			\
		.clock_subsys = (clock_control_subsys_t)DT_CLOCKS_CELL(DT_INST_PARENT(n), name),\
		.child = &mcux_flexio_pwm_child_##n,						\
		FLEXIO_PWM_PULSE_GEN_GET_CONFIG(n)						\
	};											\
												\
	static struct pwm_mcux_flexio_data pwm_mcux_flexio_data_##n;				\
	DEVICE_DT_INST_DEFINE(n,								\
			      &mcux_flexio_pwm_init,						\
			      NULL,								\
			      &pwm_mcux_flexio_data_##n,					\
			      &pwm_mcux_flexio_config_##n,					\
			      POST_KERNEL, CONFIG_PWM_INIT_PRIORITY,				\
			      &pwm_mcux_flexio_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PWM_MCUX_FLEXIO_PWM_INIT)
