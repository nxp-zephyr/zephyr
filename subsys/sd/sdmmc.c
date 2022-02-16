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

static inline void sdmmc_decode_csd(struct sd_csd *csd,
	uint32_t *raw_csd, uint32_t *blk_cout, uint32_t *blk_size)
{
	uint32_t tmp_blk_cout, tmp_blk_size;

	csd->csd_structure = (uint8_t)((raw_csd[3U] &
		0xC0000000U) >> 30U);
	csd->read_time1 = (uint8_t)((raw_csd[3U] &
		0xFF0000U) >> 16U);
	csd->read_time2 = (uint8_t)((raw_csd[3U] &
		0xFF00U) >> 8U);
	csd->xfer_rate = (uint8_t)(raw_csd[3U] &
		0xFFU);
	csd->cmd_class = (uint16_t)((raw_csd[2U] &
		0xFFF00000U) >> 20U);
	csd->read_blk_len = (uint8_t)((raw_csd[2U] &
		0xF0000U) >> 16U);
	if (raw_csd[2U] & 0x8000U)
		csd->flags |= SD_CSD_READ_BLK_PARTIAL_FLAG;
	if (raw_csd[2U] & 0x4000U)
		csd->flags |= SD_CSD_READ_BLK_PARTIAL_FLAG;
	if (raw_csd[2U] & 0x2000U)
		csd->flags |= SD_CSD_READ_BLK_MISALIGN_FLAG;
	if (raw_csd[2U] & 0x1000U)
		csd->flags |= SD_CSD_DSR_IMPLEMENTED_FLAG;

	switch (csd->csd_structure) {
	case 0:
		csd->device_size = (uint32_t)((raw_csd[2U] &
			0x3FFU) << 2U);
		csd->device_size |= (uint32_t)((raw_csd[1U] &
			0xC0000000U) >> 30U);
		csd->read_current_min = (uint8_t)((raw_csd[1U] &
			0x38000000U) >> 27U);
		csd->read_current_max = (uint8_t)((raw_csd[1U] &
			0x7000000U) >> 24U);
		csd->write_current_min = (uint8_t)((raw_csd[1U] &
			0xE00000U) >> 20U);
		csd->write_current_max = (uint8_t)((raw_csd[1U] &
			0x1C0000U) >> 18U);
		csd->dev_size_mul = (uint8_t)((raw_csd[1U] &
			0x38000U) >> 15U);

		/* Get card total block count and block size. */
		tmp_blk_cout = ((csd->device_size + 1U) <<
			(csd->dev_size_mul + 2U));
		tmp_blk_size = (1U << (csd->read_blk_len));
		if (tmp_blk_size != SDMMC_DEFAULT_BLOCK_SIZE) {
			tmp_blk_cout = (tmp_blk_cout * tmp_blk_size);
			tmp_blk_size = SDMMC_DEFAULT_BLOCK_SIZE;
			tmp_blk_cout = (tmp_blk_cout / tmp_blk_size);
		}
		if (blk_cout)
			*blk_cout = tmp_blk_cout;
		if (blk_size)
			*blk_size = tmp_blk_size;
		break;
	case 1:
		tmp_blk_size = SDMMC_DEFAULT_BLOCK_SIZE;

		csd->device_size = (uint32_t)((raw_csd[2U] &
			0x3FU) << 16U);
		csd->device_size |= (uint32_t)((raw_csd[1U] &
			0xFFFF0000U) >> 16U);

		tmp_blk_cout = ((csd->device_size + 1U) * 1024U);
		if (blk_cout)
			*blk_cout = tmp_blk_cout;
		if (blk_size)
			*blk_size = tmp_blk_size;
		break;
	default:
		break;
	}

	if ((uint8_t)((raw_csd[1U] & 0x4000U) >> 14U))
		csd->flags |= SD_CSD_ERASE_BLK_EN_FLAG;
	csd->erase_size = (uint8_t)((raw_csd[1U] &
		0x3F80U) >> 7U);
	csd->write_prtect_size = (uint8_t)(raw_csd[1U] &
		0x7FU);
	csd->write_speed_factor = (uint8_t)((raw_csd[0U] &
		0x1C000000U) >> 26U);
	csd->write_blk_len = (uint8_t)((raw_csd[0U] &
		0x3C00000U) >> 22U);
	if ((uint8_t)((raw_csd[0U] & 0x200000U) >> 21U))
		csd->flags |= SD_CSD_WRITE_BLK_PARTIAL_FLAG;
	if ((uint8_t)((raw_csd[0U] & 0x8000U) >> 15U))
		csd->flags |= SD_CSD_FILE_FMT_GRP_FLAG;
	if ((uint8_t)((raw_csd[0U] & 0x4000U) >> 14U))
		csd->flags |= SD_CSD_COPY_FLAG;
	if ((uint8_t)((raw_csd[0U] & 0x2000U) >> 13U))
		csd->flags |=
			SD_CSD_PERMANENT_WRITE_PROTECT_FLAG;
	if ((uint8_t)((raw_csd[0U] & 0x1000U) >> 12U))
		csd->flags |=
			SD_CSD_TMP_WRITE_PROTECT_FLAG;
	csd->file_fmt = (uint8_t)((raw_csd[0U] & 0xC00U) >> 10U);
}

