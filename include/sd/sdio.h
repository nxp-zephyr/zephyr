/*
 * Copyright (c) 2022 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SD_SDIO_H_
#define ZEPHYR_INCLUDE_SD_SDIO_H_

#include <device.h>
#include <drivers/sdhc.h>
#include <sd/sd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enable SDIO function
 *
 * Enables SDIO function
 * @param card: card to write data to
 * @param sdio_func: SDIO function number to enable
 */
int sdio_func_enable(struct sd_card *card, int sdio_func);

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
int sdio_io_write(struct sd_card *card, int sdio_func, uint32_t reg, uint8_t *data, int count);

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
int sdio_io_read(struct sd_card *card, int sdio_func, uint32_t reg, uint8_t *data, int count);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SD_SDIO_H_ */
