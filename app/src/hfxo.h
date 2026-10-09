/*
 * HFCLK source control.
 *
 * On reset the nRF52840 runs HFCLK from HFINT, an internal RC oscillator
 * accurate to only a few percent. Everything clocked from HFCLK inherits that
 * error -- TIMER's 16 MHz base, UARTE's baud rate.
 *
 * Nothing starts the 32 MHz crystal on its own here: Zephyr's system clock
 * runs off LFCLK/RTC, and no driver in this application sets
 * CONFIG_UART_NRFX_UARTE_HFXO_ON_ACTIVE. So any module whose *output
 * frequency* matters has to ask for the crystal itself.
 *
 * Requests are reference counted, so every such module should call this
 * rather than assume some other module already did.
 */

#ifndef HFXO_H_
#define HFXO_H_

/**
 * Start the 32 MHz crystal oscillator, or join an existing request, and block
 * until it is actually running (~300-400 us from cold).
 *
 * @return 0 on success, negative errno otherwise.
 */
int hfxo_request(void);

#endif /* HFXO_H_ */
