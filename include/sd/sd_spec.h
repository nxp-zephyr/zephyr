/*
 * Copyright (c) 2022 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * SD card specification
 */

#ifndef ZEPHYR_SUBSYS_SD_SPEC_H_
#define ZEPHYR_SUBSYS_SD_SPEC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum sdhc_opcode {
	SD_GO_IDLE_STATE = 0,
	SD_ALL_SEND_CID = 2,
	SD_SEND_RELATIVE_ADDR = 3,
	SDIO_SEND_OP_COND = 5, /* SDIO cards only */
	SD_SWITCH = 6,
	SD_SELECT_CARD = 7,
	SD_SEND_IF_COND = 8,
	SD_SEND_CSD = 9,
	SD_SEND_CID = 10,
	SD_VOL_SWITCH = 11,
	SD_STOP_TRANSMISSION = 12,
	SD_SEND_STATUS = 13,
	SD_GO_INACTIVE_STATE = 15,
	SD_SET_BLOCK_SIZE = 16,
	SD_READ_SINGLE_BLOCK = 17,
	SD_READ_MULTIPLE_BLOCK = 18,
	SD_SEND_TUNING_BLOCK = 19,
	SD_SET_BLOCK_COUNT = 23,
	SD_WRITE_SINGLE_BLOCK = 24,
	SD_WRITE_MULTIPLE_BLOCK = 25,
	SD_ERASE_BLOCK_START = 32,
	SD_ERASE_BLOCK_END = 33,
	SD_ERASE_BLOCK_OPERATION = 38,
	SD_APP_CMD = 55,
	SD_READ_OCR = 58,
	SD_CRC_ON_OFF = 59,
};

enum sdhc_app_cmd {
	SD_APP_SET_BUS_WIDTH = 6,
	SD_APP_SEND_STATUS = 13,
	SD_APP_SEND_NUM_WRITTEN_BLK = 22,
	SD_APP_SET_WRITE_BLK_ERASE_CNT = 23,
	SD_APP_SEND_OP_COND = 41,
	SD_APP_CLEAR_CARD_DETECT = 42,
	SD_APP_SEND_SCR = 51,
};

/* R1 response status */
enum sdhc_r1_status {
	/* Bits 0-2 reserved */
	SDHC_R1_AUTH_ERR = (1U << 3),
	/* Bit 4 reserved for SDIO */
	SDHC_R1_APP_CMD = (1U << 5),
	SDHC_R1_FX_EVENT = (1U << 6),
	/* Bit 7 reserved */
	SDHC_R1_RDY_DATA = (1U << 8),
	SDHC_R1_CUR_STATE = (0xFU << 9),
	SDHC_R1_ERASE_RESET = (1 << 13),
	SDHC_R1_ECC_DISABLED = (1 << 14),
	SDHC_R1_ERASE_SKIP = (1 << 15),
	SDHC_R1_CSD_OVERWRITE = (1 << 16),
	/* Bits 17-18 reserved */
	SDHC_R1_ERR = (1 << 19),
	SDHC_R1_CC_ERR = (1 << 20),
	SDHC_R1_ECC_FAIL = (1 << 21),
	SDHC_R1_ILLEGAL_CMD = (1 << 22),
	SDHC_R1_CRC_ERR = (1 << 23),
	SDHC_R1_UNLOCK_FAIL = (1 << 24),
	SDHC_R1_CARD_LOCKED = (1 << 25),
	SDHC_R1_WP_VIOLATION = (1 << 26),
	SDHC_R1_ERASE_PARAM = (1 << 27),
	SDHC_R1_ERASE_SEQ_ERR = (1 << 28),
	SDHC_R1_BLOCK_LEN_ERR = (1 << 29),
	SDHC_R1_ADDR_ERR = (1 << 30),
	SDHC_R1_OUT_OF_RANGE = (1 << 31),
};

