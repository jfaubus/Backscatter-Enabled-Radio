/*
 * Capstone framework: nRF52840 DK
 *
 * A single TIMER compare event forked over PPI onto two GPIOTE tasks, so two
 * pins transition on the same clock edge with the CPU uninvolved.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ppi_fork.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/*
 * 4 MHz on P1.10/P1.11 is the ceiling for this path. 16 MHz timer base, CC0
 * floors at 2 ticks (ppi_fork.c), and two compare events make one output
 * period: 16e6 / 2 / 2 = 4e6.
 */
#define PPI_TOGGLE_HZ 4000000U

/* Slow enough not to clutter the console while probing. */
#define HEARTBEAT_MS 5000U

int main(void)
{
	int err;

	err = ppi_fork_start(PPI_TOGGLE_HZ);
	if (err != 0) {
		LOG_ERR("PPI fork failed to start (%d)", err);
		return err;
	}

	/*
	 * Nothing left for the CPU to do. Anything already armed keeps running
	 * from hardware regardless of what this thread does.
	 */
	while (true) {
		k_msleep(HEARTBEAT_MS);
		LOG_INF("alive");
	}

	return 0;
}
