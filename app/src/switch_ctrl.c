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
#include "board_pins.h"
#include "modulator.h"
#include "qspi_pattern.h"
#include "rf_switch.h"

LOG_MODULE_REGISTER(switch_ctrl, LOG_LEVEL_INF);

/*
 * Mod_Sel0/1 are the same pins QSPI uses for IO2/IO3. With
 * MOD_CODE_SHIFT = 2, code bit 0 lands on IO2 and bit 1 on IO3, so static
 * mode drives the SP4T exactly the way the modulator's bursts do.
 */
#define SW_PIN_SEL0 PIN_QSPI_IO2
#define SW_PIN_SEL1 PIN_QSPI_IO3

BUILD_ASSERT((SW_PIN_SEL0 >> 5) == (SW_PIN_SEL1 >> 5),
	     "Mod_Sel0/1 must share a GPIO port so both change in one write");

#define SW_PORT ((SW_PIN_SEL0 >> 5) ? NRF_P1 : NRF_P0)
#define SW_SEL0_BIT (SW_PIN_SEL0 & 0x1FU)
#define SW_SEL1_BIT (SW_PIN_SEL1 & 0x1FU)
#define SW_MASK ((1UL << SW_SEL0_BIT) | (1UL << SW_SEL1_BIT))

enum sw_mode {
	SW_MODE_STATIC,
	SW_MODE_PATTERN,
};

// The shell runs in its own thread, so every mode change goes through this
static K_MUTEX_DEFINE(sw_lock);

static enum sw_mode mode = SW_MODE_STATIC;
static uint8_t held_code = MOD_CODE_IDLE;
static uint32_t spdt_level = SPDT_HARVEST_LEVEL;

// Switch codes for the current pattern, ready for qspi_pattern_burst()
static uint8_t burst_buf[QSPI_PATTERN_MAX_BURST];
static size_t burst_len;

/*
 * Scope test bitstream, sent LSB first: 8 ones, 8 zeros, then 1010... for
 * 16 bits. Mod_Sel1:Mod_Sel0 should step 00 -> 01 -> 11 -> 10 with each
 * state lasting 3 or 4 SCK clocks: a full rotation every ~377 ns (2.65 MHz)
 * for the first 8 us and every ~465 ns (2.15 MHz) for the next 8 us, then a
 * mix of the two.
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
	case MOD_CODE_PHASE_0:
		return "0 deg";
	case MOD_CODE_PHASE_90:
		return "90 deg";
	case MOD_CODE_PHASE_180:
		return "180 deg";
	case MOD_CODE_PHASE_270:
		return "270 deg";
	default:
		return "?";
	}
}

static const char *spdt_name(uint32_t level)
{
	return (level == SPDT_HARVEST_LEVEL) ? "harvest" : "SP4T";
}

/*
 * Both lines change in one write to OUT, so the SP4T never sees a
 * half-updated code between the old and new load.
 */
static void gpio_write_code(uint8_t code)
{
	uint32_t bits = ((code & 0x1U) ? (1UL << SW_SEL0_BIT) : 0U) |
			((code & 0x2U) ? (1UL << SW_SEL1_BIT) : 0U);

	SW_PORT->OUT = (SW_PORT->OUT & ~SW_MASK) | bits;
}

static void gpio_take_pins(uint8_t code)
{
	// Set the level before enabling the outputs so they come up correct
	gpio_write_code(code);
	nrf_gpio_cfg_output(SW_PIN_SEL0);
	nrf_gpio_cfg_output(SW_PIN_SEL1);
}

// Call with sw_lock held. Gives Mod_Sel0/1 back to GPIO on the held load
static void pattern_stop_locked(void)
{
	if (mode == SW_MODE_PATTERN) {
		// nrfx_qspi_uninit() disconnects the pins; GPIO takes them back
		qspi_pattern_stop();
		gpio_take_pins(held_code);
		mode = SW_MODE_STATIC;
	}
}

static void harvest_locked(void)
{
	rf_switch_harvest();
	spdt_level = SPDT_HARVEST_LEVEL;
}

int switch_ctrl_init(void)
{
	int err;

	k_mutex_lock(&sw_lock, K_FOREVER);

	err = rf_switch_init();
	spdt_level = SPDT_HARVEST_LEVEL;
	gpio_take_pins(MOD_CODE_IDLE);
	held_code = MOD_CODE_IDLE;
	mode = SW_MODE_STATIC;

	k_mutex_unlock(&sw_lock);

	return err;
}

void switch_ctrl_harvest(void)
{
	k_mutex_lock(&sw_lock, K_FOREVER);
	pattern_stop_locked();
	harvest_locked();
	k_mutex_unlock(&sw_lock);
}

int switch_ctrl_hold(uint8_t code)
{
	if (code > 3U) {
		return -EINVAL;
	}

	k_mutex_lock(&sw_lock, K_FOREVER);

	pattern_stop_locked();

	// Pick the load first, then route the antenna to it
	gpio_write_code(code);
	held_code = code;
	rf_switch_modulate();
	spdt_level = !SPDT_HARVEST_LEVEL;

	k_mutex_unlock(&sw_lock);

	return 0;
}