#define SDHC_R1_ERR_FLAGS (SDHC_R1_AUTH_ERR | SDHC_R1_ERASE_SKIP |		\
		SDHC_R1_CSD_OVERWRITE | SDHC_R1_ERR | SDHC_R1_CC_ERR |		\
		SDHC_R1_ECC_FAIL | SDHC_R1_ILLEGAL_CMD | SDHC_R1_CRC_ERR |	\
		SDHC_R1_UNLOCK_FAIL | SDHC_R1_WP_VIOLATION |			\
		SDHC_R1_ERASE_PARAM | SDHC_R1_ERASE_SEQ_ERR |			\
		SDHC_R1_BLOCK_LEN_ERR | SDHC_R1_ADDR_ERR |			\
		SDHC_R1_OUT_OF_RANGE)

#define SDHC_CMD_SIZE 6
#define SDHC_CMD_BODY_SIZE (SDHC_CMD_SIZE - 1)
#define SDHC_CRC16_SIZE 2

/* Command flags */
#define SDHC_START 0x80
#define SDHC_TX 0x40

/* Fields in various card registers */
#define SDHC_HCS (1 << 30)
#define SDHC_CCS (1 << 30)
#define SDHC_BUSY (1 << 31)
#define SDHC_VHS_MASK (0x0F << 8)
#define SDHC_VHS_3V3 (1 << 8)
#define SDHC_CHECK 0xAA
#define SDHC_CSD_SIZE 16
#define SDHC_CSD_V1 0
#define SDHC_CSD_V2 1

/* Data block tokens */
#define SDHC_TOKEN_SINGLE 0xFE
#define SDHC_TOKEN_MULTI_WRITE 0xFC
#define SDHC_TOKEN_STOP_TRAN 0xFD

/* Data block responses */
#define SDHC_RESPONSE_ACCEPTED 0x05
#define SDHC_RESPONSE_CRC_ERR 0x0B
#define SDHC_RESPONSE_WRITE_ERR 0x0E

#define SDHC_MIN_TRIES 20
#define SDHC_RETRY_DELAY 20
/* Time to wait for the card to initialise */
#define SDHC_INIT_TIMEOUT 5000
/* Time to wait for the card to respond or come ready */
#define SDHC_READY_TIMEOUT 500

enum sdhc_rsp_type {
	SDHC_RSP_TYPE_NONE = 0U,
	SDHC_RSP_TYPE_R1 = 1U,
	SDHC_RSP_TYPE_R1b = 2U,
	SDHC_RSP_TYPE_R2 = 3U,
	SDHC_RSP_TYPE_R3 = 4U,
	SDHC_RSP_TYPE_R4 = 5U,
	SDHC_RSP_TYPE_R5 = 6U,
	SDHC_RSP_TYPE_R5b = 7U,
	SDHC_RSP_TYPE_R6 = 8U,
	SDHC_RSP_TYPE_R7 = 9U,
};

enum sdhc_flag {
	SDHC_HIGH_CAPACITY_FLAG = (1U << 1U),
	SDHC_4BITS_WIDTH = (1U << 2U),
	SDHC_SDHC_FLAG = (1U << 3U),
	SDHC_SDXC_FLAG = (1U << 4U),
	SDHC_1800MV_FLAG = (1U << 5U),
	SDHC_3000MV_FLAG = (1U << 6U),
	SDHC_CMD23_FLAG = (1U << 7U),
	SDHC_SPEED_CLASS_CONTROL_FLAG = (1U << 8U),
};

enum sdhc_r1_error_flag {
	SDHC_R1OUTOF_RANGE_ERR = (1U << 31U),
	SDHC_R1ADDRESS_ERR = (1U << 30U),
	SDHC_R1BLK_LEN_ERR = (1U << 29U),
	SDHC_R1ERASE_SEQ_ERR = (1U << 28U),
	SDHC_R1ERASE_PARAMETER_ERR = (1U << 27U),
	SDHC_R1WRITE_PROTECTION_ERR = (1U << 26U),
	SDHC_R1CARD_LOCKED_ERR = (1U << 25U),
	SDHC_R1LOCK_UNLOCK_ERR = (1U << 24U),
	SDHC_R1CMD_CRC_ERR = (1U << 23U),
	SDHC_R1ILLEGAL_CMD_ERR = (1U << 22U),
	SDHC_R1ECC_ERR = (1U << 21U),
	SDHC_R1CARD_CONTROL_ERR = (1U << 20U),
	SDHC_R1ERR = (1U << 19U),
	SDHC_R1CID_CSD_OVERWRITE_ERR = (1U << 16U),
	SDHC_R1WRITE_PROTECTION_ERASE_SKIP = (1U << 15U),
	SDHC_R1CARD_ECC_DISABLED = (1U << 14U),
	SDHC_R1ERASE_RESET = (1U << 13U),
	SDHC_R1READY_FOR_DATA = (1U << 8U),
	SDHC_R1SWITCH_ERR = (1U << 7U),
	SDHC_R1APP_CMD = (1U << 5U),
	SDHC_R1AUTH_SEQ_ERR = (1U << 3U),

