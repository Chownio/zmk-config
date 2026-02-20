# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK)
  zephyr_library()
  zephyr_library_sources(src/behavior_display_soft_off.c)
endif()
