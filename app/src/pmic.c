#include "pmic.h"

#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/shell/shell.h>

// STATUS[0..2], from the zephyr,user node in the board overlay
static const struct gpio_dt_spec status_pins[] = {
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), pmic_status_gpios, 0),
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), pmic_status_gpios, 1),
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), pmic_status_gpios, 2),
};

int pmic_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(status_pins); i++) {
		int err = gpio_pin_configure_dt(&status_pins[i], GPIO_INPUT);

		if (err != 0) {
			return err;
		}
	}

	return 0;
}

static int cmd_pmic(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "STATUS[0] LDOs ready:   %d", gpio_pin_get_dt(&status_pins[0]));
	shell_print(sh, "STATUS[1] battery low:  %d", gpio_pin_get_dt(&status_pins[1]));
	shell_print(sh, "STATUS[2] MPP eval:     %d", gpio_pin_get_dt(&status_pins[2]));

	return 0;
}

SHELL_CMD_REGISTER(pmic, NULL, "Read the AEM30940 status pins", cmd_pmic);