static inline void sdmmc_decode_scr(struct sd_scr *scr,
	uint32_t *raw_scr, uint32_t *version)
{
	uint32_t tmp_version = 0;

	scr->scr_structure = (uint8_t)((raw_scr[0U] & 0xF0000000U) >> 28U);
	scr->sd_spec = (uint8_t)((raw_scr[0U] & 0xF000000U) >> 24U);
	if ((uint8_t)((raw_scr[0U] & 0x800000U) >> 23U))
		scr->flags |= SD_SCR_DATA_STATUS_AFTER_ERASE;
	scr->sd_sec = (uint8_t)((raw_scr[0U] & 0x700000U) >> 20U);
	scr->sd_width = (uint8_t)((raw_scr[0U] & 0xF0000U) >> 16U);
	if ((uint8_t)((raw_scr[0U] & 0x8000U) >> 15U))
		scr->flags |= SD_SCR_SPEC3;
	scr->sd_ext_sec = (uint8_t)((raw_scr[0U] & 0x7800U) >> 10U);
	scr->cmd_support = (uint8_t)(raw_scr[0U] & 0x3U);
	scr->rsvd = raw_scr[1U];
	/* Get specification version. */
	switch (scr->sd_spec) {
	case 0U:
		tmp_version = SD_SPEC_VER1_0;
		break;
	case 1U:
		tmp_version = SD_SPEC_VER1_1;
		break;
	case 2U:
		tmp_version = SD_SPEC_VER2_0;
		if (scr->flags & SD_SCR_SPEC3) {
			tmp_version = SD_SPEC_VER3_0;
		}
		break;
	default:
		break;
	}

	if (version && tmp_version)
		*version = tmp_version;
}

static inline void sdmmc_decode_cid(struct sd_cid *cid,
	uint32_t *raw_cid)
{
	cid->manufacturer = (uint8_t)((raw_cid[3U] & 0xFF000000U) >> 24U);
	cid->application = (uint16_t)((raw_cid[3U] & 0xFFFF00U) >> 8U);

	cid->name[0U] = (uint8_t)((raw_cid[3U] & 0xFFU));
	cid->name[1U] = (uint8_t)((raw_cid[2U] & 0xFF000000U) >> 24U);
	cid->name[2U] = (uint8_t)((raw_cid[2U] & 0xFF0000U) >> 16U);
	cid->name[3U] = (uint8_t)((raw_cid[2U] & 0xFF00U) >> 8U);
	cid->name[4U] = (uint8_t)((raw_cid[2U] & 0xFFU));

	cid->version = (uint8_t)((raw_cid[1U] & 0xFF000000U) >> 24U);

	cid->ser_num = (uint32_t)((raw_cid[1U] & 0xFFFFFFU) << 8U);
	cid->ser_num |= (uint32_t)((raw_cid[0U] & 0xFF000000U) >> 24U);

	cid->date = (uint16_t)((raw_cid[0U] & 0xFFF00U) >> 8U);
}

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

/* Reads card identification register, and decodes it */
static int sdmmc_read_cid(struct sd_card *card)
{
	struct sdhc_command cmd = {0};
	int ret;

	cmd.opcode = SD_ALL_SEND_CID;
	cmd.arg = 0;
	cmd.response_type = SDHC_RSP_TYPE_R2;
	cmd.timeout_ms = CONFIG_SD_CMD_TIMEOUT;

	ret = sdhc_request(card->sdhc, &cmd, NULL);
	if (ret) {
		LOG_DBG("CMD2 failed");
		return ret;
	}
	/* Decode SD CID */
	sdmmc_decode_cid(&card->cid, cmd.response);
	LOG_DBG("Card MID: 0x%x, OID: %c%c", card->cid.manufacturer,
		((char *)&card->cid.application)[0],
		((char *)&card->cid.application)[1]);
	return 0;
}