int switch_ctrl_spdt_level(uint32_t level)
{
	if (level > 1U) {
		return -EINVAL;
	}

	k_mutex_lock(&sw_lock, K_FOREVER);
	pattern_stop_locked();
	rf_switch_set_level(level);
	spdt_level = level;
	k_mutex_unlock(&sw_lock);

	return 0;
}

int switch_ctrl_pattern_start(const uint8_t *bits, size_t nbits)
{
	int len;
	int err = 0;

	k_mutex_lock(&sw_lock, K_FOREVER);

	// Bursts route the antenna themselves; between them it harvests
	harvest_locked();

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
		pattern_stop_locked();
	}

	k_mutex_unlock(&sw_lock);

	return (err == 0) ? len : err;
}

int switch_ctrl_burst(void)
{
	int err = -EAGAIN;

	k_mutex_lock(&sw_lock, K_FOREVER);

	if (mode == SW_MODE_PATTERN) {
		rf_switch_modulate();
		err = qspi_pattern_burst(burst_buf, burst_len);
		rf_switch_harvest();
	}

	k_mutex_unlock(&sw_lock);

	return err;
}

/* ---- shell -------------------------------------------------------------- */

static int cmd_harvest(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	switch_ctrl_harvest();
	shell_print(sh, "SPDT on harvest");

	return 0;
}

static int cmd_phase(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t code;

	ARG_UNUSED(argc);

	if (strcmp(argv[1], "0") == 0) {
		code = MOD_CODE_PHASE_0;
	} else if (strcmp(argv[1], "90") == 0) {
		code = MOD_CODE_PHASE_90;
	} else if (strcmp(argv[1], "180") == 0) {
		code = MOD_CODE_PHASE_180;
	} else if (strcmp(argv[1], "270") == 0) {
		code = MOD_CODE_PHASE_270;
	} else {
		shell_error(sh, "phase must be 0, 90, 180 or 270");
		return -EINVAL;
	}

	switch_ctrl_hold(code);
	shell_print(sh, "SPDT on SP4T, holding %s (Mod_Sel1:Mod_Sel0 = %u%u)",
		    code_name(code), (code >> 1) & 1U, code & 1U);

	return 0;
}

static int cmd_code(const struct shell *sh, size_t argc, char **argv)
{
	char *end;
	unsigned long code;

	ARG_UNUSED(argc);

	code = strtoul(argv[1], &end, 0);
	if (*end != '\0' || code > 3U) {
		shell_error(sh, "code must be 0..3 (Mod_Sel1:Mod_Sel0)");
		return -EINVAL;
	}

	switch_ctrl_hold((uint8_t)code);
	shell_print(sh, "SPDT on SP4T, holding code %lu%lu (%s)",
		    (code >> 1) & 1U, code & 1U, code_name((uint8_t)code));

	return 0;
}

static int cmd_spdt(const struct shell *sh, size_t argc, char **argv)
{
	char *end;
	unsigned long level;

	ARG_UNUSED(argc);

	level = strtoul(argv[1], &end, 0);
	if (*end != '\0' || level > 1U) {
		shell_error(sh, "level must be 0 or 1");
		return -EINVAL;
	}

	switch_ctrl_spdt_level((uint32_t)level);
	shell_print(sh, "CTRL_SPDT = %lu (firmware assumes this is %s)", level,
		    spdt_name((uint32_t)level));

	return 0;
}

static int cmd_test(const struct shell *sh, size_t argc, char **argv)
{
	int len;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	len = switch_ctrl_pattern_start(test_bits, 8U * sizeof(test_bits));
	if (len < 0) {
		shell_error(sh, "failed to start (%d); back on harvest", len);
		return len;
	}

	shell_print(sh, "running %u bit test pattern (%d bytes); SPDT goes to "
		    "SP4T for each burst", 8U * sizeof(test_bits), len);

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
		shell_error(sh, "failed to start (%d); back on harvest", len);
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
		shell_print(sh, "pattern: %u byte burst, SPDT on SP4T only during "
			    "bursts", burst_len);
	} else {
		shell_print(sh, "static: SPDT on %s (CTRL_SPDT = %u), SP4T on %s "
			    "(Mod_Sel1:Mod_Sel0 = %u%u)", spdt_name(spdt_level),
			    spdt_level, code_name(held_code),
			    (held_code >> 1) & 1U, held_code & 1U);
	}

	k_mutex_unlock(&sw_lock);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sw_cmds,
	SHELL_CMD_ARG(harvest, NULL, "SPDT to the rectifier", cmd_harvest, 1, 0),
	SHELL_CMD_ARG(phase, NULL, "Hold a load: phase <0|90|180|270>",
		      cmd_phase, 2, 0),
	SHELL_CMD_ARG(code, NULL, "Hold a raw Mod_Sel1:Mod_Sel0 code: code <0-3>",
		      cmd_code, 2, 0),
	SHELL_CMD_ARG(spdt, NULL, "Drive CTRL_SPDT to a raw level: spdt <0|1>",
		      cmd_spdt, 2, 0),
	SHELL_CMD_ARG(test, NULL, "Run the modulated test bursts", cmd_test, 1, 0),
	SHELL_CMD_ARG(adv, NULL, "Send a heart-rate advertising packet: adv [bpm]",
		      cmd_adv, 1, 1),
	SHELL_CMD_ARG(status, NULL, "Show the current switch state", cmd_status, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sw, &sw_cmds, "RF switch control (SPDT + SP4T)", NULL);
