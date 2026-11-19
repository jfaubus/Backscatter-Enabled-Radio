/*
 * Pin map for the RF switch path.
 *
 * The QSPI block drives the SP4T (Mod_Sel0/1 on IO2/IO3) and a GPIO drives
 * the SPDT that chooses between the rectifier and the SP4T. The pins come
 * from the zephyr,user node in the board overlay
 * (boards/nrf52840dk_nrf52840.overlay); see the pin table there.
 */

#ifndef BOARD_PINS_H_
#define BOARD_PINS_H_

#include <zephyr/devicetree.h>
#include <soc_nrf_common.h>
#include <hal/nrf_gpio.h>

#define BOARD_PINS_NODE DT_PATH(zephyr_user)

#define PIN_QSPI_SCK NRF_DT_GPIOS_TO_PSEL(BOARD_PINS_NODE, qspi_sck_gpios)
#define PIN_QSPI_CSN NRF_DT_GPIOS_TO_PSEL(BOARD_PINS_NODE, qspi_csn_gpios)
#define PIN_QSPI_IO0 NRF_DT_GPIOS_TO_PSEL(BOARD_PINS_NODE, qspi_io0_gpios)
#define PIN_QSPI_IO1 NRF_DT_GPIOS_TO_PSEL(BOARD_PINS_NODE, qspi_io1_gpios)
#define PIN_QSPI_IO2 NRF_DT_GPIOS_TO_PSEL(BOARD_PINS_NODE, qspi_io2_gpios)
#define PIN_QSPI_IO3 NRF_DT_GPIOS_TO_PSEL(BOARD_PINS_NODE, qspi_io3_gpios)
#define PIN_CTRL_SPDT NRF_DT_GPIOS_TO_PSEL(BOARD_PINS_NODE, ctrl_spdt_gpios)

/*
 * CTRL_SPDT level that connects the antenna to the rectifier.
 *
 * PROVISIONAL: confirm with the EEs. Low is the safer guess either way,
 * since an unpowered or reset MCU leaves the pin low.
 */
#define SPDT_HARVEST_LEVEL 0U

#endif /* BOARD_PINS_H_ */