/*
 * Requests card to publish a new relative card address, and move from
 * identification to data mode
 */
static int sdmmc_request_rca(struct sd_card *card)
{
	struct sdhc_command cmd = {0};
	int ret;

	cmd.opcode = SD_SEND_RELATIVE_ADDR;
	cmd.arg = 0;
	cmd.response_type = SDHC_RSP_TYPE_R6;
	cmd.timeout_ms = CONFIG_SD_CMD_TIMEOUT;
	/* Issue CMD3 until card responds with nonzero RCA */
	do {
		ret = sdhc_request(card->sdhc, &cmd, NULL);
		if (ret) {
			LOG_DBG("CMD3 failed");
			return ret;
		}
		/* Card RCA is in upper 16 bits of response */
		card->relative_addr = ((cmd.response[0U] & 0xFFFF0000) >> 16U);
	} while (card->relative_addr == 0U);
	LOG_DBG("Card relative addr: %d", card->relative_addr);
	return 0;
}

/* Read card specific data register */
static int sdmmc_read_csd(struct sd_card *card)
{
	struct sdhc_command cmd = {0};
	int ret;

	cmd.opcode = SD_SEND_CSD;
	cmd.arg = (card->relative_addr << 16U);
	cmd.response_type = SDHC_RSP_TYPE_R2;
	cmd.timeout_ms = CONFIG_SD_CMD_TIMEOUT;

	ret = sdhc_request(card->sdhc, &cmd, NULL);
	if (ret) {
		LOG_DBG("CMD9 failed");
		return ret;
	}
	sdmmc_decode_csd(&card->csd, cmd.response,
		&card->block_count, &card->block_size);
	LOG_DBG("Card block count %d, block size %d",
		card->block_count, card->block_size);
	return 0;
}

static int sdmmc_select_card(struct sd_card *card)
{
	struct sdhc_command cmd = {0};
	int ret;

	cmd.opcode = SD_SELECT_CARD;
	cmd.arg = (card->relative_addr << 16U);
	cmd.response_type = SDHC_RSP_TYPE_R1;
	cmd.timeout_ms = CONFIG_SD_CMD_TIMEOUT;

	ret = sdhc_request(card->sdhc, &cmd, NULL);
	if (ret) {
		LOG_DBG("CMD7 failed");
		return ret;
	}
	ret = sdmmc_check_response(&cmd);
	if (ret) {
		LOG_DBG("CMD7 reports error");
		return ret;
	}
	return 0;
}

/* Reads SD configuration register */
static int sdmmc_read_scr(struct sd_card *card)
{
	struct sdhc_command cmd = {0};
	struct sdhc_data data = {0};
	int ret;
	/* DMA onto stack is unsafe, so we use an internal card buffer */
	uint32_t *scr = (uint32_t *)card->card_buffer;
	uint32_t raw_scr[2];

	ret = sdmmc_app_command(card, card->relative_addr);
	if (ret) {
		LOG_DBG("SD app command failed for SD SCR");
		return ret;
	}

	cmd.opcode = SD_APP_SEND_SCR;
	cmd.arg = 0;
	cmd.response_type = SDHC_RSP_TYPE_R1;
	cmd.timeout_ms = CONFIG_SD_CMD_TIMEOUT;

	data.block_size = 8U;
	data.blocks = 1U;
	data.data = scr;
	data.timeout_ms = CONFIG_SD_DATA_TIMEOUT;

	ret = sdhc_request(card->sdhc, &cmd, &data);
	if (ret) {
		LOG_DBG("ACMD51 failed: %d", ret);
		return ret;
	}
	/* Decode SCR */
	raw_scr[0] = sys_be32_to_cpu(scr[0]);
	raw_scr[1] = sys_be32_to_cpu(scr[1]);
	sdmmc_decode_scr(&card->scr, raw_scr, &card->sd_version);
	LOG_DBG("SD reports specification version %d", card->sd_version);
	/* Check card supported bus width */
	if (card->scr.sd_width & 0x4U) {
		card->flags |= SDHC_4BITS_WIDTH;
	}
	/* Check if card supports speed class command (CMD20) */
	if (card->scr.cmd_support & 0x1U) {
		card->flags |= SDHC_SPEED_CLASS_CONTROL_FLAG;
	}
	/* Check for set block count (CMD 23) support */
	if (card->scr.cmd_support & 0x2U) {
		card->flags |= SDHC_CMD23_FLAG;
	}
	return 0;
}

