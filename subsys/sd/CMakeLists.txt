# SPDX-License-Identifier: Apache-2.0

if (CONFIG_SD_STACK)

  zephyr_sources (
	sd.c
	sdio.c
	sdmmc.c
  )

endif()