	SDHC_R1ERR_All_FLAG =
		(SDHC_R1OUTOF_RANGE_ERR |
		SDHC_R1ADDRESS_ERR |
		SDHC_R1BLK_LEN_ERR |
		SDHC_R1ERASE_SEQ_ERR |
		SDHC_R1ERASE_PARAMETER_ERR |
		SDHC_R1WRITE_PROTECTION_ERR |
		SDHC_R1CARD_LOCKED_ERR |
		SDHC_R1LOCK_UNLOCK_ERR |
		SDHC_R1CMD_CRC_ERR |
		SDHC_R1ILLEGAL_CMD_ERR |
		SDHC_R1ECC_ERR |
		SDHC_R1CARD_CONTROL_ERR |
		SDHC_R1ERR |
		SDHC_R1CID_CSD_OVERWRITE_ERR |
		SDHC_R1AUTH_SEQ_ERR),

	SDHC_R1ERR_NONE = 0,
};

#define SD_R1_CURRENT_STATE(x) (((x)&0x00001E00U) >> 9U)

enum sd_r1_current_state {
	SDMMC_R1_IDLE = 0U,
	SDMMC_R1_READY = 1U,
	SDMMC_R1_IDENTIFY = 2U,
	SDMMC_R1_STANDBY = 3U,
	SDMMC_R1_TRANSFER = 4U,
	SDMMC_R1_SEND_DATA = 5U,
	SDMMC_R1_RECIVE_DATA = 6U,
	SDMMC_R1_PROGRAM = 7U,
	SDMMC_R1_DISCONNECT = 8U,
};

enum sd_ocr_flag {
	SD_OCR_PWR_BUSY_FLAG = (1U << 31U),
	/*!< Power up busy status */
	SD_OCR_HOST_CAP_FLAG = (1U << 30U),
	/*!< Card capacity status */
	SD_OCR_CARD_CAP_FLAG = SD_OCR_HOST_CAP_FLAG,
	/*!< Card capacity status */
	SD_OCR_SWITCH_18_REQ_FLAG = (1U << 24U),
	/*!< Switch to 1.8V request */
	SD_OCR_SWITCH_18_ACCEPT_FLAG = SD_OCR_SWITCH_18_REQ_FLAG,
	/*!< Switch to 1.8V accepted */
	SD_OCR_VDD27_28FLAG = (1U << 15U),
	/*!< VDD 2.7-2.8 */
	SD_OCR_VDD28_29FLAG = (1U << 16U),
	/*!< VDD 2.8-2.9 */
	SD_OCR_VDD29_30FLAG = (1U << 17U),
	/*!< VDD 2.9-3.0 */
	SD_OCR_VDD30_31FLAG = (1U << 18U),
	/*!< VDD 2.9-3.0 */
	SD_OCR_VDD31_32FLAG = (1U << 19U),
	/*!< VDD 3.0-3.1 */
	SD_OCR_VDD32_33FLAG = (1U << 20U),
	/*!< VDD 3.1-3.2 */
	SD_OCR_VDD33_34FLAG = (1U << 21U),
	/*!< VDD 3.2-3.3 */
	SD_OCR_VDD34_35FLAG = (1U << 22U),
	/*!< VDD 3.3-3.4 */
	SD_OCR_VDD35_36FLAG = (1U << 23U),
	/*!< VDD 3.4-3.5 */
};

#define SDIO_OCR_IO_NUMBER_SHIFT (28U)
/* Lower 24 bits hold SDIO I/O OCR */
#define SDIO_IO_OCR_MASK (0xFFFFFFU)

