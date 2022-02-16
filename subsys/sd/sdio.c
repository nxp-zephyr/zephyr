/*
 * Copyright (c) 2022 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include <zephyr.h>
#include <drivers/sdhc.h>
#include <sd/sd.h>
#include <sd/sdio.h>
#include <sd/sd_spec.h>
#include <logging/log.h>

#include "sd_utils.h"

LOG_MODULE_DECLARE(sd, CONFIG_SD_LOG_LEVEL);

/*
 * Sends second CMD5 command to SDIO card.
 * The CMD5 command will initialize an SDIO card
 */
static int sdio_send_ocr(struct sd_card *card)
{
	struct sdhc_command cmd = {0};
	int ret;
	uint32_t ocr_arg = 0U;
	bool io_rdy = false;

	/* 3.3 volts is supported by default, so set voltage window */
	ocr_arg |= (SDIO_OCR_VDD32_33FLAG | SDIO_OCR_VDD33_34FLAG);
	if (card->host_props.host_caps.vol_300_support) {
		/* Indicate 3.0V support */
		ocr_arg |= SDIO_OCR_VDD29_30FLAG;
	}
	if (card->host_props.host_caps.vol_180_support) {
		/* Request the card to switch to 1.8V */
		ocr_arg |= SDIO_OCR_180_VOL_FLAG;
	}
	cmd.arg = ocr_arg;
	/* Send OCR to SDIO card */
	/* Spec says we must give a 1 second timeout for command */
	cmd.timeout_ms = 1000;
	ret = sdhc_request(card->sdhc, &cmd, NULL);
	if (ret) {
		LOG_ERR("SDIO card error on CMD 5");
		/* Retry */
		return SD_RETRY;
	}
	/* Check if card I/O is ready, if not retry command */
	io_rdy = ((cmd.response[0] & SDIO_OCR_IO_READY_FLAG) != 0U);
	if (!io_rdy) {
		/* Retry */
		return SD_RETRY;
	}
	/* Check memory present flag */
	if (cmd.response[0U] & SDIO_OCR_MEM_PRESENT_FLAG) {
		card->type = CARD_COMBO;
	} else {
		card->type = CARD_SDIO;
	}
	/* Check to see what voltage the card supports */
	if (cmd.response[0U] & SDIO_OCR_180_VOL_FLAG) {
		card->flags |= SDHC_1800MV_FLAG;
	} else if (cmd.response[0U] & SDIO_OCR_VDD29_30FLAG) {
		card->flags |= SDHC_3000MV_FLAG;
	}
	return 0;
}


int sdio_card_init(struct sd_card *card)
{
	int ret;

	ret = sd_retry(sdio_send_ocr, card, CONFIG_SD_OCR_RETRY_COUNT);
	if (ret) {
		return ret;
	}
	/* If card is combo card, send app command 41 */
	/* CMD11 */
	/* Voltage switch sequence */
	/* Check mem, if set do CMD2 */
	/* CMD3 */
	return ret;
}

/**
 * @brief Enable SDIO function
 *
 * Enables SDIO function
 * @param card: card to write data to
 * @param sdio_func: SDIO function number to enable
 */
int sdio_func_enable(struct sd_card *card, int sdio_func)
{
	return -ENOTSUP;
}

/**
 * @brief write data to SDIO registers
 *
 * Writes data to SDIO registers
 * @param card: card to write data to
 * @param sdio_func: SDIO function number to use
 * @param reg: SDIO register address to write to
 * @param data: buffer to write from
 * @param count: number of registers to read
 */
int sdio_io_write(struct sd_card *card, int sdio_func, uint32_t reg, uint8_t *data, int count)
{
	return -ENOTSUP;
}

/**
 * @brief Read data from SDIO registers
 *
 * Reads data from SDIO registers
 * @param card: card to read data from
 * @param sdio_func: SDIO function number to use
 * @param reg: SDIO register address to read from
 * @param data: buffer to read into
 * @param count: number of registers to read
 */
int sdio_io_read(struct sd_card *card, int sdio_func, uint32_t reg, uint8_t *data, int count)
{
	return -ENOTSUP;
}
