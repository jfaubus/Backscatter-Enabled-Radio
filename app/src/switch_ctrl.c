#include "switch_ctrl.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <hal/nrf_gpio.h>

#include "ble_adv.h"
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

// Switch codes for the current pattern, ready for qspi_pattern_burst()
static uint8_t burst_buf[QSPI_PATTERN_MAX_BURST];
static size_t burst_len;

/*
 * Scope test bitstream, sent LSB first: 8 ones, 8 zeros, then 1010... for
 * 16 bits. IO2/IO3 should step 00 -> 01 -> 11 every 4 SCK clocks (125 ns)
 * for the first 8 us, every 5 clocks (156 ns) for the next 8 us, then mix
 * the two. Harvest (10) sits before and after.
 */
static const uint8_t test_bits[] = { 0xFF, 0x00, 0x55, 0x55 };

/*
 * Advertising packets go out for channel 38 (2426 MHz), so the carrier has
 * to sit at 2426 - 2.4 = 2423.6 MHz. Same random static address as the
 * radio test, C0:BE:AC:00:00:01, least significant byte first.
 */
#define ADV_CHANNEL BLE_ADV_CHANNEL_38
#define ADV_DEFAULT_BPM 72U

static const uint8_t adv_addr[BLE_ADDR_LEN] = {
	0x01, 0x00, 0x00, 0xAC, 0xBE, 0xC0
};

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

int switch_ctrl_pattern_start(const uint8_t *bits, size_t nbits)
{
	int len;
	int err = 0;

	k_mutex_lock(&sw_lock, K_FOREVER);

	len = modulator_build(bits, nbits, burst_buf, sizeof(burst_buf));
	if (len < 0) {
		err = len;
	} else if (mode != SW_MODE_PATTERN) {
		err = qspi_pattern_init(NRF_QSPI_FREQ_DIV1);
	}

	if (err == 0) {
		burst_len = (size_t)len;
		mode = SW_MODE_PATTERN;
	} else {
		// Never leave the switch on whatever it was doing before
		if (mode == SW_MODE_PATTERN) {
			qspi_pattern_stop();
		}
		gpio_take_pins(MOD_CODE_HARVEST);
		mode = SW_MODE_STATIC;
		held_code = MOD_CODE_HARVEST;
	}

	k_mutex_unlock(&sw_lock);

	return (err == 0) ? len : err;
}

int switch_ctrl_burst(void)
{
	int err = -EAGAIN;

	k_mutex_lock(&sw_lock, K_FOREVER);

	if (mode == SW_MODE_PATTERN) {
		err = qspi_pattern_burst(burst_buf, burst_len);
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
	int len;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	len = switch_ctrl_pattern_start(test_bits, 8U * sizeof(test_bits));
	if (len < 0) {
		shell_error(sh, "failed to start (%d); back in harvest", len);
		return len;
	}

	shell_print(sh, "running %u bit test pattern (%d bytes) on P1.07/P1.08",
		    8U * sizeof(test_bits), len);

	return 0;
}

static int cmd_adv(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t ad[BLE_ADV_DATA_MAX];
	uint8_t pkt[BLE_ADV_PACKET_MAX];
	unsigned long bpm = ADV_DEFAULT_BPM;
	char *end;
	int ad_len;
	int pkt_len;
	int len;

	if (argc > 1) {
		bpm = strtoul(argv[1], &end, 0);
		if (*end != '\0' || bpm > 255U) {
			shell_error(sh, "bpm must be 0..255");
			return -EINVAL;
		}
	}

	ad_len = ble_adv_hr_data((uint8_t)bpm, ad, sizeof(ad));
	if (ad_len < 0) {
		shell_error(sh, "ble_adv_hr_data failed (%d)", ad_len);
		return ad_len;
	}

	pkt_len = ble_adv_build(adv_addr, ad, (size_t)ad_len, ADV_CHANNEL,
				pkt, sizeof(pkt));
	if (pkt_len < 0) {
		shell_error(sh, "ble_adv_build failed (%d)", pkt_len);
		return pkt_len;
	}

	len = switch_ctrl_pattern_start(pkt, 8U * (size_t)pkt_len);
	if (len < 0) {
		shell_error(sh, "failed to start (%d); back in harvest", len);
		return len;
	}

	shell_print(sh, "sending advertising packet: %lu bpm, channel %u, "
		    "%d bytes = %d bits (~%d us on air), %d byte burst",
		    bpm, ADV_CHANNEL, pkt_len, 8 * pkt_len, 8 * pkt_len, len);
	shell_print(sh, "carrier must be at 2423.6 MHz for the receiver on "
		    "2426 MHz");
	shell_hexdump(sh, pkt, (size_t)pkt_len);

	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_mutex_lock(&sw_lock, K_FOREVER);

	if (mode == SW_MODE_PATTERN) {
		shell_print(sh, "pattern: %u byte burst", burst_len);
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
	SHELL_CMD_ARG(adv, NULL, "Send a heart-rate advertising packet: adv [bpm]",
		      cmd_adv, 1, 1),
	SHELL_CMD_ARG(status, NULL, "Show the current switch mode", cmd_status, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sw, &sw_cmds, "SP4T switch control", NULL);
