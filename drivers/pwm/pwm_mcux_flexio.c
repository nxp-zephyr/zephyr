/*
 * Copyright (c) 2023, NXP
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

#define CHANNEL_COUNT				FLEXIO_TIMCMP_COUNT
#define FLEXIO_PWM_TIMER_CMP_MAX_VALUE		(0xFFFFU)
#define FLEXIO_PWM_TIMCMP_CMP_UPPER_MASK	(0x0000FF00UL)
#define FLEXIO_PWM_TIMCMP_CMP_LOWER_MASK	(0x000000FFUL)
#define FLEXIO_PWM_TIMCMP_CMP_UPPER_SHIFT       (0x8U)
#define FLEXIO_PWM_TIMCMP_CMP_LOWER_SHIFT       (0x0U)
#define FLEXIO_PWM_TIMCMP_CMP_UPPER_WIDTH       (0x8U)
#define FLEXIO_PWM_TIMCMP_CMP_LOWER_WIDTH       (0x8U)
#define FLEXIO_PWM_TIMCMP_CMP_UPPER(x)          (((uint32_t)(((uint32_t)(x)) << FLEXIO_PWM_TIMCMP_CMP_UPPER_SHIFT)) & FLEXIO_PWM_TIMCMP_CMP_UPPER_MASK)
#define FLEXIO_PWM_TIMCMP_CMP_LOWER(x)          (((uint32_t)(((uint32_t)(x)) << FLEXIO_PWM_TIMCMP_CMP_LOWER_SHIFT)) & FLEXIO_PWM_TIMCMP_CMP_LOWER_MASK)

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

struct flexio_pwm_channel_config
{
	FLEXIO_Type *flexio_base;
	/** Flexio used timer index */
	uint8_t timer_id;
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
};

struct pwm_mcux_flexio_config {
	const struct device *flexio_dev;
	const struct nxp_flexio_child *child;
	const struct pinctrl_dev_config *pincfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
};

