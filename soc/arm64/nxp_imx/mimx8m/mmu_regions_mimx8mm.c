/*
 * Copyright 2020-2022,2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm64/arm_mmu.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

static const struct arm_mmu_region mmu_regions[] = {

	MMU_REGION_FLAT_ENTRY("GIC",
			      DT_REG_ADDR_BY_IDX(DT_NODELABEL(gic), 0),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(gic), 0),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("GIC",
			      DT_REG_ADDR_BY_IDX(DT_NODELABEL(gic), 1),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(gic), 1),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("CCM",
			      DT_REG_ADDR(DT_NODELABEL(ccm)),
			      DT_REG_SIZE(DT_NODELABEL(ccm)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("UART2",
			      DT_REG_ADDR(DT_NODELABEL(uart2)),
			      DT_REG_SIZE(DT_NODELABEL(uart2)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("ANA_PLL",
			      DT_REG_ADDR(DT_NODELABEL(ana_pll)),
			      DT_REG_SIZE(DT_NODELABEL(ana_pll)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("UART4",
			      DT_REG_ADDR(DT_NODELABEL(uart4)),
			      DT_REG_SIZE(DT_NODELABEL(uart4)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("IOMUXC",
			      DT_REG_ADDR(DT_NODELABEL(iomuxc)),
			      DT_REG_SIZE(DT_NODELABEL(iomuxc)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

#ifdef CONFIG_HAS_MCUX_GPT
	MMU_REGION_FLAT_ENTRY("GPT0",
			      DT_REG_ADDR(DT_INST(0, nxp_imx_gpt)),
			      DT_REG_SIZE(DT_INST(0, nxp_imx_gpt)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("GPT1",
			      DT_REG_ADDR(DT_INST(1, nxp_imx_gpt)),
			      DT_REG_SIZE(DT_INST(1, nxp_imx_gpt)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_NA | MT_NS),
#endif

	MMU_REGION_FLAT_ENTRY("RDC",
			      DT_REG_ADDR(DT_NODELABEL(rdc)),
			      DT_REG_SIZE(DT_NODELABEL(rdc)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ram_console), okay)
	MMU_REGION_FLAT_ENTRY("RAM_CONSOLE",
			      DT_REG_ADDR(DT_NODELABEL(ram_console)),
			      DT_REG_SIZE(DT_NODELABEL(ram_console)),
			      MT_NORMAL_NC | MT_P_RW_U_NA | MT_NS),
#endif
};

const struct arm_mmu_config mmu_config = {
	.num_regions = ARRAY_SIZE(mmu_regions),
	.mmu_regions = mmu_regions,
};
