/*
 * Pin map for the RF switch path, per board.
 *
 * The QSPI block drives the SP4T (Mod_Sel0/1 on IO2/IO3) and a GPIO drives
 * the SPDT that chooses between the rectifier and the SP4T. The nRF52840
 * QSPI has independent PSEL registers for every signal, so any pins work,
 * but 32 MHz is only specified on the dedicated QSPI pins -- which is what
 * the wearable PCB uses.
 *
 * QSPI also needs CSN, IO0 and IO1 assigned even though nothing listens to
 * them: CSN and IO0 are driven during every burst, and IO1 is read during
 * activation (see qspi_idle_pulldown_apply()).
 */

#ifndef BOARD_PINS_H_
#define BOARD_PINS_H_

#include <hal/nrf_gpio.h>

#if defined(CONFIG_BOARD_NRF52840DK)

/*
 * nRF52840 DK: everything on the Arduino header so it can be probed.
 *
 *   Arduino   Port    Signal
 *   -------   ----    ------
 *   D2        P1.03   QSPI SCK
 *   D3        P1.04   QSPI CSN
 *   D4        P1.05   QSPI IO0
 *   D5        P1.06   QSPI IO1
 *   D6        P1.07   QSPI IO2 = Mod_Sel0
 *   D8        P1.10   QSPI IO3 = Mod_Sel1  (D7 / P1.08 is damaged on our DK)
 *   D9        P1.11   CTRL_SPDT
 */
#define PIN_QSPI_SCK NRF_GPIO_PIN_MAP(1, 3)
#define PIN_QSPI_CSN NRF_GPIO_PIN_MAP(1, 4)
#define PIN_QSPI_IO0 NRF_GPIO_PIN_MAP(1, 5)
#define PIN_QSPI_IO1 NRF_GPIO_PIN_MAP(1, 6)
#define PIN_QSPI_IO2 NRF_GPIO_PIN_MAP(1, 7)
#define PIN_QSPI_IO3 NRF_GPIO_PIN_MAP(1, 10)
#define PIN_CTRL_SPDT NRF_GPIO_PIN_MAP(1, 11)

#elif defined(CONFIG_BOARD_BEACON_WEARABLE)

/*
 * Wearable PCB: the dedicated QSPI pins. CSN, IO0 and IO1 are not routed
 * anywhere on the board (PROVISIONAL: confirm with the EEs) but still have
 * to be assigned.
 *
 *   Port    Signal
 *   ----    ------
 *   P0.19   QSPI SCK   (QSPI_CLK, test point only)
 *   P0.17   QSPI CSN   (unrouted)
 *   P0.20   QSPI IO0   (unrouted)
 *   P0.21   QSPI IO1   (unrouted)
 *   P0.22   QSPI IO2 = Mod_Sel0
 *   P0.23   QSPI IO3 = Mod_Sel1
 *   P0.24   CTRL_SPDT  (high drive)
 */
#define PIN_QSPI_SCK NRF_GPIO_PIN_MAP(0, 19)
#define PIN_QSPI_CSN NRF_GPIO_PIN_MAP(0, 17)
#define PIN_QSPI_IO0 NRF_GPIO_PIN_MAP(0, 20)
#define PIN_QSPI_IO1 NRF_GPIO_PIN_MAP(0, 21)
#define PIN_QSPI_IO2 NRF_GPIO_PIN_MAP(0, 22)
#define PIN_QSPI_IO3 NRF_GPIO_PIN_MAP(0, 23)
#define PIN_CTRL_SPDT NRF_GPIO_PIN_MAP(0, 24)

#else
#error "No RF switch pin map for this board; add one to board_pins.h"
#endif

/*
 * CTRL_SPDT level that connects the antenna to the rectifier.
 *
 * PROVISIONAL: confirm with the EEs. Low is the safer guess either way,
 * since an unpowered or reset MCU leaves the pin low.
 */
#define SPDT_HARVEST_LEVEL 0U

#endif /* BOARD_PINS_H_ */
