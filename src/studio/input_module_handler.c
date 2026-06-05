/*
 * Copyright (c) 2026 The DYA Contributors
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <dt-bindings/zmk/input_module.h>
#include <dya/input_module/input_module.pb.h>
#include <zmk/event_manager.h>
#include <zmk/events/input_module_state.h>
#include <zmk/input_module.h>
#include <zmk/studio/custom.h>

LOG_MODULE_DECLARE(zmk_input_module, CONFIG_ZMK_INPUT_MODULE_LOG_LEVEL);

static struct zmk_rpc_custom_subsystem_meta input_module_meta = {
	ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS(CONFIG_ZMK_INPUT_MODULE_STUDIO_RPC_UI_URL),
	.security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

ZMK_RPC_CUSTOM_SUBSYSTEM(dya__input_module, &input_module_meta,
			 input_module_rpc_handle_request);
ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(dya__input_module, dya_input_module_Response);

static uint8_t request_id;

static void set_error_response(dya_input_module_Response *resp, const char *message)
{
	dya_input_module_ErrorResponse err = dya_input_module_ErrorResponse_init_zero;

	snprintf(err.message, sizeof(err.message), "%s", message);
	resp->which_response_type = dya_input_module_Response_error_tag;
	resp->response_type.error = err;
}

static dya_input_module_ModuleCapabilities proto_capabilities(uint32_t flags)
{
	return (dya_input_module_ModuleCapabilities){
		.flags = flags,
		.kscan = (flags & ZMK_INPUT_MODULE_CAP_KSCAN) != 0,
		.encoder = (flags & ZMK_INPUT_MODULE_CAP_ENCODER) != 0,
		.adc = (flags & ZMK_INPUT_MODULE_CAP_ADC) != 0,
		.spi = (flags & ZMK_INPUT_MODULE_CAP_SPI) != 0,
		.i2c = (flags & ZMK_INPUT_MODULE_CAP_I2C) != 0,
	};
}

static void fill_profile(dya_input_module_ModuleProfile *dst,
			 const struct zmk_input_module_profile *src, uint32_t selected,
			 uint32_t applied, bool is_applied)
{
	*dst = (dya_input_module_ModuleProfile)dya_input_module_ModuleProfile_init_zero;
	dst->id = src->id;
	snprintf(dst->name, sizeof(dst->name), "%s", src->name);
	dst->capabilities = proto_capabilities(src->capabilities);
	dst->selected = src->id == selected;
	dst->applied = is_applied && src->id == applied;
	dst->available = src->available;
}

static void fill_state(dya_input_module_ModuleState *state, uint32_t source, int status)
{
	uint32_t selected = zmk_input_module_selected_get();
	uint32_t applied = zmk_input_module_applied_get();
	size_t profile_count = zmk_input_module_profile_count();
	size_t out_count = MIN(profile_count, ARRAY_SIZE(state->profiles));

	*state = (dya_input_module_ModuleState)dya_input_module_ModuleState_init_zero;
	state->selected_profile_id = selected;
	snprintf(state->selected_profile_name, sizeof(state->selected_profile_name), "%s",
		 zmk_input_module_profile_name(selected));
	state->applied_profile_id = applied;
	snprintf(state->applied_profile_name, sizeof(state->applied_profile_name), "%s",
		 zmk_input_module_profile_name(applied));
	state->applied = zmk_input_module_is_applied();
	state->reboot_required = !state->applied || selected != applied;
	state->source = source;
	state->status = status;
	state->profiles_count = out_count;

	for (size_t i = 0; i < out_count; i++) {
		struct zmk_input_module_profile profile;
		int ret = zmk_input_module_profile_get(i, &profile);

		if (ret != 0) {
			LOG_WRN("Failed to read input module profile %u: %d", (uint32_t)i, ret);
			continue;
		}

		fill_profile(&state->profiles[i], &profile, selected, applied, state->applied);
	}
}

static bool encode_state_notification(pb_ostream_t *stream, const pb_field_t *field,
				      void *const *arg)
{
	dya_input_module_Notification *notification = (dya_input_module_Notification *)*arg;

	if (!pb_encode_tag_for_field(stream, field)) {
		return false;
	}

	size_t size;
	if (!pb_get_encoded_size(&size, dya_input_module_Notification_fields, notification)) {
		LOG_WRN("Failed to get encoded size for input module notification");
		return false;
	}

	if (!pb_encode_varint(stream, size)) {
		return false;
	}

	return pb_encode(stream, dya_input_module_Notification_fields, notification);
}

static int get_subsystem_index(void)
{
	size_t subsystem_count;

	STRUCT_SECTION_COUNT(zmk_rpc_custom_subsystem, &subsystem_count);

	for (size_t i = 0; i < subsystem_count; i++) {
		struct zmk_rpc_custom_subsystem *custom_subsys;

		STRUCT_SECTION_GET(zmk_rpc_custom_subsystem, i, &custom_subsys);
		if (strcmp(custom_subsys->identifier, "dya__input_module") == 0) {
			return (int)i;
		}
	}

	return -ENOENT;
}

static void send_state_notification(const dya_input_module_ModuleState *state)
{
	int subsystem_idx = get_subsystem_index();

	if (subsystem_idx < 0) {
		LOG_ERR("Failed to get input module subsystem index");
		return;
	}

	dya_input_module_Notification notification = dya_input_module_Notification_init_zero;
	notification.which_notification_type = dya_input_module_Notification_state_tag;
	notification.notification_type.state.has_state = true;
	notification.notification_type.state.state = *state;

	struct zmk_studio_custom_notification event = {
		.subsystem_index = subsystem_idx,
		.encode_payload =
			{
				.funcs = {.encode = encode_state_notification},
				.arg = &notification,
			},
	};

	raise_zmk_studio_custom_notification(event);
}

static void handle_get_state(dya_input_module_Response *resp)
{
	dya_input_module_GetStateResponse result = dya_input_module_GetStateResponse_init_zero;

	fill_state(&result.state, 0, 0);

	resp->which_response_type = dya_input_module_Response_get_state_tag;
	resp->response_type.get_state = result;
}

static void handle_get_all_states(dya_input_module_Response *resp)
{
	dya_input_module_ModuleState local = dya_input_module_ModuleState_init_zero;

	fill_state(&local, 0, 0);
	send_state_notification(&local);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
	request_id++;
	if (request_id == 0) {
		request_id = 1;
	}

	struct zmk_input_module_state_request ev = {
		.request_id = request_id,
	};

	raise_zmk_input_module_state_request(ev);
#endif

	dya_input_module_GetAllStatesResponse result =
		dya_input_module_GetAllStatesResponse_init_zero;
	result.request_sent = true;

	resp->which_response_type = dya_input_module_Response_get_all_states_tag;
	resp->response_type.get_all_states = result;
}

static void handle_set_selected(const dya_input_module_SetSelectedRequest *req,
				dya_input_module_Response *resp)
{
	dya_input_module_SetSelectedResponse result =
		dya_input_module_SetSelectedResponse_init_zero;

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
	if (req->target > 0) {
		request_id++;
		if (request_id == 0) {
			request_id = 1;
		}

		struct zmk_input_module_select_request ev = {
			.profile_id = req->profile_id,
			.request_id = request_id,
		};

		raise_zmk_input_module_select_request(ev);

		result.request_sent = true;
		result.target = req->target;
		fill_state(&result.state, 0, 0);
		resp->which_response_type = dya_input_module_Response_set_selected_tag;
		resp->response_type.set_selected = result;
		return;
	}
#endif

	int ret = zmk_input_module_select_set(req->profile_id);

	if (ret != 0) {
		set_error_response(resp, ret == -EINVAL ? "invalid input module profile"
							: "failed to select input module profile");
		return;
	}

	fill_state(&result.state, 0, 0);
	send_state_notification(&result.state);

	result.request_sent = true;
	result.target = req->target;

	resp->which_response_type = dya_input_module_Response_set_selected_tag;
	resp->response_type.set_selected = result;
}

static bool input_module_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
					    pb_callback_t *encode_response)
{
	dya_input_module_Response *resp =
		ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(dya__input_module,
								  encode_response);

	dya_input_module_Request req = dya_input_module_Request_init_zero;
	pb_istream_t req_stream =
		pb_istream_from_buffer(raw_request->payload.bytes, raw_request->payload.size);

	if (!pb_decode(&req_stream, dya_input_module_Request_fields, &req)) {
		LOG_WRN("Failed to decode input module request: %s", PB_GET_ERROR(&req_stream));
		set_error_response(resp, "failed to decode request");
		return true;
	}

	switch (req.which_request_type) {
	case dya_input_module_Request_get_state_tag:
		handle_get_state(resp);
		break;
	case dya_input_module_Request_get_all_states_tag:
		handle_get_all_states(resp);
		break;
	case dya_input_module_Request_set_selected_tag:
		handle_set_selected(&req.request_type.set_selected, resp);
		break;
	default:
		LOG_WRN("Unsupported input module request type: %d", req.which_request_type);
		set_error_response(resp, "unsupported request");
		break;
	}

	return true;
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static int input_module_state_report_listener(const zmk_event_t *eh)
{
	struct zmk_input_module_state_report *ev = as_zmk_input_module_state_report(eh);

	if (ev == NULL || ev->source == ZMK_RELAY_EVENT_SOURCE_SELF) {
		return ZMK_EV_EVENT_BUBBLE;
	}

	dya_input_module_ModuleState state = dya_input_module_ModuleState_init_zero;

	fill_state(&state, ev->source, ev->status);
	state.selected_profile_id = ev->selected_profile_id;
	snprintf(state.selected_profile_name, sizeof(state.selected_profile_name), "%s",
		 zmk_input_module_profile_name(ev->selected_profile_id));
	state.applied_profile_id = ev->applied_profile_id;
	snprintf(state.applied_profile_name, sizeof(state.applied_profile_name), "%s",
		 zmk_input_module_profile_name(ev->applied_profile_id));
	state.applied = ev->applied;
	state.reboot_required = !ev->applied || ev->selected_profile_id != ev->applied_profile_id;

	for (pb_size_t i = 0; i < state.profiles_count; i++) {
		bool available = state.profiles[i].id < 32 &&
				 ((ev->available_profile_mask & BIT(state.profiles[i].id)) != 0);

		state.profiles[i].available = available;
		state.profiles[i].selected = state.profiles[i].id == ev->selected_profile_id;
		state.profiles[i].applied = ev->applied && state.profiles[i].id == ev->applied_profile_id;
	}

	send_state_notification(&state);
	return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(input_module_studio_state_report_handler, input_module_state_report_listener);
ZMK_SUBSCRIPTION(input_module_studio_state_report_handler, zmk_input_module_state_report);

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */
