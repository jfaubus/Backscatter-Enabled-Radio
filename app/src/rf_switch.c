#include "rf_switch.h"

#include <zephyr/kernel.h>

#include <hal/nrf_gpio.h>

#include "board_pins.h"

/*
 * Time for the SPDT to settle after CTRL_SPDT changes, before the first
 * reflected symbol matters.
 *
 * PROVISIONAL: take this from the SPDT datasheet's switching time.
 */
#define SPDT_SETTLE_US 5U

static void spdt_write(uint32_t level)
{
	if (level) {
		nrf_gpio_pin_set(PIN_CTRL_SPDT);
	} else {
		nrf_gpio_pin_clear(PIN_CTRL_SPDT);
	}
}

int rf_switch_init(void)
{
	// Set the level before enabling the output so it comes up in harvest
	spdt_write(SPDT_HARVEST_LEVEL);

	// High drive: the SPDT's control input sources its current from this pin
	nrf_gpio_cfg(PIN_CTRL_SPDT,
		     NRF_GPIO_PIN_DIR_OUTPUT,
		     NRF_GPIO_PIN_INPUT_DISCONNECT,
		     NRF_GPIO_PIN_NOPULL,
		     NRF_GPIO_PIN_H0H1,
		     NRF_GPIO_PIN_NOSENSE);

	return 0;
}

void rf_switch_harvest(void)
{
	spdt_write(SPDT_HARVEST_LEVEL);
}

void rf_switch_modulate(void)
{
	spdt_write(!SPDT_HARVEST_LEVEL);
	k_busy_wait(SPDT_SETTLE_US);
}