/*
 * Sets bus width of host and card, following section 3.4 of
 * SD host controller specification
 */
static int sdmmc_set_bus_width(struct sd_card *card, enum sdhc_bus_width width)
{
	struct sdhc_command cmd = {0};
	int ret;

	/*
	 * The specification strictly requires card interrupts to be masked, but
	 * Linux does not do so, so we won't either.
	 */
	/* Send ACMD6 to change bus width */
	ret = sdmmc_app_command(card, card->relative_addr);
	if (ret) {
		LOG_DBG("SD app command failed for ACMD6");
		return ret;
	}
	cmd.opcode = SD_APP_SET_BUS_WIDTH;
	cmd.response_type = SDHC_RSP_TYPE_R1;
	cmd.timeout_ms = CONFIG_SD_CMD_TIMEOUT;
	switch (width) {
	case SDHC_BUS_WIDTH1BIT:
		cmd.arg = 0U;
		break;
	case SDHC_BUS_WIDTH4BIT:
		cmd.arg = 2U;
		break;
	default:
		return -ENOTSUP;
	}
	/* Send app command */
	ret = sdhc_request(card->sdhc, &cmd, NULL);
	if (ret) {
		LOG_DBG("Error on ACMD6: %d", ret);
		return ret;
	}
	ret = sdmmc_check_response(&cmd);
	if (ret) {
		LOG_DBG("ACMD6 reports error, response 0x%x", cmd.response[0U]);
		return ret;
	}
	/* Card now has changed bus width. Change host bus width */
	card->bus_io.bus_width = width;
	ret = sdhc_set_io(card->sdhc, &card->bus_io);
	if (ret) {
		LOG_DBG("Could not change host bus width");
	}
	return ret;
}

/*
 * Sends SD switch function CMD6.
 * See table 4-32 in SD physical specification for argument details.
 * When setting a function, we should set the 4 bit block of the command
 * argument corresponding to that function to "value", and all other 4 bit
 * blocks should be left as 0xF (no effect on current function)
 */
static int sdmmc_switch(struct sd_card *card, enum sd_switch_arg mode,
	enum sd_group_num group, uint8_t value, uint8_t *response)
{
	struct sdhc_command cmd = {0};
	struct sdhc_data data = {0};

	cmd.opcode = SD_SWITCH;
	cmd.arg = ((mode & 0x1) << 31) | 0x00FFFFFF;
	cmd.arg &= ~(0xFU << (group * 4));
	cmd.arg |= (value & 0xF) << (group * 4);
	cmd.response_type = SDHC_RSP_TYPE_R1;
	cmd.timeout_ms = CONFIG_SD_CMD_TIMEOUT;

	data.block_size = 64U;
	data.blocks = 1;
	data.data = response;
	data.timeout_ms = CONFIG_SD_DATA_TIMEOUT;

	return sdhc_request(card->sdhc, &cmd, &data);
}

static int sdmmc_read_switch(struct sd_card *card)
{
	uint8_t *status;
	int ret;

	if (card->sd_version < SD_SPEC_VER1_1) {
		/* Switch not supported */
		LOG_INF("SD spec 1.01 does not support CMD6");
		return 0;
	}
	/* Use card internal buffer to read 64 byte switch data */
	status = card->card_buffer;
	/**
	 * Setting switch to zero will read card's support values,
	 * otherwise known as SD "check function"
	 */
	ret = sdmmc_switch(card, SD_SWITCH_CHECK, 0, 0, status);
	if (ret) {
		LOG_DBG("CMD6 failed %d", ret);
		return ret;
	}
	/*
	 * See table 4-11 and 4.3.10.4 of physical layer specification for
	 * bit definitions. Note that response is big endian, so index 13 will
	 * read bits 400-408.
	 * Bit n being set in support bit field indicates support for function
	 * number n on the card. (So 0x3 indicates support for functions 0 and 1)
	 */
	if (status[13] & HIGH_SPEED_BUS_SPEED) {
		card->switch_caps.hs_max_dtr = HS_MAX_DTR;
	}
	if (card->sd_version >= SD_SPEC_VER3_0) {
		card->switch_caps.bus_speed = status[13];
		card->switch_caps.sd_drv_type = status[9];
		card->switch_caps.sd_current_limit = status[7];
	}
	return 0;
}

