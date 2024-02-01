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
#include "../misc/mcux_flexio/mcux_flexio.h"


LOG_MODULE_REGISTER(pwm_mcux_flexio, CONFIG_PWM_LOG_LEVEL);

#define CHANNEL_COUNT FLEXIO_TIMCMP_COUNT
#define FLEXIO_PWM_TIMER_CMP_MAX_VALUE		(0x0100U)
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
	const struct mcux_flexio_child *child;
	const struct pinctrl_dev_config *pincfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
};

struct pwm_mcux_flexio_data {
	//const struct device *dev;
	uint32_t period_cycles;
	//uint32_t event_number[CHANNEL_COUNT];
	struct flexio_pwm_channel_config *channel_cfg;
	uint32_t flexio_clk;
};

static void Flexio_Pwm_Ip_SetUpperValue(FLEXIO_Type * Base, uint8_t Timer, uint8_t Value)
{
    Base->TIMCMP[Timer] = (Base->TIMCMP[Timer] & ~FLEXIO_PWM_TIMCMP_CMP_UPPER_MASK)
                        | FLEXIO_PWM_TIMCMP_CMP_UPPER(Value);
}

static void Flexio_Pwm_Ip_SetLowerValue(FLEXIO_Type * Base, uint8_t Timer, uint8_t Value)
{
    Base->TIMCMP[Timer] = (Base->TIMCMP[Timer] & ~FLEXIO_PWM_TIMCMP_CMP_LOWER_MASK)
                        | FLEXIO_PWM_TIMCMP_CMP_LOWER(Value);
}

static void Flexio_Pwm_Ip_SetPinLevel(FLEXIO_Type * Base, uint8_t Pin, bool Level)
{
    Base->PINOUTD = (Base->PINOUTD & ~((uint32_t)((uint32_t)1U << Pin))) | (FLEXIO_PINOUTD_OUTD((uint32_t)((true == Level) ? (uint32_t)0x1U : (uint32_t)0x0U) << Pin));
     Base->PINOUTE = (Base->PINOUTE & ~((uint32)((uint32)1U << Pin))) | FLEXIO_PINOUTE_OUTE((uint32)((true == true) ? (uint32)0x1U : (uint32)0x0U) << Pin);
}

static void Flexio_Pwm_Ip_ConfigurePinOverride(FLEXIO_Type * Base, uint8_t Pin, bool Enabled)
{
     Base->PINOUTE = (Base->PINOUTE & ~((uint32)((uint32)1U << Pin))) | FLEXIO_PINOUTE_OUTE((uint32)((true == Enabled) ? (uint32)0x1U : (uint32)0x0U) << Pin);
}

static void Flexio_Pwm_Ip_SetTimerMode(FLEXIO_Type *Base, uint8_t Timer, uint8_t TimerMode)
{
    Base->TIMCTL[Timer] = (Base->TIMCTL[Timer] & ~FLEXIO_TIMCTL_TIMOD_MASK) | FLEXIO_TIMCTL_TIMOD(TimerMode);
}

static void Flexio_Pwm_Ip_SetTimerCfg(FLEXIO_Type *Base, uint8_t Timer)
{
	Base->TIMCFG[Timer] = (Base->TIMCFG[Timer] & ~FLEXIO_TIMCFG_TIMOUT_MASK) | FLEXIO_TIMCFG_TIMOUT(kFLEXIO_TimerOutputOneNotAffectedByReset);
	Base->TIMCFG[Timer] = (Base->TIMCFG[Timer] & ~FLEXIO_TIMCFG_TSTOP_MASK) | FLEXIO_TIMCFG_TSTOP(kFLEXIO_TimerStopBitDisabled);
	Base->TIMCFG[Timer] = (Base->TIMCFG[Timer] & ~FLEXIO_TIMCFG_TIMENA_MASK) | FLEXIO_TIMCFG_TIMENA(kFLEXIO_TimerEnabledAlways);
	Base->TIMCFG[Timer] = (Base->TIMCFG[Timer] & ~FLEXIO_TIMCFG_TIMDIS_MASK) | FLEXIO_TIMCFG_TIMDIS(kFLEXIO_TimerDisableNever);
	Base->TIMCFG[Timer] = (Base->TIMCFG[Timer] & ~FLEXIO_TIMCFG_TSTART_MASK) | FLEXIO_TIMCFG_TSTART(kFLEXIO_TimerStartBitDisabled);
	Base->TIMCFG[Timer] = (Base->TIMCFG[Timer] & ~FLEXIO_TIMCFG_TIMRST_MASK) | FLEXIO_TIMCFG_TIMRST(kFLEXIO_TimerResetNever);
	Base->TIMCFG[Timer] = (Base->TIMCFG[Timer] & ~FLEXIO_TIMCTL_TRGSRC_MASK) | FLEXIO_TIMCTL_TRGSRC(kFLEXIO_TimerTriggerSourceInternal);
	Base->TIMCTL[Timer] = (Base->TIMCTL[Timer] & ~FLEXIO_TIMCTL_TIMOD_MASK) | FLEXIO_TIMCTL_TIMOD(kFLEXIO_TimerModeDual8BitPWM);
}

