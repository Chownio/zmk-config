# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT

target_sources(app PRIVATE src/behavior_display_soft_off.c)
target_link_options(app PRIVATE -Wl,--wrap=sys_poweroff)
