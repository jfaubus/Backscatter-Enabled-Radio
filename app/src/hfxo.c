#include "hfxo.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/clock_control.h>

LOG_MODULE_REGISTER(hfxo, LOG_LEVEL_INF);

int hfxo_request(void)
{
	/* Each clock is its own clock-control device in current Zephyr. The
	 * older CLOCK_CONTROL_NRF_SUBSYS_HF enumerator on the parent "clock"
	 * node is deprecated and not compiled in on this tree.
	 */
	const struct device *clk = DEVICE_DT_GET(DT_NODELABEL(hfclk));
	int err;

	if (!device_is_ready(clk)) {
		LOG_ERR("hfclk device not ready");
		return -ENODEV;
	}

	/* clock_control_on() resolves to common_api_blocking_start() for this
	 * driver, so it returns only once the crystal has actually started.
	 */
	err = clock_control_on(clk, NULL);
	if (err == -EALREADY || err == -EINPROGRESS) {
		return 0;
	}

	if (err != 0) {
		LOG_ERR("HFXO request failed (%d)", err);
		return err;
	}

	LOG_INF("HFXO running; HFCLK is now crystal-accurate");

	return 0;
}
