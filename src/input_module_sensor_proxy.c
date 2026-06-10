#define DT_DRV_COMPAT zmk_input_module_sensor_proxy

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zmk/input_module.h>

LOG_MODULE_DECLARE(zmk_input_module, CONFIG_ZMK_INPUT_MODULE_LOG_LEVEL);

struct input_module_sensor_route_config {
	uint32_t profile_id;
	const struct device *sensor;
	uint16_t triggers_per_rotation;
};

struct input_module_sensor_proxy_config {
	const struct input_module_sensor_route_config *routes;
	size_t routes_len;
};

struct input_module_sensor_proxy_data {
	const struct input_module_sensor_route_config *active_route;
	const struct device *trigger_route_sensor;
	const struct device *dev;
	const struct sensor_trigger *trigger;
	sensor_trigger_handler_t handler;
	struct sensor_trigger route_trigger;
	struct k_work_delayable trigger_attach_work;
	struct k_work trigger_work;
	atomic_t trigger_pending;
	struct sensor_value remainder;
	uint8_t trigger_attach_retries;
	bool trigger_attached;
};

static int attach_active_route_trigger(const struct device *dev);
static void input_module_sensor_proxy_route_trigger_handler(const struct device *route_sensor,
							    const struct sensor_trigger *trig);

static const struct input_module_sensor_route_config *
find_active_route(const struct device *dev)
{
	const struct input_module_sensor_proxy_config *config = dev->config;
	uint32_t profile_id = zmk_input_module_is_applied() ? zmk_input_module_applied_get()
							    : zmk_input_module_selected_get();

	for (size_t i = 0; i < config->routes_len; i++) {
		if (config->routes[i].profile_id == profile_id) {
			return &config->routes[i];
		}
	}

	return NULL;
}

static const struct input_module_sensor_route_config *
refresh_active_route(const struct device *dev)
{
	struct input_module_sensor_proxy_data *data = dev->data;
	const struct input_module_sensor_route_config *route = find_active_route(dev);

	if (route != data->active_route) {
		data->active_route = route;
		data->trigger_route_sensor = NULL;
		data->trigger_attached = false;
		data->trigger_attach_retries = 0;
		data->remainder = (struct sensor_value){0};
	}

	return route;
}

static void schedule_trigger_attach_retry(const struct device *dev)
{
	struct input_module_sensor_proxy_data *data = dev->data;

	if (data->trigger_attach_retries >= 50) {
		LOG_WRN("%s giving up sensor trigger attach after %u retries", dev->name,
			data->trigger_attach_retries);
		return;
	}

	data->trigger_attach_retries++;
	k_work_reschedule(&data->trigger_attach_work, K_MSEC(20));
}

static int attach_active_route_trigger(const struct device *dev)
{
	const struct input_module_sensor_route_config *route = refresh_active_route(dev);
	struct input_module_sensor_proxy_data *data = dev->data;
	int ret;

	if (data->trigger == NULL || data->handler == NULL) {
		return 0;
	}

	if (route == NULL) {
		LOG_DBG("%s has no active sensor route for profile %s", dev->name,
			zmk_input_module_profile_name(zmk_input_module_applied_get()));
		return 0;
	}

	if (data->trigger_attached && data->trigger_route_sensor == route->sensor) {
		return 0;
	}

	if (IS_ENABLED(CONFIG_ZMK_INPUT_MODULE_SENSOR_PROXY_SKIP_TRIGGER_ATTACH)) {
		LOG_INF("%s skipping sensor trigger attach to %s for profile %s", dev->name,
			route->sensor->name, zmk_input_module_profile_name(zmk_input_module_applied_get()));
		data->trigger_route_sensor = route->sensor;
		data->trigger_attached = true;
		data->trigger_attach_retries = 0;
		return 0;
	}

	if (!device_is_ready(route->sensor)) {
		if (data->trigger_attach_retries == 0) {
			LOG_WRN("%s selected sensor %s is not ready; retrying trigger attach",
				dev->name, route->sensor->name);
		} else {
			LOG_DBG("%s selected sensor %s is not ready; retry %u", dev->name,
				route->sensor->name, data->trigger_attach_retries);
		}
		schedule_trigger_attach_retry(dev);
		return 0;
	}

	data->route_trigger = *data->trigger;
	ret = sensor_trigger_set(route->sensor, &data->route_trigger,
				 input_module_sensor_proxy_route_trigger_handler);
	if (ret < 0) {
		LOG_WRN("%s failed to attach sensor trigger to %s: %d", dev->name,
			route->sensor->name, ret);
		schedule_trigger_attach_retry(dev);
		return ret;
	}

	data->trigger_route_sensor = route->sensor;
	data->trigger_attached = true;
	data->trigger_attach_retries = 0;
	LOG_INF("%s attached sensor trigger to %s for profile %s", dev->name,
		route->sensor->name, zmk_input_module_profile_name(zmk_input_module_applied_get()));

	return 0;
}

