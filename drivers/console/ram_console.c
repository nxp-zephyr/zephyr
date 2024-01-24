/* ram_console.c - Console messages to a RAM buffer */

/*
 * Copyright (c) 2015 Intel Corporation
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/linker/devicetree_regions.h>

extern void __printk_hook_install(int (*fn)(int));
extern void __stdout_hook_install(int (*fn)(int));

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ram_console), okay)
char ram_console[DT_REG_SIZE(DT_NODELABEL(ram_console))] __attribute__((__section__(LINKER_DT_NODE_REGION_NAME(DT_NODELABEL(ram_console)))));
#else
/* Extra byte to ensure we're always NULL-terminated */
char ram_console[CONFIG_RAM_CONSOLE_BUFFER_SIZE + 1];
#endif

static int pos;
static unsigned int size;

static int ram_console_out(int character)
{
	ram_console[pos] = (char)character;
	pos = (pos + 1) % size;
	return character;
}

static int ram_console_init(void)
{
#if DT_NODE_HAS_STATUS(DT_NODELABEL(ram_console), okay)
	size = DT_REG_SIZE(DT_NODELABEL(ram_console)) - 1;
#else
	size = CONFIG_RAM_CONSOLE_BUFFER_SIZE;
#endif
	__printk_hook_install(ram_console_out);
	__stdout_hook_install(ram_console_out);

	return 0;
}

SYS_INIT(ram_console_init, PRE_KERNEL_1, CONFIG_CONSOLE_INIT_PRIORITY);
