/* Demo device: the XIAO nRF52840's RGB LED and die temperature, described
 * to the page as controls. A real device replaces this file; src/tapui/
 * stays as is. */
#include <stdlib.h>
#include <string.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "tapui/tapui.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define UI_JSON                                                                                    \
	"{\"title\":\"XIAO demo\",\"controls\":["                                                  \
	"{\"type\":\"toggle\",\"id\":\"light\",\"label\":\"Light\"},"                              \
	"{\"type\":\"select\",\"id\":\"color\",\"label\":\"Colour\",\"options\":"                  \
	"[\"red\",\"green\",\"blue\",\"white\",\"cyan\",\"magenta\",\"yellow\"]},"                 \
	"{\"type\":\"slider\",\"id\":\"bright\",\"label\":\"Brightness\",\"min\":0,\"max\":100,"   \
	"\"step\":5,\"unit\":\"%\"},"                                                              \
	"{\"type\":\"button\",\"id\":\"blink\",\"label\":\"Blink 3 times\"},"                      \
	"{\"type\":\"readout\",\"id\":\"temp\",\"label\":\"Chip temperature\",\"unit\":\"°C\"},"   \
	"{\"type\":\"readout\",\"id\":\"uptime\",\"label\":\"Uptime\",\"unit\":\"s\"}"            \
	"]}"

static const struct {
	const char *name;
	uint8_t r, g, b;
} colors[] = {
	{"red", 1, 0, 0},  {"green", 0, 1, 0},   {"blue", 0, 0, 1},   {"white", 1, 1, 1},
	{"cyan", 0, 1, 1}, {"magenta", 1, 0, 1}, {"yellow", 1, 1, 0},
};

static const struct pwm_dt_spec rgb[] = {
	PWM_DT_SPEC_GET(DT_NODELABEL(rgb_r)),
	PWM_DT_SPEC_GET(DT_NODELABEL(rgb_g)),
	PWM_DT_SPEC_GET(DT_NODELABEL(rgb_b)),
};

static const struct device *const die_temp = DEVICE_DT_GET(DT_NODELABEL(temp));

static bool light = true;
static size_t color;
static int bright = 40;
static int blinks_left;

static void show(bool on)
{
	const uint8_t mask[] = {colors[color].r, colors[color].g, colors[color].b};

	for (size_t i = 0; i < ARRAY_SIZE(rgb); i++) {
		uint32_t pulse = (on && mask[i]) ? rgb[i].period / 100 * bright : 0;

		pwm_set_pulse_dt(&rgb[i], pulse);
	}
}

static void blink_step(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(blink_work, blink_step);

static void blink_step(struct k_work *work)
{
	ARG_UNUSED(work);
	/* Odd steps dark, even steps lit, ending lit as the toggle says. */
	blinks_left--;
	show(blinks_left % 2 == 0 ? light : !light);
	if (blinks_left > 0) {
		k_work_schedule(&blink_work, K_MSEC(200));
	}
}

static int read_temp_centi(void)
{
	struct sensor_value v;

	if (sensor_sample_fetch(die_temp) || sensor_channel_get(die_temp, SENSOR_CHAN_DIE_TEMP, &v)) {
		return INT32_MIN;
	}
	return v.val1 * 100 + v.val2 / 10000;
}

static void send_temp(void)
{
	int c = read_temp_centi();
	char buf[16];

	if (c == INT32_MIN) {
		return;
	}
	/* The nRF52's sensor steps in 0.25 °C. */
	snprintf(buf, sizeof(buf), "%d.%02d", c / 100, abs(c % 100));
	tapui_send("{\"t\":\"state\",\"id\":\"temp\",\"v\":%s}", buf);
}

static void on_hello(void)
{
	tapui_send("{\"t\":\"ui\",\"ui\":" UI_JSON "}");
	tapui_send_bool("light", light);
	tapui_send_str("color", colors[color].name);
	tapui_send_int("bright", bright);
	send_temp();
	tapui_send_int("uptime", (int)(k_uptime_get() / 1000));
}

static void on_cmd(const struct tapui_cmd *cmd)
{
	const char *id = cmd->id;

	if (cmd->op == TAPUI_OP_SET && strcmp(id, "light") == 0) {
		light = strcmp(cmd->value, "1") == 0;
		show(light);
		tapui_send_bool(id, light);
	} else if (cmd->op == TAPUI_OP_SET && strcmp(id, "color") == 0) {
		for (size_t i = 0; i < ARRAY_SIZE(colors); i++) {
			if (strcmp(cmd->value, colors[i].name) == 0) {
				color = i;
			}
		}
		show(light);
		tapui_send_str(id, colors[color].name);
	} else if (cmd->op == TAPUI_OP_SET && strcmp(id, "bright") == 0) {
		bright = CLAMP(atoi(cmd->value), 0, 100);
		show(light);
		tapui_send_int(id, bright);
	} else if (cmd->op == TAPUI_OP_PRESS && strcmp(id, "blink") == 0) {
		blinks_left = 6;
		k_work_schedule(&blink_work, K_NO_WAIT);
	} else {
		char msg[64];

		snprintf(msg, sizeof(msg), "unknown control: %s", id);
		tapui_send_err(msg);
	}
}

static const struct tapui_handlers handlers = {
	.on_hello = on_hello,
	.on_cmd = on_cmd,
};

int main(void)
{
	int err = tapui_start(&handlers);

	if (err) {
		LOG_ERR("TapUI failed to start: %d", err);
		return 0;
	}
	show(light);

	for (;;) {
		k_sleep(K_SECONDS(2));
		if (tapui_connected()) {
			send_temp();
			tapui_send_int("uptime", (int)(k_uptime_get() / 1000));
		}
	}
	return 0;
}
