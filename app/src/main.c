/*
 * Capstone framework: nRF52840 DK
 *
 * QSPI driven directly by nrfx as a 4-bit-wide pattern generator at the
 * fastest clock the chip has (SCK up to 32 MHz off HFCLK128M).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "qspi_pattern.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* 256 bytes = 512 payload nibbles = 512 SCK clocks = 16 us at 32 MHz. */
#define QSPI_BURST_BYTES 256U

/*
 * A burst is only ~17 us long. At 500 ms between bursts the duty cycle is
 * 0.003%, which is miserable to trigger on. 10 ms gives 100 bursts/second --
 * still a clear idle gap between frames, but the scope stays lively.
 */
#define BURST_PERIOD_MS 10U

int main(void)
{
	int err;

	err = qspi_pattern_init(NRF_QSPI_FREQ_DIV1);
	if (err != 0) {
		LOG_ERR("QSPI pattern generator failed to start (%d)", err);
		return err;
	}

	LOG_INF("firing %u-byte bursts every %u ms", QSPI_BURST_BYTES, BURST_PERIOD_MS);

	while (true) {
		err = qspi_pattern_square(QSPI_BURST_BYTES);
		if (err != 0) {
			LOG_ERR("burst failed (%d)", err);
			return err;
		}

		// sleeps to await the next burst
		k_msleep(BURST_PERIOD_MS);
	}

	return 0;
}
