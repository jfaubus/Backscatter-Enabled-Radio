/*
 * Capstone framework: nRF52840 DK
 *
 *   1. QSPI driven directly by nrfx as a 4-bit-wide pattern generator at the
 *      fastest clock the chip has (SCK up to 32 MHz off HFCLK128M).
 *   2. A single TIMER compare event forked over PPI onto two GPIOTE tasks, so
 *      two pins transition on the same clock edge with the CPU uninvolved.
 *
 * The two share no peripheral, no pin and no PPI/GPIOTE resource -- only
 * HFCLK, which both request and which is reference counted. They can run
 * together. The switches below exist so each can be brought up in isolation,
 * which is worth doing before trusting them in combination.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Bring-up switches Flip to 1 to include a subsystem */
#define ENABLE_PPI  0
#define ENABLE_QSPI 1

#if ENABLE_PPI
#include "ppi_fork.h"
#endif

#if ENABLE_QSPI
#include "qspi_pattern.h"
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if ENABLE_PPI
/*
 * 4 MHz on P1.10/P1.11 is the ceiling for this path. 16 MHz timer base, CC0
 * floors at 2 ticks (ppi_fork.c), and two compare events make one output
 * period: 16e6 / 2 / 2 = 4e6.
 */
#define PPI_TOGGLE_HZ 4000000U
#endif

#if ENABLE_QSPI
/* 256 bytes = 512 payload nibbles = 512 SCK clocks = 16 us at 32 MHz. */
#define QSPI_BURST_BYTES 256U

/*
 * A burst is only ~17 us long. At 500 ms between bursts the duty cycle is
 * 0.003%, which is miserable to trigger on. 10 ms gives 100 bursts/second --
 * still a clear idle gap between frames, but the scope stays lively.
 */
#define BURST_PERIOD_MS 10U
#endif

/* Slow enough not to clutter the console while probing. */
#define HEARTBEAT_MS 5000U

int main(void)
{
	int err = 0;

	(void)err;

	LOG_INF("capstone: PPI=%s QSPI=%s",
		ENABLE_PPI ? "on" : "off",
		ENABLE_QSPI ? "on" : "off");

#if ENABLE_PPI
	err = ppi_fork_start(PPI_TOGGLE_HZ);
	if (err != 0) {
		LOG_ERR("PPI fork failed to start (%d)", err);
		return err;
	}
#endif

#if ENABLE_QSPI
	err = qspi_pattern_init(NRF_QSPI_FREQ_DIV1);
	if (err != 0) {
		LOG_ERR("QSPI pattern generator failed to start (%d)", err);
#if ENABLE_PPI
		LOG_WRN("PPI fork is still running on P1.10/P1.11");
#endif
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
#else
	/*
	 * Nothing left for the CPU to do. Anything already armed keeps running
	 * from hardware regardless of what this thread does.
	 */
	while (true) {
		k_msleep(HEARTBEAT_MS);
		LOG_INF("alive");
	}
#endif

	return 0;
}
