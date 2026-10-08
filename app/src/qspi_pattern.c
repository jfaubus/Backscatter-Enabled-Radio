#include "qspi_pattern.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <nrfx_qspi.h>
#include <hal/nrf_gpio.h>

#include "hfxo.h"

LOG_MODULE_REGISTER(qspi_pattern, LOG_LEVEL_INF);

/*
 * Pin map. These are NOT the DK's QSPI flash pins (P0.17/19/20-23). The
 * nRF52840 QSPI has independent PSEL registers for every signal, so the block
 * is moved onto six adjacent, otherwise-unused Arduino header pins:
 *
 *   Arduino   Port    QSPI signal
 *   -------   ----    -----------
 *   D2        P1.03   SCK
 *   D3        P1.04   CSN
 *   D4        P1.05   IO0
 *   D5        P1.06   IO1
 *   D6        P1.07   IO2
 *   D7        P1.08   IO3
 */
#define QSPI_PIN_SCK NRF_GPIO_PIN_MAP(1, 3)
#define QSPI_PIN_CSN NRF_GPIO_PIN_MAP(1, 4)
#define QSPI_PIN_IO0 NRF_GPIO_PIN_MAP(1, 5)
#define QSPI_PIN_IO1 NRF_GPIO_PIN_MAP(1, 6)
#define QSPI_PIN_IO2 NRF_GPIO_PIN_MAP(1, 7)
#define QSPI_PIN_IO3 NRF_GPIO_PIN_MAP(1, 8)

/* The peripheral polls the (nonexistent) memory during ACTIVATE. Fail fast
 * instead of sitting in nrfx's wait loop for half a second.
 */
#define QSPI_ACTIVATE_TIMEOUT_MS 20

/* QSPI DMA reads the source through the EasyDMA port: the buffer has to live
 * in Data RAM and be word-aligned.
 */
static uint8_t pattern_buf[QSPI_PATTERN_MAX_BURST] __aligned(4);

static bool initialised;

uint32_t qspi_pattern_sck_hz(nrf_qspi_frequency_t sck_div)
{
	/* SCK is derived from a fixed 32 MHz source: SCK = 32 MHz / (DIV + 1),
	 * where the enum value is the divider minus one.
	 */
	return 32000000U / ((uint32_t)sck_div + 1U);
}

static void qspi_config_fill(nrfx_qspi_config_t *cfg, nrf_qspi_frequency_t sck_div)
{
	*cfg = (nrfx_qspi_config_t)NRFX_QSPI_DEFAULT_CONFIG(
		QSPI_PIN_SCK, QSPI_PIN_CSN,
		QSPI_PIN_IO0, QSPI_PIN_IO1,
		QSPI_PIN_IO2, QSPI_PIN_IO3);

	/* PP4IO (opcode 0x38): the opcode goes out on IO0 alone, then address
	 * and payload are shifted four bits per clock across IO0..IO3. That is
	 * what makes this a 4-bit-wide pattern generator rather than a serial
	 * one.
	 */
	cfg->prot_if.writeoc = NRF_QSPI_WRITEOC_PP4IO;
	cfg->prot_if.readoc = NRF_QSPI_READOC_READ4IO;
	cfg->prot_if.addrmode = NRF_QSPI_ADDRMODE_24BIT;

	cfg->phy_if.sck_freq = sck_div;
	cfg->phy_if.spi_mode = NRF_QSPI_MODE_0;
	/* Minimum CSN-to-SCK dead time; nothing on the bus needs setup time. */
	cfg->phy_if.sck_delay = 0;

	cfg->timeout = QSPI_ACTIVATE_TIMEOUT_MS;
}

/*
 * nrfx configures every QSPI pin with NRF_GPIO_PIN_NOPULL. That is correct
 * with a real memory attached, but here nothing drives IO1. During ACTIVATE
 * the peripheral issues a status-register read and waits for the memory's
 * WIP bit to clear; a floating IO1 that settles high reads WIP=1 forever and
 * activation returns -ETIMEDOUT. Pulling IO1 down makes the status byte read
 * back 0x00, so activation completes.
 *
 * The pull is applied after nrfx_qspi_init() because init overwrites PIN_CNF.
 * (Strapping P1.06 to GND on the header has the same effect in hardware.)
 * 
 * Basically there's no external flash pulling this line down saying "hey
 * Im ready" so were configuring it so that it constantly reads as "ready"
 */