static void Flexio_Pwm_Ip_SetTimerInitLogic(FLEXIO_Type *Base, uint8_t Timer, uint8_t TimerInitOut)
{
    Base->TIMCFG[Timer] = (Base->TIMCFG[Timer] & ~FLEXIO_TIMCFG_TIMOUT_MASK) | FLEXIO_TIMCFG_TIMOUT(TimerInitOut);
}

static void Flexio_Pwm_Ip_SetTimerPrescaler(FLEXIO_Type *Base, uint8_t Timer, uint8_t Prescaler)
{
    Base->TIMCFG[Timer] = (Base->TIMCFG[Timer] & ~FLEXIO_TIMCFG_TIMDEC_MASK) | FLEXIO_TIMCFG_TIMDEC(Prescaler);
}

static void Flexio_Pwm_Ip_SetTimerPinOutput(FLEXIO_Type * Base, uint8_t Timer, enum flexio_pwm_timer_pin PinMode)
{
    Base->TIMCTL[Timer] = (Base->TIMCTL[Timer] & ~FLEXIO_TIMCTL_PINCFG_MASK) | FLEXIO_TIMCTL_PINCFG(PinMode);
}

static void Flexio_Pwm_Ip_SetTimerPinPolarity(FLEXIO_Type * Base, uint8_t Timer, enum flexio_pwm_polarity Polarity)
{
    Base->TIMCTL[Timer] = (Base->TIMCTL[Timer] & ~FLEXIO_TIMCTL_PINPOL_MASK) | FLEXIO_TIMCTL_PINPOL(Polarity);
}

static void Flexio_Pwm_Ip_SetTimerPin(FLEXIO_Type * Base, uint8_t Timer, uint8_t Pin)
{
    Base->TIMCTL[Timer] = (Base->TIMCTL[Timer] & ~FLEXIO_TIMCTL_PINSEL_MASK) | FLEXIO_TIMCTL_PINSEL(Pin);
}

static void Flexio_Pwm_Ip_InitTimerPin(FLEXIO_Type * const Base,
                                       const struct flexio_pwm_channel_config * const channel_cfg)
{
    /* Enable the pin out for the selected timer */
    Flexio_Pwm_Ip_SetTimerPinOutput(Base, channel_cfg->timer_id, FLEXIO_PWM_TIMER_PIN_OUTPUT_ENABLE);
    Flexio_Pwm_Ip_SetTimerPinPolarity(Base, channel_cfg->timer_id, channel_cfg->polarity);
    /* Select the pin that the selected timer will ouput the signal */
    Flexio_Pwm_Ip_SetTimerPin(Base, channel_cfg->timer_id, channel_cfg->pin_id);
}