enum sdio_ocr_flag {
	SDIO_OCR_IO_READY_FLAG = (1U << 31),
	SDIO_OCR_IO_NUMBER = (7U << 28U), /*!< Number of io function */
	SDIO_OCR_MEM_PRESENT_FLAG = (1U << 27U), /*!< Memory present flag */
	SDIO_OCR_180_VOL_FLAG = (1U << 24),	/*!< Switch to 1.8v signalling */
	SDIO_OCR_VDD20_21FLAG = (1U << 8U),  /*!< VDD 2.0-2.1 */
	SDIO_OCR_VDD21_22FLAG = (1U << 9U),  /*!< VDD 2.1-2.2 */
	SDIO_OCR_VDD22_23FLAG = (1U << 10U), /*!< VDD 2.2-2.3 */
	SDIO_OCR_VDD23_24FLAG = (1U << 11U), /*!< VDD 2.3-2.4 */
	SDIO_OCR_VDD24_25FLAG = (1U << 12U), /*!< VDD 2.4-2.5 */
	SDIO_OCR_VDD25_26FLAG = (1U << 13U), /*!< VDD 2.5-2.6 */
	SDIO_OCR_VDD26_27FLAG = (1U << 14U), /*!< VDD 2.6-2.7 */
	SDIO_OCR_VDD27_28FLAG = (1U << 15U), /*!< VDD 2.7-2.8 */
	SDIO_OCR_VDD28_29FLAG = (1U << 16U), /*!< VDD 2.8-2.9 */
	SDIO_OCR_VDD29_30FLAG = (1U << 17U), /*!< VDD 2.9-3.0 */
	SDIO_OCR_VDD30_31FLAG = (1U << 18U), /*!< VDD 2.9-3.0 */
	SDIO_OCR_VDD31_32FLAG = (1U << 19U), /*!< VDD 3.0-3.1 */
	SDIO_OCR_VDD32_33FLAG = (1U << 20U), /*!< VDD 3.1-3.2 */
	SDIO_OCR_VDD33_34FLAG = (1U << 21U), /*!< VDD 3.2-3.3 */
	SDIO_OCR_VDD34_35FLAG = (1U << 22U), /*!< VDD 3.3-3.4 */
	SDIO_OCR_VDD35_36FLAG = (1U << 23U), /*!< VDD 3.4-3.5 */
};

#define SD_PRODUCT_NAME_BYTES (5U)

struct sd_cid {
	uint8_t manufacturer;
	/*!< Manufacturer ID [127:120] */
	uint16_t application;
	/*!< OEM/Application ID [119:104] */
	uint8_t name[SD_PRODUCT_NAME_BYTES];
	/*!< Product name [103:64] */
	uint8_t version;
	/*!< Product revision [63:56] */
	uint32_t ser_num;
	/*!< Product serial number [55:24] */
	uint16_t date;
	/*!< Manufacturing date [19:8] */
};

enum hs_max_data_rate {
	HS_MAX_DTR = 50000000,
};

enum uhs_max_data_rate {
	UHS_SDR104_MAX_DTR = 208000000,
	UHS_SDR50_MAX_DTR = 100000000,
	UHS_DDR50_MAX_DTR = 50000000,
	UHS_SDR25_MAX_DTR = 50000000,
	UHS_SDR12_MAX_DTR = 25000000,
};

enum sd_bus_speed {
	UHS_SDR12_BUS_SPEED = (1U << 0),
	HIGH_SPEED_BUS_SPEED = (1U << 1),
	UHS_SDR25_BUS_SPEED = (1U << 1),
	UHS_SDR50_BUS_SPEED = (1U << 2),
	UHS_SDR104_BUS_SPEED = (1U << 3),
	UHS_DDR50_BUS_SPEED = (1U << 4),
};

/* SD timing mode function selection values */
enum sd_timing_mode {
	SD_TIMING_SDR12 = 0U,
	/*!< Default mode & SDR12 */
	SD_TIMING_SDR25 = 1U,
	/*!< High speed mode & SDR25 */
	SD_TIMING_SDR50 = 2U,
	/*!< SDR50 mode*/
	SD_TIMING_SDR104 = 3U,
	/*!< SDR104 mode */
	SD_TIMING_DDR50 = 4U,
	/*!< DDR50 mode */
};