/* Returns 1 if host supports UHS, zero otherwise */
static inline int sdmmc_host_uhs(struct sdhc_host_props *props)
{
	return (props->host_caps.sdr50_support |
		props->host_caps.uhs_2_support |
		props->host_caps.sdr104_support |
		props->host_caps.ddr50_support)
		& (props->host_caps.vol_180_support);
}

static inline void sdmmc_select_bus_speed(struct sd_card *card)
{
	/*
	 * Note that function support is defined using bitfields, but function
	 * selection is defined using values 0x0-0xF.
	 */
	if (card->host_props.host_caps.sdr104_support &&
		(card->switch_caps.bus_speed & UHS_SDR104_BUS_SPEED)) {
		card->card_speed = SD_TIMING_SDR104;
	} else if (card->host_props.host_caps.ddr50_support &&
		(card->switch_caps.bus_speed & UHS_DDR50_BUS_SPEED)) {
		card->card_speed = SD_TIMING_DDR50;
	} else if (card->host_props.host_caps.sdr50_support &&
		(card->switch_caps.bus_speed & UHS_SDR50_BUS_SPEED)) {
		card->card_speed = SD_TIMING_SDR50;
	} else if (card->host_props.host_caps.high_spd_support &&
		(card->switch_caps.bus_speed & UHS_SDR12_BUS_SPEED)) {
		card->card_speed = SD_TIMING_SDR12;
	}
}

/* Selects driver type for SD card */
static int sdmmc_select_driver_type(struct sd_card *card)
{
	int ret = 0;
	uint8_t *status = card->card_buffer;

	/*
	 * We will only attempt to use driver type C over the default of type B,
	 * since it should result in lower current consumption if supported.
	 */
	if (card->host_props.host_caps.drv_type_c_support &&
		(card->switch_caps.sd_drv_type & SD_DRIVER_TYPE_C)) {
		card->bus_io.driver_type = SD_DRIVER_TYPE_C;
		/* Change drive strength */
		ret = sdmmc_switch(card, SD_SWITCH_SET,
			SD_GRP_DRIVER_STRENGTH_MODE,
			(find_msb_set(SD_DRIVER_TYPE_C) - 1), status);
	}
	return ret;
}

/* Sets current limit for SD card */
static int sdmmc_set_current_limit(struct sd_card *card)
{
	int ret;
	int max_current = -1;
	uint8_t *status = card->card_buffer;

	if ((card->card_speed != SD_TIMING_SDR50) &&
		(card->card_speed != SD_TIMING_SDR104) &&
		(card->card_speed != SD_TIMING_DDR50)) {
		return 0; /* Cannot set current limit */
	} else if (card->host_props.max_current_180 >= 800 &&
		(card->switch_caps.sd_current_limit & SD_MAX_CURRENT_800MA)) {
		max_current = SD_SET_CURRENT_800MA;
	} else if (card->host_props.max_current_180 >= 600 &&
		(card->switch_caps.sd_current_limit & SD_MAX_CURRENT_600MA)) {
		max_current = SD_SET_CURRENT_600MA;
	} else if (card->host_props.max_current_180 >= 400 &&
		(card->switch_caps.sd_current_limit & SD_MAX_CURRENT_400MA)) {
		max_current = SD_SET_CURRENT_400MA;
	} else if (card->host_props.max_current_180 >= 200 &&
		(card->switch_caps.sd_current_limit & SD_MAX_CURRENT_200MA)) {
		max_current = SD_SET_CURRENT_200MA;
	}
	if (max_current != -1) {
		LOG_DBG("Changing SD current limit: %d", max_current);
		/* Switch SD current */
		ret = sdmmc_switch(card, SD_SWITCH_SET, SD_GRP_CURRENT_LIMIT_MODE,
			max_current, status);
		if (ret) {
			LOG_DBG("Failed to set SD current limit");
			return ret;
		}
		if (((status[15] >> 4) & 0x0F) != max_current) {
			/* Status response indicates card did not select request limit */
			LOG_WRN("Card did not accept current limit");
		}
	}
	return 0;
}

