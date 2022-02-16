/*
 * Copyright (c) 2022 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <drivers/sdhc.h>
#include <sd/sd.h>
#include <sd/sdmmc.h>
#include <sd/sd_spec.h>
#include <logging/log.h>
#include <sys/byteorder.h>
#include <drivers/disk.h>

#include "sd_utils.h"

LOG_MODULE_DECLARE(sd, CONFIG_SD_LOG_LEVEL);

/* Checks SD status return codes */
static inline int sdmmc_check_response(struct sdhc_command *cmd)
{
	if (cmd->response_type == SDHC_RSP_TYPE_R1) {
		return (cmd->response[0U] & SDHC_R1_ERR_FLAGS);
	}
	return 0;
}

/* Helper to send SD app command */
static int sdmmc_app_command(struct sd_card *card, int relative_card_address)
{
	struct sdhc_command cmd = {0};
	int ret;

	cmd.opcode = SD_APP_CMD;
	cmd.arg = relative_card_address << 16U;
	cmd.response_type = SDHC_RSP_TYPE_R1;
	cmd.timeout_ms = CONFIG_SD_CMD_TIMEOUT;
	ret = sdhc_request(card->sdhc, &cmd, NULL);
	if (ret) {
		/* We want to retry transmission */
		return SD_RETRY;
	}
	ret = sdmmc_check_response(&cmd);
	if (ret) {
		LOG_WRN("SD app command failed with R1 response of 0x%X",
			cmd.response[0]);
		return -EIO;
	}
	/* Check application command flag to determine if card is ready for APP CMD */
	if (cmd.response[0U] & SDHC_R1_APP_CMD) {
		return 0;
	}
	/* Command succeeded, but card not ready for app command. No APP CMD support */
	return -ENOTSUP;
}

/* Sends OCR to card, performing voltage switch if possible. */
static int sdmmc_send_ocr(struct sd_card *card)
{
	struct sdhc_command cmd;
	int ret;
	int retries = 0;
	uint32_t ocr_arg = 0U;
	bool busy = true;
	/* Send SD app command */
	ret = sdmmc_app_command(card, 0U);
	if (ret) {
		/* Force a retry */
		return SD_RETRY;
	}
	/* Initial query ACMD41, get voltage window */
	cmd.opcode = SD_APP_SEND_OP_COND;
	cmd.arg = 0;
	cmd.response_type = SDHC_RSP_TYPE_R3;
	cmd.timeout_ms = CONFIG_SD_CMD_TIMEOUT;
	ret = sdhc_request(card->sdhc, &cmd, NULL);
	if (ret) {
		/* Transfer failed. Do not retry. */
		return ret;
	}
	if (card->flags & SDHC_SDHC_FLAG) {
		/* High capacity card. See if host supports 1.8V */
		if (card->host_props.host_caps.vol_180_support) {
			ocr_arg |= SD_OCR_SWITCH_18_REQ_FLAG;
		}
		/* Set host high capacity support flag */
		ocr_arg |= SD_OCR_HOST_CAP_FLAG;
	}
	/* Set voltage window */
	if (card->host_props.host_caps.vol_300_support) {
		ocr_arg |= SD_OCR_VDD29_30FLAG;
	}
	ocr_arg |= (SD_OCR_VDD32_33FLAG | SD_OCR_VDD33_34FLAG);
	/* Send initialization ACMD41 */
	while (busy && (retries++) < CONFIG_SD_OCR_RETRY_COUNT) {
		ret = sdmmc_app_command(card, 0U);
		if (ret == SD_RETRY) {
			/* Retry */
			continue;
		} else if (ret) {
			return ret;
		}
		cmd.arg = ocr_arg;
		ret = sdhc_request(card->sdhc, &cmd, NULL);
		if (ret) {
			/* OCR failed */
			return ret;
		}
		/*
		 * Check to see if card is busy with power up. PWR_BUSY
		 * flag will be cleared when card finishes power up sequence
		 */
		busy = !(cmd.response[0U] & SD_OCR_PWR_BUSY_FLAG);
		if (busy) {
			/* Delay to allow card to power up.
			 * Delay may not be longer than 50ms
			 */
			sd_delay(10);
		}
	}
	if (retries >= CONFIG_SD_OCR_RETRY_COUNT) {
		/* OCR timed out */
		return -ETIMEDOUT;
	}
	LOG_DBG("SDMMC responded to ACMD41 after %d attempts", retries);
	/* Check SD high capacity and 1.8V support flags */
	if (cmd.response[0U] & SD_OCR_CARD_CAP_FLAG) {
		card->flags |= SDHC_HIGH_CAPACITY_FLAG;
	}
	if (cmd.response[0U] & SD_OCR_SWITCH_18_ACCEPT_FLAG) {
		LOG_DBG("Card supports 1.8V signaling");
		card->flags |= SDHC_1800MV_FLAG;
	}
	/* Check OCR voltage window */
	if (cmd.response[0U] & SD_OCR_VDD29_30FLAG) {
		card->flags |= SDHC_3000MV_FLAG;
	}
	/* Save OCR */
	card->ocr = cmd.response[0U];
	return 0;
}