static void trigger_attach_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct input_module_sensor_proxy_data *data =
		CONTAINER_OF(dwork, struct input_module_sensor_proxy_data, trigger_attach_work);

	if (data->dev != NULL) {
		(void)attach_active_route_trigger(data->dev);
	}
}

static void trigger_work_handler(struct k_work *work)
{
	struct input_module_sensor_proxy_data *data =
		CONTAINER_OF(work, struct input_module_sensor_proxy_data, trigger_work);

	if (!atomic_cas(&data->trigger_pending, 1, 0)) {
		return;
	}

	if (data->dev != NULL && data->trigger != NULL && data->handler != NULL) {
		data->handler(data->dev, data->trigger);
	}

	if (atomic_get(&data->trigger_pending) != 0) {
		k_work_submit(&data->trigger_work);
	}
}

static void input_module_sensor_proxy_route_trigger_handler(const struct device *route_sensor,
							    const struct sensor_trigger *trig)
{
	struct input_module_sensor_proxy_data *data =
		CONTAINER_OF(trig, struct input_module_sensor_proxy_data, route_trigger);

	ARG_UNUSED(route_sensor);

	if (CONFIG_ZMK_INPUT_MODULE_SENSOR_PROXY_TRIGGER_DEBOUNCE_MS > 0) {
		k_msleep(CONFIG_ZMK_INPUT_MODULE_SENSOR_PROXY_TRIGGER_DEBOUNCE_MS);
	}

	if (IS_ENABLED(CONFIG_ZMK_INPUT_MODULE_SENSOR_PROXY_DROP_TRIGGER_EVENTS)) {
		return;
	}

	if (IS_ENABLED(CONFIG_ZMK_INPUT_MODULE_SENSOR_PROXY_SAMPLE_ONLY_TRIGGER_EVENTS)) {
		if (data->dev != NULL) {
			(void)sensor_sample_fetch(data->dev);
		}
		return;
	}

	if (IS_ENABLED(CONFIG_ZMK_INPUT_MODULE_SENSOR_PROXY_FETCH_ONLY_TRIGGER_EVENTS)) {
		struct sensor_value value;

		if (data->dev != NULL && data->trigger != NULL) {
			(void)sensor_sample_fetch(data->dev);
			(void)sensor_channel_get(data->dev, data->trigger->chan, &value);
		}
		return;
	}

	if (IS_ENABLED(CONFIG_ZMK_INPUT_MODULE_SENSOR_PROXY_DIRECT_TRIGGER_HANDLER)) {
		if (data->dev != NULL && data->trigger != NULL && data->handler != NULL) {
			data->handler(data->dev, data->trigger);
		}
		return;
	}

	atomic_set(&data->trigger_pending, 1);
	k_work_submit(&data->trigger_work);
}

static int input_module_sensor_proxy_sample_fetch(const struct device *dev,
						  enum sensor_channel chan)
{
	const struct input_module_sensor_route_config *route = refresh_active_route(dev);

	if (route == NULL) {
		return -ENODEV;
	}

	return sensor_sample_fetch_chan(route->sensor, chan);
}

