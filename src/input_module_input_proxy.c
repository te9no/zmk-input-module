#define DT_DRV_COMPAT zmk_input_module_input_proxy

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/input_module.h>

LOG_MODULE_DECLARE(zmk_input_module, CONFIG_ZMK_INPUT_MODULE_LOG_LEVEL);

struct input_module_input_route_config {
	uint32_t profile_id;
	const struct device *input;
};

struct input_module_input_proxy_config {
	const struct input_module_input_route_config *routes;
	size_t routes_len;
};

static const struct input_module_input_route_config *
find_active_route(const struct device *dev)
{
	const struct input_module_input_proxy_config *config = dev->config;
	uint32_t profile_id = zmk_input_module_is_applied() ? zmk_input_module_applied_get()
							    : zmk_input_module_selected_get();

	for (size_t i = 0; i < config->routes_len; i++) {
		if (config->routes[i].profile_id == profile_id) {
			return &config->routes[i];
		}
	}

	return NULL;
}

static void input_module_input_proxy_forward(const struct device *dev, struct input_event *evt)
{
	const struct input_module_input_route_config *route = find_active_route(dev);

	if (route == NULL || evt->dev != route->input) {
		return;
	}

	int ret = input_report(dev, evt->type, evt->code, evt->value, evt->sync, K_NO_WAIT);

	if (ret < 0) {
		LOG_WRN("failed to forward input event from %s via %s: %d", evt->dev->name,
			dev->name, ret);
	}
}

#define ROUTE_ENTRY(node_id)                                                                       \
	{                                                                                          \
		.profile_id = DT_PROP(node_id, profile_id),                                        \
		.input = DEVICE_DT_GET(DT_PHANDLE(node_id, input)),                                \
	},

#define ROUTES_NAME(n) _CONCAT(input_module_input_proxy_routes_, n)
#define ROUTE_CALLBACK_NAME(node_id) _CONCAT(input_module_input_proxy_callback_, node_id)
#define ROUTE_LISTENER_NAME(node_id) _CONCAT(input_module_input_proxy_listener_, node_id)

#define ROUTE_CALLBACK_DEFINE(node_id, inst)                                                       \
	static void ROUTE_CALLBACK_NAME(node_id)(struct input_event *evt)                          \
	{                                                                                          \
		input_module_input_proxy_forward(DEVICE_DT_GET(DT_DRV_INST(inst)), evt);           \
	}                                                                                          \
	static const STRUCT_SECTION_ITERABLE(input_listener, ROUTE_LISTENER_NAME(node_id)) = {    \
		.dev = DEVICE_DT_GET(DT_PHANDLE(node_id, input)),                                 \
		.callback = ROUTE_CALLBACK_NAME(node_id),                                        \
	};

#define INPUT_PROXY_DEFINE(n)                                                                      \
	static const struct input_module_input_route_config ROUTES_NAME(n)[] = {                  \
		DT_INST_FOREACH_CHILD(n, ROUTE_ENTRY)};                                           \
	static const struct input_module_input_proxy_config input_module_input_proxy_config_##n = {\
		.routes = ROUTES_NAME(n),                                                        \
		.routes_len = ARRAY_SIZE(ROUTES_NAME(n)),                                        \
	};                                                                                         \
	DT_INST_FOREACH_CHILD_VARGS(n, ROUTE_CALLBACK_DEFINE, n)                                  \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &input_module_input_proxy_config_##n,          \
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(INPUT_PROXY_DEFINE)
