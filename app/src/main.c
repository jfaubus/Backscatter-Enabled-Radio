/*
 * Capstone framework: nRF52840 DK
 *
 * QSPI driven directly by nrfx as a 4-bit-wide pattern generator at the
 * fastest clock the chip has (SCK up to 32 MHz off HFCLK128M), fed with
 * SP4T switch codes from the modulator.
 *
 * Boots with the switch held in harvest. Use the shell ("sw" + Tab) to hold
 * a reflect load, run the scope test pattern, or send a real advertising
 * packet; see switch_ctrl.h.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "modulator.h"
#include "switch_ctrl.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* 10 ms between bursts keeps the scope easy to trigger, and is an ordinary
 * advertising interval for a phone to pick up.
 */
#define BURST_PERIOD_MS 10U

int main(void)
{
	int err;

	err = switch_ctrl_init();
	if (err != 0) {
		LOG_ERR("switch_ctrl_init failed (%d)", err);
		return err;
	}

	LOG_INF("switch in harvest; \"sw test\" or \"sw adv\" starts bursts "
		"every %u ms", BURST_PERIOD_MS);

	while (true) {
		// -EAGAIN just means a static state is being held
		err = switch_ctrl_burst();
		if (err != 0 && err != -EAGAIN) {
			LOG_ERR("burst failed (%d); back to harvest", err);
			switch_ctrl_hold(MOD_CODE_HARVEST);
		}

		k_msleep(BURST_PERIOD_MS);
	}

	return 0;
}
