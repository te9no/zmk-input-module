#define DT_DRV_COMPAT zmk_behavior_input_module_select

#include <drivers/behavior.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/input_module.h>

LOG_MODULE_DECLARE(zmk_input_module, CONFIG_ZMK_INPUT_MODULE_LOG_LEVEL);

static uint32_t pending_profile_id;

static void input_module_select_work_handler(struct k_work *work)
{
	uint32_t profile_id = pending_profile_id;

	LOG_INF("input module select behavior work: profile=%s (%u)",
		zmk_input_module_profile_name(profile_id), profile_id);

	int ret = zmk_input_module_select_set(profile_id);

	zmk_input_module_report_state_deferred(
		0, ret, CONFIG_ZMK_INPUT_MODULE_STATE_REPORT_BEHAVIOR_DELAY_MS);
}

K_WORK_DEFINE(input_module_select_work, input_module_select_work_handler);

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
				     struct zmk_behavior_binding_event event)
{
	LOG_INF("input module select behavior pressed; scheduling profile=%s (%u)",
		zmk_input_module_profile_name(binding->param1), binding->param1);

	pending_profile_id = binding->param1;
	zmk_input_module_submit_work(&input_module_select_work);

	return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
				      struct zmk_behavior_binding_event event)
{
	return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_input_module_select_driver_api = {
	.binding_pressed = on_keymap_binding_pressed,
	.binding_released = on_keymap_binding_released,
	.locality = BEHAVIOR_LOCALITY_EVENT_SOURCE,
};

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define INPUT_MODULE_SELECT_INST(n)                                                           \
	BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                       \
				CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                         \
				&behavior_input_module_select_driver_api);

DT_INST_FOREACH_STATUS_OKAY(INPUT_MODULE_SELECT_INST)

#endif
