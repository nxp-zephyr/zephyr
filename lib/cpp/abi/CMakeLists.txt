# SPDX-License-Identifier: Apache-2.0

zephyr_sources_ifdef(CONFIG_STATIC_INIT_GNU
  cpp_dtors.c
)
