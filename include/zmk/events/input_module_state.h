/*
 * Copyright (c) 2026 The DYA Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/toolchain.h>
#include <zmk/event_manager.h>

struct zmk_input_module_state_request {
	uint8_t request_id;
};

struct zmk_input_module_select_request {
	uint32_t profile_id;
	uint8_t request_id;
};

struct zmk_input_module_state_report {
	uint16_t selected_profile_id;
	uint16_t applied_profile_id;
	uint32_t available_profile_mask;
	int8_t status;
	bool applied;
	uint8_t source;
	uint8_t request_id;
} __packed;

ZMK_EVENT_DECLARE(zmk_input_module_state_request);
ZMK_EVENT_DECLARE(zmk_input_module_select_request);
ZMK_EVENT_DECLARE(zmk_input_module_state_report);
