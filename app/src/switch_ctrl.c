#include "switch_ctrl.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <hal/nrf_gpio.h>

#include "modulator.h"
#include "qspi_pattern.h"

LOG_MODULE_REGISTER(switch_ctrl, LOG_LEVEL_INF);

/*
 * The same pins QSPI uses for IO2/IO3. With MOD_CODE_SHIFT = 2, code bit 0
 * (V1) lands on IO2 and bit 1 (V2) on IO3, so static mode drives the switch
 * exactly the way the modulator's bursts do.
 */
#define SW_PIN_V1 NRF_GPIO_PIN_MAP(1, 7) /* IO2, Arduino D6 */
#define SW_PIN_V2 NRF_GPIO_PIN_MAP(1, 8) /* IO3, Arduino D7 */

#define SW_PORT NRF_P1
#define SW_V1_BIT 7U
#define SW_V2_BIT 8U
#define SW_MASK ((1UL << SW_V1_BIT) | (1UL << SW_V2_BIT))

enum sw_mode {
	SW_MODE_STATIC,
	SW_MODE_PATTERN,
};

// The shell runs in its own thread, so every mode change goes through this
static K_MUTEX_DEFINE(sw_lock);

static enum sw_mode mode = SW_MODE_STATIC;
static uint8_t held_code = MOD_CODE_HARVEST;

static const char *code_name(uint8_t code)
{
	switch (code) {
	case MOD_CODE_HARVEST:
		return "harvest";
	case MOD_CODE_PHASE_0:
		return "phase 0";
	case MOD_CODE_PHASE_120:
		return "phase 120";
	case MOD_CODE_PHASE_240:
		return "phase 240";
	default:
		return "?";
	}
}

/*
 * Both lines change in one write to OUT, so the switch never sees a
 * half-updated code between the old and new state.
 */
static void gpio_write_code(uint8_t code)
{
	uint32_t bits = ((code & 0x1U) ? (1UL << SW_V1_BIT) : 0U) |
			((code & 0x2U) ? (1UL << SW_V2_BIT) : 0U);

	SW_PORT->OUT = (SW_PORT->OUT & ~SW_MASK) | bits;
}

static void gpio_take_pins(uint8_t code)
{
	// Set the level before enabling the outputs so they come up correct
	gpio_write_code(code);
	nrf_gpio_cfg_output(SW_PIN_V1);
	nrf_gpio_cfg_output(SW_PIN_V2);
}

int switch_ctrl_init(void)
{
	k_mutex_lock(&sw_lock, K_FOREVER);
	gpio_take_pins(MOD_CODE_HARVEST);
	mode = SW_MODE_STATIC;
	held_code = MOD_CODE_HARVEST;
	k_mutex_unlock(&sw_lock);

	return 0;
}

int switch_ctrl_hold(uint8_t code)
{
	if (code > 3U) {
		return -EINVAL;
	}

	k_mutex_lock(&sw_lock, K_FOREVER);

	if (mode == SW_MODE_PATTERN) {
		// nrfx_qspi_uninit() disconnects the pins; GPIO takes them back
		qspi_pattern_stop();
		gpio_take_pins(code);
	} else {
		gpio_write_code(code);
	}

	mode = SW_MODE_STATIC;
	held_code = code;

	k_mutex_unlock(&sw_lock);

	return 0;
}

int switch_ctrl_pattern_start(void)
{
	int err = 0;

	k_mutex_lock(&sw_lock, K_FOREVER);

	if (mode != SW_MODE_PATTERN) {
		err = qspi_pattern_init(NRF_QSPI_FREQ_DIV1);
		if (err == 0) {
			mode = SW_MODE_PATTERN;
		} else {
			// Never leave the switch on whatever code it was holding
			gpio_take_pins(MOD_CODE_HARVEST);
			held_code = MOD_CODE_HARVEST;
		}
	}

	k_mutex_unlock(&sw_lock);

	return err;
}

int switch_ctrl_burst(const void *data, size_t len)
{
	int err = -EAGAIN;

	k_mutex_lock(&sw_lock, K_FOREVER);

	if (mode == SW_MODE_PATTERN) {
		err = qspi_pattern_burst(data, len);
	}

	k_mutex_unlock(&sw_lock);

	return err;
}

/* ---- shell -------------------------------------------------------------- */

static int cmd_harvest(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	switch_ctrl_hold(MOD_CODE_HARVEST);
	shell_print(sh, "holding harvest");

	return 0;
}

static int cmd_phase(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t code;

	ARG_UNUSED(argc);

	if (strcmp(argv[1], "0") == 0) {
		code = MOD_CODE_PHASE_0;
	} else if (strcmp(argv[1], "120") == 0) {
		code = MOD_CODE_PHASE_120;
	} else if (strcmp(argv[1], "240") == 0) {
		code = MOD_CODE_PHASE_240;
	} else {
		shell_error(sh, "phase must be 0, 120 or 240");
		return -EINVAL;
	}

	switch_ctrl_hold(code);
	shell_print(sh, "holding %s (code %u%u)", code_name(code),
		    (code >> 1) & 1U, code & 1U);

	return 0;
}

static int cmd_code(const struct shell *sh, size_t argc, char **argv)
{
	char *end;
	unsigned long code;

	ARG_UNUSED(argc);

	code = strtoul(argv[1], &end, 0);
	if (*end != '\0' || code > 3U) {
		shell_error(sh, "code must be 0..3 (V2:V1)");
		return -EINVAL;
	}

	switch_ctrl_hold((uint8_t)code);
	shell_print(sh, "holding code %lu%lu (%s)", (code >> 1) & 1U, code & 1U,
		    code_name((uint8_t)code));

	return 0;
}

static int cmd_test(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	err = switch_ctrl_pattern_start();
	if (err != 0) {
		shell_error(sh, "QSPI failed to start (%d); back in harvest", err);
		return err;
	}

	shell_print(sh, "running modulated test bursts on P1.07/P1.08");

	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_mutex_lock(&sw_lock, K_FOREVER);

	if (mode == SW_MODE_PATTERN) {
		shell_print(sh, "pattern: modulated test bursts");
	} else {
		shell_print(sh, "static: %s (code %u%u)", code_name(held_code),
			    (held_code >> 1) & 1U, held_code & 1U);
	}

	k_mutex_unlock(&sw_lock);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sw_cmds,
	SHELL_CMD_ARG(harvest, NULL, "Antenna to the rectifier", cmd_harvest, 1, 0),
	SHELL_CMD_ARG(phase, NULL, "Hold a reflect load: phase <0|120|240>",
		      cmd_phase, 2, 0),
	SHELL_CMD_ARG(code, NULL, "Hold a raw V2:V1 code: code <0-3>",
		      cmd_code, 2, 0),
	SHELL_CMD_ARG(test, NULL, "Run the modulated test bursts", cmd_test, 1, 0),
	SHELL_CMD_ARG(status, NULL, "Show the current switch mode", cmd_status, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sw, &sw_cmds, "SP4T switch control", NULL);
