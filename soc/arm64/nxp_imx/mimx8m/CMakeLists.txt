# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)

if(CONFIG_ARM_MMU)
zephyr_sources_ifdef(CONFIG_SOC_MIMX8MP_A53 mmu_regions_mimx8mp.c)
zephyr_sources_ifdef(CONFIG_SOC_MIMX8MN_A53 mmu_regions_mimx8mn.c)
zephyr_sources_ifdef(CONFIG_SOC_MIMX8MM_A53 mmu_regions_mimx8mm.c)
endif()