static void qspi_idle_pulldown_apply(void)
{
	nrf_gpio_cfg(QSPI_PIN_IO1,
		     NRF_GPIO_PIN_DIR_INPUT,
		     NRF_GPIO_PIN_INPUT_DISCONNECT,
		     NRF_GPIO_PIN_PULLDOWN,
		     NRF_GPIO_PIN_H0H1,
		     NRF_GPIO_PIN_NOSENSE);
}

int qspi_pattern_init(nrf_qspi_frequency_t sck_div)
{
	nrfx_qspi_config_t cfg;
	int err;

	if (initialised) {
		return qspi_pattern_set_freq(sck_div);
	}

	/* QSPI is clocked from HFCLK128M; without the crystal its SCK would
	 * inherit the RC oscillator's few-percent error.
	 */
	err = hfxo_request();
	if (err != 0) {
		return err;
	}

	qspi_config_fill(&cfg, sck_div);

	// writes settings into the QSPI hardware registers and sets the pins up  w/ high drive no pull
	err = nrfx_qspi_init(&cfg, NULL /* blocking mode */, NULL);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("nrfx_qspi_init failed (%d)", err);
		return err;
	}

	qspi_idle_pulldown_apply();

	// sends "read status register 0x05" adnd listens on I01 (configured to mock flash chip response)
	// with I01 forver pulled down it should always read "ready" and activation succeeds
	err = nrfx_qspi_activate(true);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("nrfx_qspi_activate failed (%d)", err);
		if (err == -ETIMEDOUT) {
			LOG_ERR("  ACTIVATE polls a status register that nothing "
				"answers. Try strapping IO1 (P1.06) to GND.");
		}
		nrfx_qspi_uninit();
		return err;
	}

	initialised = true;

	LOG_INF("QSPI pattern generator up: SCK %u Hz on P1.03, "
		"IO0..IO3 on P1.05..P1.08, CSN on P1.04",
		qspi_pattern_sck_hz(sck_div));

	return 0;
}

int qspi_pattern_set_freq(nrf_qspi_frequency_t sck_div)
{
	nrfx_qspi_config_t cfg;
	int err;

	if (!initialised) {
		return -EPERM;
	}

	qspi_config_fill(&cfg, sck_div);

	/* reconfigure() deactivates the peripheral, so the pull-down has to be
	 * reapplied before the next activation for the same reason as at init.
	 */
	err = nrfx_qspi_reconfigure(&cfg);
	if (err != 0) {
		LOG_ERR("nrfx_qspi_reconfigure failed (%d)", err);
		return err;
	}

	qspi_idle_pulldown_apply();

	err = nrfx_qspi_activate(true);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("reactivation failed (%d)", err);
		return err;
	}

	LOG_INF("SCK now %u Hz", qspi_pattern_sck_hz(sck_div));

	return 0;
}

int qspi_pattern_burst(const void *data, size_t len)
{
	if (!initialised) {
		return -EPERM;
	}

	if (len == 0U || len > sizeof(pattern_buf)) {
		return -EINVAL;
	}

	memcpy(pattern_buf, data, len);

	/*
	 * One call produces two CSN frames on the wire, not one: the QSPI
	 * peripheral automatically prepends a WREN (0x06) before every page
	 * program. The pattern frame itself is
	 *
	 *   CSN low | 0x38 on IO0 (8 clocks) | 24-bit address (6 clocks, quad)
	 *           | len*2 payload nibbles  | CSN high
	 *
	 * so on a scope the payload starts 14 SCK clocks after CSN falls on the
	 * second frame. The destination address is meaningless here (nothing is
	 * listening) and is left at 0.
	 */
	return nrfx_qspi_write(pattern_buf, len, 0);
}

int qspi_pattern_fill_burst(uint8_t byte, size_t len)
{
	if (!initialised) {
		return -EPERM;
	}

	if (len == 0U || len > sizeof(pattern_buf)) {
		return -EINVAL;
	}

	// fills the ram buffer
	memset(pattern_buf, byte, len);

	// write these 256 bytes to flash address 0 and the CPU waits while the hardware the rest
	// by DMA (remember it is not actually writing to Flash)
	return nrfx_qspi_write(pattern_buf, len, 0);
}

int qspi_pattern_square(size_t len)
{
	/*
	 * In quad mode each byte is two nibbles, MSB nibble first, and each
	 * nibble drives IO3..IO0 for one SCK clock. 0xF0 therefore drives all
	 * four lines high for one clock and low for the next: a square wave at
	 * SCK/2 on every IO line simultaneously, 16 MHz with SCK at 32 MHz.
	 */
	return qspi_pattern_fill_burst(0xF0, len);
}

void qspi_pattern_stop(void)
{
	if (!initialised) {
		return;
	}

	nrfx_qspi_uninit();
	initialised = false;
}