/*
 * Implements signal voltage switch procedure described in section 3.6.1 of
 * SD specification.
 */
static int sdmmc_switch_voltage(struct sd_card *card)
{
	int ret, sd_clock;
	struct sdhc_command cmd = {0};

	/* Check to make sure card supports 1.8V */
	if (!(card->flags & SDHC_1800MV_FLAG)) {
		/* Do not attempt to switch voltages */
		LOG_WRN("SD card reports as SDHC/SDXC, but does not support 1.8V");
		return 0;
	}
	/* Send CMD11 to request a voltage switch */
	cmd.opcode = SD_VOL_SWITCH;
	cmd.arg = 0U;
	cmd.response_type = SDHC_RSP_TYPE_R1;
	cmd.timeout_ms = CONFIG_SD_CMD_TIMEOUT;
	ret = sdhc_request(card->sdhc, &cmd, NULL);
	if (ret) {
		LOG_DBG("CMD11 failed");
		return ret;
	}
	/* Check R1 response for error */
	ret = sdmmc_check_response(&cmd);
	if (ret) {
		LOG_DBG("SD response to CMD11 indicates error");
		return ret;
	}
	/*
	 * Card should drive CMD and DAT[3:0] signals low at the next clock
	 * cycle. Some cards will only drive these
	 * lines low briefly, so we should check as soon as possible
	 */
	if (!(sdhc_card_busy(card->sdhc))) {
		/* Delay 1ms to allow card to drive lines low */
		sd_delay(1);
		if (!sdhc_card_busy(card->sdhc)) {
			/* Card did not drive CMD and DAT lines low */
			LOG_DBG("Card did not drive DAT lines low");
			return -EAGAIN;
		}
	}
	/*
	 * Per SD spec (section "Timing to Switch Signal Voltage"),
	 * host must gate clock at least 5ms.
	 */
	sd_clock = card->bus_io.clock;
	card->bus_io.clock = 0;
	ret = sdhc_set_io(card->sdhc, &card->bus_io);
	if (ret) {
		LOG_DBG("Failed to gate SD clock");
		return ret;
	}
	/* Now that clock is gated, change signal voltage */
	card->bus_io.signal_voltage = SD_VOL_1_8_V;
	ret = sdhc_set_io(card->sdhc, &card->bus_io);
	if (ret) {
		LOG_DBG("Failed to switch SD host to 1.8V");
		return ret;
	}
	sd_delay(10); /* Gate for 10ms, even though spec requires 5 */
	/* Restart the clock */
	card->bus_io.clock = sd_clock;
	ret = sdhc_set_io(card->sdhc, &card->bus_io);
	if (ret) {
		LOG_ERR("Failed to restart SD clock");
		return ret;
	}
	/*
	 * If SD does not drive at least one of
	 * DAT[3:0] high within 1ms, switch failed
	 */
	sd_delay(1);
	if (sdhc_card_busy(card->sdhc)) {
		LOG_DBG("Card failed to switch voltages");
		return -EAGAIN;
	}
	card->card_voltage = SD_VOL_1_8_V;
	LOG_INF("Card switched to 1.8V signaling");
	return 0;
}

/*
 * Initializes SDMMC card. Note that the common SD function has already
 * sent CMD0 and CMD8 to the card at function entry.
 */
int sdmmc_card_init(struct sd_card *card)
{
	int ret;

	/* Send SD OCR to card to initialize it */
	ret = sd_retry(sdmmc_send_ocr, card, CONFIG_SD_OCR_RETRY_COUNT);
	if (ret) {
		LOG_ERR("Failed to query card OCR");
		return ret;
	}
	/*
	 * If card is high capacity (SDXC or SDHC), and supports 1.8V signaling,
	 * switch to new signal voltage using "signal voltage switch procedure"
	 * described in SD specification
	 */
	if ((card->flags & SDHC_1800MV_FLAG) &&
		(card->host_props.host_caps.vol_180_support)) {
		ret = sdmmc_switch_voltage(card);
		if (ret) {
			/* Disable host support for 1.8 V */
			card->host_props.host_caps.vol_180_support = false;
			/*
			 * The host or SD card may have already switched to
			 * 1.8V. Return SD_RESTART to indicate
			 * negotiation should be restarted.
			 */
			card->status = CARD_ERROR;
			return SD_RESTART;
		}
	}
}
