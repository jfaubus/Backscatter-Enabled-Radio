/*
 * Capstone framework: nRF52840 DK
 *
 * QSPI driven directly by nrfx as a 4-bit-wide pattern generator at the
 * fastest clock the chip has (SCK up to 32 MHz off HFCLK128M), fed with
 * SP4T switch codes from the modulator.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "modulator.h"
#include "qspi_pattern.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/*
 * Test bitstream, sent LSB first: 8 ones, 8 zeros, then 1010... for 16 bits.
 * On the analyser IO2/IO3 should step 00 -> 01 -> 11 every 4 SCK clocks
 * (125 ns) for the first 8 us, every 5 clocks (156 ns) for the next 8 us,
 * then mix the two. Harvest (10) sits before and after.
 */
static const uint8_t test_bits[] = { 0xFF, 0x00, 0x55, 0x55 };

#define TEST_NBITS (8U * sizeof(test_bits))

/* A burst is ~33 us; 10 ms between bursts keeps the scope easy to trigger. */
#define BURST_PERIOD_MS 10U

static uint8_t mod_buf[QSPI_PATTERN_MAX_BURST];

int main(void)
{
	int len;
	int err;

	len = modulator_build(test_bits, TEST_NBITS, mod_buf, sizeof(mod_buf));
	if (len < 0) {
		LOG_ERR("modulator_build failed (%d)", len);
		return len;
	}

	err = qspi_pattern_init(NRF_QSPI_FREQ_DIV1);
	if (err != 0) {
		LOG_ERR("QSPI pattern generator failed to start (%d)", err);
		return err;
	}

	LOG_INF("firing %u bit test sequence (%d bytes) every %u ms",
		TEST_NBITS, len, BURST_PERIOD_MS);

	while (true) {
		err = qspi_pattern_burst(mod_buf, (size_t)len);
		if (err != 0) {
			LOG_ERR("burst failed (%d)", err);
			return err;
		}

		k_msleep(BURST_PERIOD_MS);
	}

	return 0;
}