/* SD current setting values */
enum sd_current_setting {
	SD_SET_CURRENT_200MA = 0,
	SD_SET_CURRENT_400MA = 1,
	SD_SET_CURRENT_600MA = 2,
	SD_SET_CURRENT_800MA = 3,
};


enum sd_current_limit {
	SD_MAX_CURRENT_200MA = (1U << 0),
	/*!< default current limit */
	SD_MAX_CURRENT_400MA = (1U << 1),
	/*!< current limit to 400MA */
	SD_MAX_CURRENT_600MA = (1U << 2),
	/*!< current limit to 600MA */
	SD_MAX_CURRENT_800MA = (1U << 3),
	/*!< current limit to 800MA */
};

enum sd_driver_type {
	SD_DRIVER_TYPE_B = 0x1,
	SD_DRIVER_TYPE_A = 0x2,
	SD_DRIVER_TYPE_C = 0x4,
	SD_DRIVER_TYPE_D = 0x8,
};

struct sd_switch_caps {
	enum hs_max_data_rate hs_max_dtr;
	enum uhs_max_data_rate uhs_max_dtr;
	enum sd_bus_speed bus_speed;
	enum sd_driver_type sd_drv_type;
	enum sd_current_limit sd_current_limit;
};

struct sd_csd {
	uint8_t csd_structure;
	/*!< CSD structure [127:126] */
	uint8_t read_time1;
	/*!< Data read access-time-1 [119:112] */
	uint8_t read_time2;
	/*!< Data read access-time-2 in clock cycles (NSAC*100) [111:104] */
	uint8_t xfer_rate;
	/*!< Maximum data transfer rate [103:96] */
	uint16_t cmd_class;
	/*!< Card command classes [95:84] */
	uint8_t read_blk_len;
	/*!< Maximum read data block length [83:80] */
	uint16_t flags;
	/*!< Flags in _sd_csd_flag */
	uint32_t device_size;
	/*!< Device size [73:62] */
	uint8_t read_current_min;
	/*!< Maximum read current at VDD min [61:59] */
	uint8_t read_current_max;
	/*!< Maximum read current at VDD max [58:56] */
	uint8_t write_current_min;
	/*!< Maximum write current at VDD min [55:53] */
	uint8_t write_current_max;
	/*!< Maximum write current at VDD max [52:50] */
	uint8_t dev_size_mul;
	/*!< Device size multiplier [49:47] */

	uint8_t erase_size;
	/*!< Erase sector size [45:39] */
	uint8_t write_prtect_size;
	/*!< Write protect group size [38:32] */
	uint8_t write_speed_factor;
	/*!< Write speed factor [28:26] */
	uint8_t write_blk_len;
	/*!< Maximum write data block length [25:22] */
	uint8_t file_fmt;
	/*!< File format [11:10] */
};

struct sd_scr {
	uint8_t scr_structure;
	/*!< SCR Structure [63:60] */
	uint8_t sd_spec;
	/*!< SD memory card specification version [59:56] */
	uint16_t flags;
	/*!< SCR flags in _sd_scr_flag */
	uint8_t sd_sec;
	/*!< Security specification supported [54:52] */
	uint8_t sd_width;
	/*!< Data bus widths supported [51:48] */
	uint8_t sd_ext_sec;
	/*!< Extended security support [46:43] */
	uint8_t cmd_support;
	/*!< Command support bits [33:32] 33-support CMD23, 32-support cmd20*/
	uint32_t rsvd;
	/*!< reserved for manufacturer usage [31:0] */
};


#define SDMMC_DEFAULT_BLOCK_SIZE (512U)

struct sd_data_op {
	uint32_t start_block;
	uint32_t block_size;
	uint32_t block_count;
	uint32_t *buf;
};

enum sd_switch_arg {
	SD_SWITCH_CHECK = 0U,
	/*!< SD switch mode 0: check function */
	SD_SWITCH_SET = 1U,
	/*!< SD switch mode 1: set function */
};