static int mcux_flexio_pwm_set_cycles(const struct device *dev,
				       uint32_t channel, uint32_t period_cycles,
				       uint32_t pulse_cycles, pwm_flags_t flags)
{
	const struct pwm_mcux_flexio_config *config = dev->config;
	struct pwm_mcux_flexio_data *data = dev->data;
	uint8_t duty_cycle;

	if (channel >= CHANNEL_COUNT) {
		LOG_ERR("Invalid channel");
		return -EINVAL;
	}

	if (period_cycles == 0) {
		LOG_ERR("Channel can not be set to inactive level");
		return -ENOTSUP;
	}

	if ((flags & PWM_POLARITY_INVERTED) == 0) {
		data->channel_cfg->polarity = FLEXIO_PWM_ACTIVE_HIGH;
	} else {
		data->channel_cfg->polarity = FLEXIO_PWM_ACTIVE_LOW;
	}

	duty_cycle = 100 * pulse_cycles / period_cycles;

#if 0
	if (duty_cycle == 0) {
		SCT_Type *base = config->base;

		flexio_StopTimer(config->base, kflexio_Counter_U);

		/* Set the output to inactive State */
		if (data->channel[channel].level == kflexio_HighTrue) {
			base->OUTPUT &= ~(1UL << channel);
		} else {
			base->OUTPUT |= (1UL << channel);
		}

		return 0;
	}

	if (period_cycles != data->period_cycles[channel] &&
	    duty_cycle != data->channel[channel].dutyCyclePercent) {
		uint32_t clock_freq;
		uint32_t pwm_freq;

		data->period_cycles[channel] = period_cycles;

		if (clock_control_get_rate(config->clock_dev, config->clock_subsys,
					&clock_freq)) {
			return -EINVAL;
		}

		pwm_freq = (clock_freq / config->prescale) / period_cycles;

		if (pwm_freq == 0) {
			LOG_ERR("Could not set up pwm_freq=%d", pwm_freq);
			return -EINVAL;
		}

		flexio_StopTimer(config->base, kflexio_Counter_U);

		LOG_DBG("SETUP dutycycle to %u\n", duty_cycle);
		data->channel[channel].dutyCyclePercent = duty_cycle;
		if (flexio_SetupPwm(config->base, &data->channel[channel],
				     kflexio_EdgeAlignedPwm, pwm_freq,
				     clock_freq, &data->event_number[channel]) == kStatus_Fail) {
			LOG_ERR("Could not set up pwm");
			return -ENOTSUP;
		}

		flexio_StartTimer(config->base, kflexio_Counter_U);
	} else {
		data->period_cycles[channel] = period_cycles;
		flexio_UpdatePwmDutycycle(config->base, channel, duty_cycle,
					   data->event_number[channel]);
	}
#else
#ifdef SUMIT
	/* Check received parameters */
	if(FLEXIO_PWM_IP_CHANNEL_COUNT > Channel) {
		return -EINVAL;
	}
    
	if(FLEXIO_PWM_IP_TIMER_CMP_MAX_VALUE >= DutyCycle) {
		LOG_ERR("Duty cycle is out of range");
		return -EINVAL;
	}

	if(FLEXIO_PWM_IP_TIMER_CMP_MAX_VALUE >= (uint16)(Period - DutyCycle)) {
		LOG_ERR("Low cycle is out of range");
		return -EINVAL;
	}

	Flexio_Pwm_Ip_HwAddrType * const Base = Flexio_Pwm_Ip_aBasePtr[InstanceId];

	Flexio_Pwm_Ip_SetTimerInitLogic(Base, Channel, FLEXIO_PWM_IP_TIMER_INIT_HIGH);
	Flexio_Pwm_Ip_SetTimerMode(Base, Channel, FLEXIO_PWM_IP_TIMER_PWM_HIGH);

        /* Configure the timer comparator with duty and period values */
        Flexio_Pwm_Ip_SetLowerValue(Base, Channel, (uint8)(DutyCycle - 1U));
        Flexio_Pwm_Ip_SetUpperValue(Base, Channel, (uint8)(Period - DutyCycle - 1U));
#endif
#endif
	return 0;
}

static int mcux_flexio_pwm_get_cycles_per_sec(const struct device *dev,
					       uint32_t channel,
					       uint64_t *cycles)
{
#if 0
	const struct pwm_mcux_flexio_config *config = dev->config;
	uint32_t clock_freq;

	if (clock_control_get_rate(config->clock_dev, config->clock_subsys,
				&clock_freq)) {
		return -EINVAL;
	}

	*cycles = clock_freq / config->prescale;
#else
#ifdef SUMIT

    /* Check received parameters */
    if(FLEXIO_PWM_IP_CHANNEL_COUNT > channel) {
	return -EINVAL;
    }

    const Flexio_Pwm_Ip_HwAddrType * const Base = Flexio_Pwm_Ip_aBasePtr[InstanceId];
    /* Getting the period for a channel. */
    uint16 Period = (uint16)Flexio_Pwm_Ip_GetUpperValue(Base, channel) + (uint16)Flexio_Pwm_Ip_GetLowerValue(Base, channel) + (uint16)2U;

    *cycles = 1/Period;
#endif
#endif
	return 0;
}

