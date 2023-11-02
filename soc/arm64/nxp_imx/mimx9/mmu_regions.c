/*
 * Copyright 2022 NXP
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

	MMU_REGION_FLAT_ENTRY("UART2",
			      DT_REG_ADDR(DT_NODELABEL(lpuart2)),
			      DT_REG_SIZE(DT_NODELABEL(lpuart2)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("IOMUXC",
			      DT_REG_ADDR(DT_NODELABEL(iomuxc)),
			      DT_REG_SIZE(DT_NODELABEL(iomuxc)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

#if CONFIG_SOF
	MMU_REGION_FLAT_ENTRY("MU2_A",
			      DT_REG_ADDR(DT_NODELABEL(mu2_a)),
			      DT_REG_SIZE(DT_NODELABEL(mu2_a)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("SAI3",
			      DT_REG_ADDR(DT_NODELABEL(sai3)),
			      DT_REG_SIZE(DT_NODELABEL(sai3)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("EDMA2_CH0",
			      DT_REG_ADDR(DT_NODELABEL(edma2_ch0)),
			      DT_REG_SIZE(DT_NODELABEL(edma2_ch0)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("EDMA2_CH1",
			      DT_REG_ADDR(DT_NODELABEL(edma2_ch1)),
			      DT_REG_SIZE(DT_NODELABEL(edma2_ch1)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("OUTBOX",
			      DT_REG_ADDR(DT_NODELABEL(outbox)),
			      DT_REG_SIZE(DT_NODELABEL(outbox)),
			      MT_NORMAL | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("INBOX",
			      DT_REG_ADDR(DT_NODELABEL(inbox)),
			      DT_REG_SIZE(DT_NODELABEL(inbox)),
			      MT_NORMAL | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("STREAM",
			      DT_REG_ADDR(DT_NODELABEL(stream)),
			      DT_REG_SIZE(DT_NODELABEL(stream)),
			      MT_NORMAL | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("HOST_RAM",
			      DT_REG_ADDR(DT_NODELABEL(host_ram)),
			      DT_REG_SIZE(DT_NODELABEL(host_ram)),
			      MT_NORMAL | MT_P_RW_U_NA | MT_NS),
#endif /* CONFIG_SOF */
	MMU_REGION_FLAT_ENTRY("WAKEUPMIX1_GPR",
			      DT_REG_ADDR(DT_NODELABEL(wakeupmix_gpr)),
			      DT_REG_SIZE(DT_NODELABEL(wakeupmix_gpr)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("LPI2C1",
			      DT_REG_ADDR(DT_INST(0, nxp_imx_lpi2c)),
			      DT_REG_SIZE(DT_INST(0, nxp_imx_lpi2c)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_RW | MT_NS),

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

#ifdef CONFIG_HAS_MCUX_TPM
	MMU_REGION_FLAT_ENTRY("TPM2",
			      DT_REG_ADDR(DT_INST(0, nxp_tpm_timer)),
			      DT_REG_SIZE(DT_INST(0, nxp_tpm_timer)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("TPM4",
			      DT_REG_ADDR(DT_INST(1, nxp_tpm_timer)),
			      DT_REG_SIZE(DT_INST(1, nxp_tpm_timer)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),
#endif
#ifdef CONFIG_HAS_MCUX_FLEXCAN
	MMU_REGION_FLAT_ENTRY("FLEXCAN",
			      DT_REG_ADDR(DT_INST(0, nxp_imx_flexcan)),
			      DT_REG_SIZE(DT_INST(0, nxp_imx_flexcan)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_NA | MT_NS),
#endif

#ifdef CONFIG_HAS_MCUX_ENET
	MMU_REGION_FLAT_ENTRY("ENET",
			      DT_REG_ADDR(DT_INST(0, nxp_imx_enet)),
			      DT_REG_SIZE(DT_INST(0, nxp_imx_enet)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_RW | MT_NS),
#endif

#ifdef CONFIG_HAS_MCUX_ENET_QOS
	MMU_REGION_FLAT_ENTRY("ENET_QOS",
			      DT_REG_ADDR(DT_INST(0, nxp_imx_enet_qos)),
			      DT_REG_SIZE(DT_INST(0, nxp_imx_enet_qos)),
			      MT_DEVICE_nGnRE | MT_P_RW_U_RW | MT_NS),
#endif
};

const struct arm_mmu_config mmu_config = {
	.num_regions = ARRAY_SIZE(mmu_regions),
	.mmu_regions = mmu_regions,
};