/* Applies selected card bus speed to card and host */
static int sdmmc_set_bus_speed(struct sd_card *card)
{
	int ret;
	int timing = 0;
	uint8_t *status = card->card_buffer;

	card->bus_io.timing = card->card_speed;
	switch (card->card_speed) {
	/* Set bus clock speed */
	case SD_TIMING_SDR104:
		card->switch_caps.uhs_max_dtr = UHS_SDR104_MAX_DTR;
		timing = SDHC_TIMING_SDR104;
		break;
	case SD_TIMING_DDR50:
		card->switch_caps.uhs_max_dtr = UHS_DDR50_MAX_DTR;
		timing = SDHC_TIMING_DDR50;
		break;
	case SD_TIMING_SDR50:
		card->switch_caps.uhs_max_dtr = UHS_SDR50_MAX_DTR;
		timing = SDHC_TIMING_SDR50;
		break;
	case SD_TIMING_SDR25:
		card->switch_caps.uhs_max_dtr = UHS_SDR25_MAX_DTR;
		timing = SDHC_TIMING_SDR25;
		break;
	case SD_TIMING_SDR12:
		card->switch_caps.uhs_max_dtr = UHS_SDR12_MAX_DTR;
		timing = SDHC_TIMING_SDR12;
		break;
	default:
		/* No need to change bus speed */
		return 0;
	}

	/* Switch bus speed */
	ret = sdmmc_switch(card, SD_SWITCH_SET, SD_GRP_TIMING_MODE,
		card->card_speed, status);
	if (ret) {
		LOG_DBG("Failed to switch SD card speed");
		return ret;
	}
	if ((status[16] & 0xF) != card->card_speed) {
		LOG_WRN("Card did not accept new speed");
	} else {
		/* Change host bus speed */
		card->bus_io.timing = timing;
		card->bus_io.clock = card->switch_caps.uhs_max_dtr;
		LOG_DBG("Setting bus clock to: %d", card->bus_io.clock);
		ret = sdhc_set_io(card->sdhc, &card->bus_io);
		if (ret) {
			LOG_ERR("Failed to change host bus speed");
			return ret;
		}
	}
	return 0;
}

/*
 * Init UHS capable SD card. Follows figure 3-16 in physical layer specification.
 */
static int sdmmc_init_uhs(struct sd_card *card)
{
	int ret;

	/* Raise bus width to 4 bits */
	ret = sdmmc_set_bus_width(card, SDHC_BUS_WIDTH4BIT);
	if (ret) {
		LOG_ERR("Failed to change card bus width to 4 bits");
		return ret;
	}

	/* Select bus speed for card depending on host and card capability*/
	sdmmc_select_bus_speed(card);
	/* Now, set the driver strength for the card */
	ret = sdmmc_select_driver_type(card);
	if (ret) {
		LOG_DBG("Failed to select new driver type");
		return ret;
	}
	ret = sdmmc_set_current_limit(card);
	if (ret) {
		LOG_DBG("Failed to set card current limit");
		return ret;
	}
	/* Apply the bus speed selected earlier */
	ret = sdmmc_set_bus_speed(card);
	if (ret) {
		LOG_DBG("Failed to set card bus speed");
		return ret;
	}

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
	/* Read the card's CID (card identification register) */
	ret = sdmmc_read_cid(card);
	if (ret) {
		return ret;
	}
	/*
	 * Request new relative card address. This moves the card from
	 * identification mode to data transfer mode
	 */
	ret = sdmmc_request_rca(card);
	if (ret) {
		return ret;
	}
	/* Card has entered data transfer mode. Get card specific data register */
	ret = sdmmc_read_csd(card);
	if (ret) {
		return ret;
	}
	/* Move the card to transfer state (with CMD7) to run remaining commands */
	ret = sdmmc_select_card(card);
	if (ret) {
		return ret;
	}
	/*
	 * With card in data transfer state, we can set SD clock to maximum
	 * frequency for non high speed mode (25Mhz)
	 */
	card->bus_io.clock = SD_CLOCK_25MHZ;
	ret = sdhc_set_io(card->sdhc, &card->bus_io);
	if (ret) {
		LOG_ERR("Failed to raise bus frequency to 25MHz");
		return ret;
	}
	/* Read SD SCR (SD configuration register),
	 * to get supported bus width
	 */
	ret = sdmmc_read_scr(card);
	if (ret) {
		return ret;
	}
	/* Read switch capabilities to determine what speeds card supports */
	ret = sdmmc_read_switch(card);
	if (ret) {
		LOG_ERR("Failed to read card functions");
		return ret;
	}
	if ((card->flags & SDHC_1800MV_FLAG) && sdmmc_host_uhs(&card->host_props)) {
		ret = sdmmc_init_uhs(card);
		if (ret) {
			LOG_ERR("UHS card init failed");
		}
	}
}