static int mcux_flexio_pwm_init(const struct device *dev)
{
	const struct pwm_mcux_flexio_config *config = dev->config;
	struct pwm_mcux_flexio_data *data = dev->data;
	flexio_timer_config_t timerConfig;
	status_t status;
	int err;
	FLEXIO_Type *flexio_base = (FLEXIO_Type *)(data->channel_cfg->flexio_base);

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
	
	err = mcux_flexio_child_attach(config->flexio_dev, config->child);
	if (err < 0) {
		return err;
	}

	/* Reset timer settings */
	(void)memset(&timerConfig, 0, sizeof(timerConfig));
	FLEXIO_SetTimerConfig(data->channel_cfg->flexio_base, data->channel_cfg->timer_id, &timerConfig);

	/* Reset the value driven on the corresponding pin */
	Flexio_Pwm_Ip_SetPinLevel(data->channel_cfg->flexio_base, data->channel_cfg->pin_id, false);
	Flexio_Pwm_Ip_ConfigurePinOverride(data->channel_cfg->flexio_base, data->channel_cfg->pin_id, false);

	/* Timer output is logic one when enabled and is not affected by timer reset */
	Flexio_Pwm_Ip_SetTimerInitLogic(data->channel_cfg->flexio_base, data->channel_cfg->timer_id, FLEXIO_PWM_TIMER_INIT_HIGH);

	/* Set the timer mode to dual 8bit counter PWM high */
        Flexio_Pwm_Ip_SetTimerMode(data->channel_cfg->flexio_base, data->channel_cfg->timer_id, FLEXIO_PWM_TIMER_PWM_HIGH);

	Flexio_Pwm_Ip_SetTimerPrescaler(data->channel_cfg->flexio_base, data->channel_cfg->timer_id, data->channel_cfg->prescaler);

	/* Disable interrupts as per ASR requirements */
//	ret = Flexio_Pwm_Ip_UpdateInterruptMode(data->channel_cfg->flexio_base, data->channel_cfg->timer_id, FLEXIO_PWM_IP_IRQ_DISABLED);

	/* Configure the timer comparator with duty and period values */
        Flexio_Pwm_Ip_SetLowerValue(data->channel_cfg->flexio_base, data->channel_cfg->timer_id, (uint8_t)(data->channel_cfg->duty_cycle - 1U));
        Flexio_Pwm_Ip_SetUpperValue(data->channel_cfg->flexio_base, data->channel_cfg->timer_id, (uint8_t)(data->channel_cfg->period - data->channel_cfg->duty_cycle - 1U));
	 
	/* Transition and enable the timer in selected mode */
        //Flexio_Pwm_Ip_SetTimerMode(data->channel_cfg->flexio_base, data->channel_cfg->timer_id, FLEXIO_PWM_TIMER_PWM_HIGH);
        Flexio_Pwm_Ip_SetTimerCfg(data->channel_cfg->flexio_base, data->channel_cfg->timer_id);

	 /* Configure timer pin */
	Flexio_Pwm_Ip_InitTimerPin(data->channel_cfg->flexio_base, data->channel_cfg);
	
	return 0;
}

static const struct pwm_driver_api pwm_mcux_flexio_driver_api = {
	.set_cycles = mcux_flexio_pwm_set_cycles,
	.get_cycles_per_sec = mcux_flexio_pwm_get_cycles_per_sec,
};

#if 0

#define IS_S32(n) COND_CODE_1(CONFIG_SOC_S32K344, 1, 0)

#define FLEXIO_ADDR(n) COND_CODE_1(IS_S32(n), IP_FLEXIO_BASE, FLEXIO_BASE)

#define FLEXIO_INSTANCE_CHECK(idx, node_id)							\
	((DT_REG_ADDR(node_id) == FLEXIO_ADDR(idx)) ? idx : -1)

#define FLEXIO_GET_INSTANCE(node_id)			\
	LISTIFY(__DEBRACKET FLEXIO_INSTANCE_COUNT, FLEXIO_INSTANCE_CHECK, (|), node_id)

#define FLEXIO_PWM_PULSE_CONFIG(n)								\
	.pulse_info = (struct flexio_pwm_pulse_info *)&flexio_pwm_##n##_info,

#define FLEXIO_PWM_PULSE_GEN_CONFIG(n)								\
	const flexio_pwm_channel_config flexio_pwm_##n##_init[] = {				\
		DT_INST_FOREACH_CHILD_STATUS_OKAY(n, _FLEXIO_PWM_PULSE_GEN_CONFIG)		\
	};											\

	const struct flexio_pwm_pulse_info flexio_pwm_##n##_info = {				\
		.pwm_pulse_channels = ARRAY_SIZE(flexio_pwm_##n##_init),			\
		.pwm_info = (struct flexio_pwm_channel_config *)flexio_pwm_##n##_init,		\
	};
		.flexio_pwm = (FLEXIO_Type *)DT_REG_ADDR(DT_INST_PARENT(n)),			\

#endif




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
	static const struct mcux_flexio_child mcux_flexio_pwm_child_##n = {			\
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