struct pwm_mcux_flexio_data {
	uint32_t period_cycles;
	struct flexio_pwm_channel_config *channel_cfg;
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

static int mcux_flexio_get_prescaler_div(const struct device *dev)
{
	struct pwm_mcux_flexio_data *data = dev->data;
	int ret = 1;

	switch (data->channel_cfg->prescaler) {
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
	struct pwm_mcux_flexio_data *data = dev->data;
	flexio_timer_config_t timerConfig;

	/* Check received parameters for sanity */
	if (channel >= CHANNEL_COUNT) {
		LOG_ERR("Invalid channel");
		return -EINVAL;
	}

	if (period_cycles == 0 || pulse_cycles == 0) {
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
	
	if(pulse_cycles >= period_cycles) {
		LOG_ERR("Duty cycle cannot be equal or greater than 100 percent");
		return -EINVAL;
	}

	if ((flags & PWM_POLARITY_INVERTED) == 0) {
		data->channel_cfg->polarity = FLEXIO_PWM_ACTIVE_HIGH;
	} else {
		data->channel_cfg->polarity = FLEXIO_PWM_ACTIVE_LOW;
	}

	if (data->channel_cfg->polarity == FLEXIO_PWM_ACTIVE_HIGH) {
		timerConfig.timerOutput = kFLEXIO_TimerOutputOneNotAffectedByReset;
		timerConfig.timerMode = kFLEXIO_TimerModeDual8BitPWM;

	}
	else {
		timerConfig.timerOutput = kFLEXIO_TimerOutputZeroNotAffectedByReset;
		timerConfig.timerMode = kFLEXIO_TimerModeDual8BitPWMLow;
	}

	timerConfig.timerMode = kFLEXIO_TimerModeDual8BitPWM;

	data->channel_cfg->duty_cycle = pulse_cycles;//100 * pulse_cycles / period_cycles;

	data->channel_cfg->period = period_cycles;

	timerConfig.timerCompare = ((uint8_t)(data->channel_cfg->duty_cycle - 1U)) |
	       	((uint8_t)(data->channel_cfg->period - data->channel_cfg->duty_cycle - 1U) << FLEXIO_PWM_TIMCMP_CMP_UPPER_WIDTH);
#if 1
	timerConfig.timerDecrement = data->channel_cfg->prescaler;
	timerConfig.timerStop = kFLEXIO_TimerStopBitDisabled;
	timerConfig.timerEnable = kFLEXIO_TimerEnabledAlways;
        timerConfig.timerDisable = kFLEXIO_TimerDisableNever;	
	timerConfig.timerStart = kFLEXIO_TimerStartBitDisabled;
	timerConfig.timerReset = kFLEXIO_TimerResetNever;
	timerConfig.triggerSource = kFLEXIO_TimerTriggerSourceInternal;
	
	/* Enable the pin out for the selected timer */
	timerConfig.pinConfig = FLEXIO_PWM_TIMER_PIN_OUTPUT_ENABLE;
	timerConfig.pinPolarity = data->channel_cfg->polarity;

	/* Select the pin that the selected timer will output the signal on */
        timerConfig.pinSelect = data->channel_cfg->pin_id;
#endif	
	FLEXIO_SetTimerConfig(data->channel_cfg->flexio_base, data->channel_cfg->timer_id, &timerConfig);

       	/* Disable pin override if active to support channels working in cases not 0% 100% */
        if (Flexio_Pwm_Ip_GetPinOverride(data->channel_cfg->flexio_base, data->channel_cfg->pin_id))
        {
            Flexio_Pwm_Ip_ConfigurePinOverride(data->channel_cfg->flexio_base, data->channel_cfg->pin_id, false);
        }
    
#if 0
	/* Check received parameters */
    
	if(FLEXIO_PWM_IP_TIMER_CMP_MAX_VALUE >= DutyCycle) {
		LOG_ERR("Duty cycle is out of range");
		return -EINVAL;
	}

	if(FLEXIO_PWM_IP_TIMER_CMP_MAX_VALUE >= (uint16)(Period - DutyCycle)) {
		LOG_ERR("Low cycle is out of range");
		return -EINVAL;
	}


	Flexio_Pwm_Ip_SetTimerInitLogic(Base, Channel, FLEXIO_PWM_IP_TIMER_INIT_HIGH);
	Flexio_Pwm_Ip_SetTimerMode(Base, Channel, FLEXIO_PWM_IP_TIMER_PWM_HIGH);

        /* Configure the timer comparator with duty and period values */
        Flexio_Pwm_Ip_SetLowerValue(Base, Channel, (uint8)(DutyCycle - 1U));
        Flexio_Pwm_Ip_SetUpperValue(Base, Channel, (uint8)(Period - DutyCycle - 1U));
#endif
	return 0;
}

static int mcux_flexio_pwm_get_cycles_per_sec(const struct device *dev,
					       uint32_t channel,
					       uint64_t *cycles)
{
	int prescaler_div = 1;
	struct pwm_mcux_flexio_data *data = dev->data;

	prescaler_div = mcux_flexio_get_prescaler_div(dev);
	*cycles = (uint64_t)((data->flexio_clk *2) / ((data->channel_cfg->period) * (prescaler_div)));
	//printk("\ndata->flexio_clk = %d period=%d prescaler_div =%d cycles=%lld\n",data->flexio_clk, data->channel_cfg->period,prescaler_div, *cycles);
	
	return 0;
}

static int mcux_flexio_pwm_init(const struct device *dev)
{
	const struct pwm_mcux_flexio_config *config = dev->config;
	struct pwm_mcux_flexio_data *data = dev->data;
	flexio_timer_config_t timerConfig;
	FLEXIO_Type *flexio_base = (FLEXIO_Type *)(data->channel_cfg->flexio_base);
	int err;

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
	
	err = nxp_flexio_child_attach(config->flexio_dev, config->child);
	if (err < 0) {
		return err;
	}

	/* Reset timer settings */
	(void)memset(&timerConfig, 0, sizeof(timerConfig));
	FLEXIO_SetTimerConfig(flexio_base, data->channel_cfg->timer_id, &timerConfig);

	/* Reset the value driven on the corresponding pin */
	Flexio_Pwm_Ip_SetPinLevel(flexio_base, data->channel_cfg->pin_id, false);
	Flexio_Pwm_Ip_ConfigurePinOverride(flexio_base, data->channel_cfg->pin_id, false);

	/* Timer output is logic one when enabled and is not affected by timer reset */
	timerConfig.timerOutput = kFLEXIO_TimerOutputOneNotAffectedByReset;

	/* Set the timer mode to dual 8bit counter PWM high */
	timerConfig.timerMode = kFLEXIO_TimerModeDual8BitPWM;
	
	/* Timer scaling factor w.r.t Flexio Clock */
	timerConfig.timerDecrement = data->channel_cfg->prescaler;

	timerConfig.timerCompare = ((uint8_t)(data->channel_cfg->duty_cycle - 1U)) |
	       	((uint8_t)(data->channel_cfg->period - data->channel_cfg->duty_cycle - 1U) 
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
	timerConfig.pinPolarity = data->channel_cfg->polarity;

	/* Select the pin that the selected timer will output the signal on */
        timerConfig.pinSelect = data->channel_cfg->pin_id;

	FLEXIO_SetTimerConfig(flexio_base, data->channel_cfg->timer_id, &timerConfig);

	return 0;
}

static const struct pwm_driver_api pwm_mcux_flexio_driver_api = {
	.set_cycles = mcux_flexio_pwm_set_cycles,
	.get_cycles_per_sec = mcux_flexio_pwm_get_cycles_per_sec,
};

#define PWM_MCUX_FLEXIO_PWM_INIT(n)								\
	PINCTRL_DT_INST_DEFINE(n);								\
												\
	static struct flexio_pwm_channel_config  flexio_pwm_channel_##n = {			\
                .flexio_base = (FLEXIO_Type *)DT_REG_ADDR(DT_INST_PARENT(n)),			\
                .timer_id = DT_INST_PROP(n, timer_id),						\
                .pin_id = DT_INST_PROP(n, pin_id),						\
                .prescaler = DT_INST_PROP(n, prescaler),					\
                .period = DT_INST_PROP(n, period),						\
		.duty_cycle = DT_INST_PROP(n, duty_cycle),					\
		.polarity = DT_INST_PROP(n, polarity),						\
        };											\
												\
	static const struct nxp_flexio_child mcux_flexio_pwm_child_##n = {			\
                .isr = NULL,									\
                .user_data = NULL,								\
                .res = {									\
                        .shifter_index = NULL,							\
                        .shifter_count = 0,							\
                        .timer_index = &flexio_pwm_channel_##n.timer_id,			\
                        .timer_count = 1							\
                }										\
        };                									\
												\
	static struct pwm_mcux_flexio_data pwm_mcux_flexio_data_##n = {				\
		.channel_cfg = &flexio_pwm_channel_##n,						\
	};											\
												\
	static const struct pwm_mcux_flexio_config pwm_mcux_flexio_config_##n = {		\
		.flexio_dev = DEVICE_DT_GET(DT_INST_PARENT(n)),					\
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),					\
		.child = &mcux_flexio_pwm_child_##n,						\
		.clock_dev = DEVICE_DT_GET(DT_CLOCKS_CTLR(DT_INST_PARENT(n))),			\
		.clock_subsys = (clock_control_subsys_t)DT_CLOCKS_CELL(DT_INST_PARENT(n), name),\
	};											\
												\
	DEVICE_DT_INST_DEFINE(n,								\
			      mcux_flexio_pwm_init,						\
			      NULL,								\
			      &pwm_mcux_flexio_data_##n,					\
			      &pwm_mcux_flexio_config_##n,					\
			      POST_KERNEL, CONFIG_PWM_INIT_PRIORITY,				\
			      &pwm_mcux_flexio_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PWM_MCUX_FLEXIO_PWM_INIT)