enum sd_group_num {
	SD_GRP_TIMING_MODE = 0U,
	/*!< access mode group*/
	SD_GRP_CMD_SYS_MODE = 1U,
	/*!< command system group*/
	SD_GRP_DRIVER_STRENGTH_MODE = 2U,
	/*!< driver strength group*/
	SD_GRP_CURRENT_LIMIT_MODE = 3U,
	/*!< current limit group*/
};

enum sd_driver_strength {
	SD_DRV_STRENGTH_TYPEB = 0U,
	/*!< default driver strength*/
	SD_DRV_STRENGTH_TYPEA = 1U,
	/*!< driver strength TYPE A */
	SD_DRV_STRENGTH_TYPEC = 2U,
	/*!< driver strength TYPE C */
	SD_DRV_STRENGTH_TYPED = 3U,
	/*!< driver strength TYPE D */
};

enum sd_csd_flag {
	SD_CSD_READ_BLK_PARTIAL_FLAG = (1U << 0U),
	/*!< Partial blocks for read allowed [79:79] */
	SD_CSD_WRITE_BLK_MISALIGN_FLAG = (1U << 1U),
	/*!< Write block misalignment [78:78] */
	SD_CSD_READ_BLK_MISALIGN_FLAG = (1U << 2U),
	/*!< Read block misalignment [77:77] */
	SD_CSD_DSR_IMPLEMENTED_FLAG = (1U << 3U),
	/*!< DSR implemented [76:76] */
	SD_CSD_ERASE_BLK_EN_FLAG = (1U << 4U),
	/*!< Erase single block enabled [46:46] */
	SD_CSD_WRITE_PROTECT_GRP_EN_FLAG = (1U << 5U),
	/*!< Write protect group enabled [31:31] */
	SD_CSD_WRITE_BLK_PARTIAL_FLAG = (1U << 6U),
	/*!< Partial blocks for write allowed [21:21] */
	SD_CSD_FILE_FMT_GRP_FLAG = (1U << 7U),
	/*!< File format group [15:15] */
	SD_CSD_COPY_FLAG = (1U << 8U),
	/*!< Copy flag [14:14] */
	SD_CSD_PERMANENT_WRITE_PROTECT_FLAG = (1U << 9U),
	/*!< Permanent write protection [13:13] */
	SD_CSD_TMP_WRITE_PROTECT_FLAG = (1U << 10U),
	/*!< Temporary write protection [12:12] */
};

enum sd_scr_flag {
	SD_SCR_DATA_STATUS_AFTER_ERASE = (1U << 0U),
	/*!< Data status after erases [55:55] */
	SD_SCR_SPEC3 = (1U << 1U),
	/*!< Specification version 3.00 or higher [47:47]*/
};

enum sd_spec_version {
	SD_SPEC_VER1_0 = (1U << 0U),
	/*!< SD card version 1.0-1.01 */
	SD_SPEC_VER1_1 = (1U << 1U),
	/*!< SD card version 1.10 */
	SD_SPEC_VER2_0 = (1U << 2U),
	/*!< SD card version 2.00 */
	SD_SPEC_VER3_0 = (1U << 3U),
	/*!< SD card version 3.0 */
};

enum sd_command_class {
	SD_CMD_CLASS_BASIC = (1U << 0U),
	/*!< Card command class 0 */
	SD_CMD_CLASS_BLOCK_READ = (1U << 2U),
	/*!< Card command class 2 */
	SD_CMD_CLASS_BLOCK_WRITE = (1U << 4U),
	/*!< Card command class 4 */
	SD_CMD_CLASS_ERASE = (1U << 5U),
	/*!< Card command class 5 */
	SD_CMD_CLASS_WRITE_PROTECT = (1U << 6U),
	/*!< Card command class 6 */
	SD_CMD_CLASS_LOCKCARD = (1U << 7U),
	/*!< Card command class 7 */
	SD_CMD_CLASS_APP_SPECIFIC = (1U << 8U),
	/*!< Card command class 8 */
	SD_CMD_CLASS_IO_MODE = (1U << 9U),
	/*!< Card command class 9 */
	SD_CMD_CLASS_SWITCH = (1U << 10U),
	/*!< Card command class 10 */
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_SD_SPEC_H_ */