static int normalize_rotation_value(const struct device *dev,
				    const struct input_module_sensor_route_config *route,
				    const struct sensor_value *raw, struct sensor_value *val)
{
	struct input_module_sensor_proxy_data *data = dev->data;
	int trigger_degrees;
	int triggers;

	if (route->triggers_per_rotation == 0) {
		*val = *raw;
		return 0;
	}

	trigger_degrees = 360 / route->triggers_per_rotation;
	if (trigger_degrees <= 0) {
		return -EINVAL;
	}

	data->remainder.val1 += raw->val1;
	data->remainder.val2 += raw->val2;
	data->remainder.val1 += data->remainder.val2 / 1000000;
	data->remainder.val2 %= 1000000;

	triggers = data->remainder.val1 / trigger_degrees;
	data->remainder.val1 %= trigger_degrees;

	*val = (struct sensor_value){
		.val1 = 0,
		.val2 = triggers,
	};

	LOG_DBG("%s normalized %s rotation raw=%d.%06d remainder=%d.%06d triggers=%d",
		dev->name, route->sensor->name, raw->val1, raw->val2, data->remainder.val1,
		data->remainder.val2, triggers);

	return 0;
}

static int input_module_sensor_proxy_channel_get(const struct device *dev,
						 enum sensor_channel chan,
						 struct sensor_value *val)
{
	const struct input_module_sensor_route_config *route = refresh_active_route(dev);
	struct sensor_value raw;
	int ret;

	if (route == NULL) {
		return -ENODEV;
	}

	ret = sensor_channel_get(route->sensor, chan, &raw);
	if (ret < 0) {
		return ret;
	}

	if (chan == SENSOR_CHAN_ROTATION) {
		return normalize_rotation_value(dev, route, &raw, val);
	}

	*val = raw;
	return 0;
}

static int input_module_sensor_proxy_trigger_set(const struct device *dev,
						 const struct sensor_trigger *trig,
						 sensor_trigger_handler_t handler)
{
	struct input_module_sensor_proxy_data *data = dev->data;

	data->trigger = trig;
	data->handler = handler;
	data->trigger_route_sensor = NULL;
	data->trigger_attached = false;
	data->trigger_attach_retries = 0;
	atomic_clear(&data->trigger_pending);

	return attach_active_route_trigger(dev);
}

static const struct sensor_driver_api input_module_sensor_proxy_api = {
	.trigger_set = input_module_sensor_proxy_trigger_set,
	.sample_fetch = input_module_sensor_proxy_sample_fetch,
	.channel_get = input_module_sensor_proxy_channel_get,
};

static int input_module_sensor_proxy_init(const struct device *dev)
{
	struct input_module_sensor_proxy_data *data = dev->data;

	data->dev = dev;
	k_work_init_delayable(&data->trigger_attach_work, trigger_attach_work_handler);
	k_work_init(&data->trigger_work, trigger_work_handler);

	return 0;
}

#define ROUTE_ENTRY(node_id)                                                                       \
	{                                                                                          \
		.profile_id = DT_PROP(node_id, profile_id),                                        \
		.sensor = DEVICE_DT_GET(DT_PHANDLE(node_id, sensor)),                              \
		.triggers_per_rotation = DT_PROP_OR(node_id, triggers_per_rotation, 0),             \
	},

#define ROUTES_NAME(n) _CONCAT(input_module_sensor_proxy_routes_, n)

#define SENSOR_PROXY_DEFINE(n)                                                                     \
	static const struct input_module_sensor_route_config ROUTES_NAME(n)[] = {                   \
		DT_INST_FOREACH_CHILD(n, ROUTE_ENTRY)};                                            \
	static const struct input_module_sensor_proxy_config input_module_sensor_proxy_config_##n = {\
		.routes = ROUTES_NAME(n),                                                        \
		.routes_len = ARRAY_SIZE(ROUTES_NAME(n)),                                        \
	};                                                                                         \
	static struct input_module_sensor_proxy_data input_module_sensor_proxy_data_##n;             \
	DEVICE_DT_INST_DEFINE(n, input_module_sensor_proxy_init, NULL,                               \
			      &input_module_sensor_proxy_data_##n,                                  \
			      &input_module_sensor_proxy_config_##n, POST_KERNEL,                   \
			      CONFIG_SENSOR_INIT_PRIORITY, &input_module_sensor_proxy_api);

DT_INST_FOREACH_STATUS_OKAY(SENSOR_PROXY_DEFINE)
