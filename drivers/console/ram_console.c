/* ram_console.c - Console messages to a RAM buffer */

/*
 * Copyright (c) 2015 Intel Corporation
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/linker/devicetree_regions.h>
#include <zephyr/drivers/console/ram_console.h>

extern void __printk_hook_install(int (*fn)(int));
extern void __stdout_hook_install(int (*fn)(int));

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ram_console), okay)
char ram_console[DT_REG_SIZE(DT_NODELABEL(ram_console))] __attribute__((__section__(LINKER_DT_NODE_REGION_NAME(DT_NODELABEL(ram_console)))));
#else
/* Extra byte to ensure we're always NULL-terminated */
char ram_console[CONFIG_RAM_CONSOLE_BUFFER_SIZE + 1];
#endif

static struct ram_console_header *header;

static int ram_console_out(int character)
{
	header->buf_addr[header->pos] = (char)character;
	header->pos = (header->pos + 1) % header->buf_size;
	return character;
}

static int ram_console_init(void)
{
	unsigned int size;

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ram_console), okay)
	size = DT_REG_SIZE(DT_NODELABEL(ram_console)) - 1;
#else
	size = CONFIG_RAM_CONSOLE_BUFFER_SIZE;
#endif
	memset(ram_console, 0, size + 1);
	header = (struct ram_console_header *)ram_console;
	strcpy(header->flag_string, RAM_CONSOLE_HEAD_STR);
	header->buf_addr = (char *)(ram_console + RAM_CONSOLE_HEAD_SIZE);
	header->buf_size = size - RAM_CONSOLE_HEAD_SIZE;
	header->pos = 0;

	__printk_hook_install(ram_console_out);
	__stdout_hook_install(ram_console_out);

	return 0;
}

SYS_INIT(ram_console_init, PRE_KERNEL_1, CONFIG_CONSOLE_INIT_PRIORITY);
