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

	MMU_REGION_FLAT_ENTRY("ANA_PLL",
			      DT_REG_ADDR(DT_NODELABEL(ana_pll)),
			      DT_REG_SIZE(DT_NODELABEL(ana_pll)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("IOMUXC",
			      DT_REG_ADDR(DT_NODELABEL(iomuxc)),
			      DT_REG_SIZE(DT_NODELABEL(iomuxc)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("RDC",
			      DT_REG_ADDR(DT_NODELABEL(rdc)),
			      DT_REG_SIZE(DT_NODELABEL(rdc)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("I2C3",
			      DT_REG_ADDR(DT_INST(0, nxp_imx_i2c)),
			      DT_REG_SIZE(DT_INST(0, nxp_imx_i2c)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("SAI1",
			      DT_REG_ADDR(DT_INST(0, nxp_imx_sai)),
			      DT_REG_SIZE(DT_INST(0, nxp_imx_sai)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("SAI2",
			      DT_REG_ADDR(DT_INST(1, nxp_imx_sai)),
			      DT_REG_SIZE(DT_INST(1, nxp_imx_sai)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("SAI3",
			      DT_REG_ADDR(DT_INST(2, nxp_imx_sai)),
			      DT_REG_SIZE(DT_INST(2, nxp_imx_sai)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("SAI5",
			      DT_REG_ADDR(DT_INST(3, nxp_imx_sai)),
			      DT_REG_SIZE(DT_INST(3, nxp_imx_sai)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("SAI6",
			      DT_REG_ADDR(DT_INST(4, nxp_imx_sai)),
			      DT_REG_SIZE(DT_INST(4, nxp_imx_sai)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_NA | MT_NS),

#ifdef CONFIG_HAS_MCUX_ENET
	MMU_REGION_FLAT_ENTRY("ENET",
			      DT_REG_ADDR(DT_INST(0, nxp_imx_enet)),
			      DT_REG_SIZE(DT_INST(0, nxp_imx_enet)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_RW | MT_NS),
#endif

	MMU_REGION_DT_COMPAT_FOREACH_FLAT_ENTRY(nxp_imx_iuart,
				  (MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS))
};

const struct arm_mmu_config mmu_config = {
	.num_regions = ARRAY_SIZE(mmu_regions),
	.mmu_regions = mmu_regions,
};
