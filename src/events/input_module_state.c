/*
 * Copyright (c) 2026 The DYA Contributors
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <limits.h>

#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/input_module_state.h>
#include <zmk/input_module.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/central.h>
#endif

LOG_MODULE_DECLARE(zmk_input_module, CONFIG_ZMK_INPUT_MODULE_LOG_LEVEL);

ZMK_EVENT_IMPL(zmk_input_module_state_request);
ZMK_EVENT_IMPL(zmk_input_module_select_request);
ZMK_EVENT_IMPL(zmk_input_module_state_report);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)

ZMK_RELAY_EVENT_CENTRAL_TO_PERIPHERAL(zmk_input_module_state_request, imq, );
ZMK_RELAY_EVENT_HANDLE(zmk_input_module_state_request, imq, );

ZMK_RELAY_EVENT_CENTRAL_TO_PERIPHERAL(zmk_input_module_select_request, ims, );
ZMK_RELAY_EVENT_HANDLE(zmk_input_module_select_request, ims, );

ZMK_RELAY_EVENT_PERIPHERAL_TO_CENTRAL(zmk_input_module_state_report, imr, source);
ZMK_RELAY_EVENT_HANDLE(zmk_input_module_state_report, imr, source);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static int8_t status_to_i8(int status)
{
	return status < INT8_MIN ? INT8_MIN : (status > INT8_MAX ? INT8_MAX : status);
}

static uint16_t profile_id_to_u16(uint32_t profile_id)
{
	return profile_id > UINT16_MAX ? UINT16_MAX : profile_id;
}

static void raise_state_report(uint8_t request_id, int status)
{
	struct zmk_input_module_state_report report = {
		.selected_profile_id = profile_id_to_u16(zmk_input_module_selected_get()),
		.applied_profile_id = profile_id_to_u16(zmk_input_module_applied_get()),
		.available_profile_mask = zmk_input_module_available_profile_mask(),
		.status = status_to_i8(status),
		.applied = zmk_input_module_is_applied(),
		.source = ZMK_RELAY_EVENT_SOURCE_SELF,
		.request_id = request_id,
	};

	raise_zmk_input_module_state_report(report);
}

static int input_module_state_request_listener(const zmk_event_t *eh)
{
	struct zmk_input_module_state_request *ev = as_zmk_input_module_state_request(eh);

	if (ev == NULL) {
		return ZMK_EV_EVENT_BUBBLE;
	}

	raise_state_report(ev->request_id, 0);
	return ZMK_EV_EVENT_HANDLED;
}

static int input_module_select_request_listener(const zmk_event_t *eh)
{
	struct zmk_input_module_select_request *ev = as_zmk_input_module_select_request(eh);

	if (ev == NULL) {
		return ZMK_EV_EVENT_BUBBLE;
	}

	int ret = zmk_input_module_select_set(ev->profile_id);

	raise_state_report(ev->request_id, ret);
	return ZMK_EV_EVENT_HANDLED;
}

ZMK_LISTENER(input_module_state_request_handler, input_module_state_request_listener);
ZMK_SUBSCRIPTION(input_module_state_request_handler, zmk_input_module_state_request);

ZMK_LISTENER(input_module_select_request_handler, input_module_select_request_listener);
ZMK_SUBSCRIPTION(input_module_select_request_handler, zmk_input_module_select_request);

#endif /* !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT) */
