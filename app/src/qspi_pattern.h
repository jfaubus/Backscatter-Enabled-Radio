/*
 * QSPI as a high-speed digital pattern generator on the nRF52840.
 *
 * The QSPI peripheral is clocked from HFCLK128M and can drive SCK at up to
 * 32 MHz -- faster than anything the CPU can reach by writing OUTSET/OUTCLR,
 * and faster than SPIM (8 MHz) or PWM. In quad ("4IO") mode it shifts four
 * bits per SCK clock out of IO0..IO3, so a RAM buffer becomes an arbitrary
 * 4-bit-wide waveform at up to 32 Mbit/s per line.
 *
 * There is no memory device on the bus: the pins are mapped onto the Arduino
 * header, not the DK's onboard MX25R6435F. See the pin table in
 * qspi_pattern.c.
 */

#ifndef QSPI_PATTERN_H_
#define QSPI_PATTERN_H_

#include <stddef.h>
#include <stdint.h>

#include <hal/nrf_qspi.h>

/** Largest burst a single qspi_pattern_burst() call can emit. */
#define QSPI_PATTERN_MAX_BURST 1024U

/**
 * Bring up QSPI as a pattern generator.
 *
 * @param sck_div SCK prescaler. SCK = 32 MHz / (sck_div + 1), so
 *                NRF_QSPI_FREQ_DIV1 is the 32 MHz maximum.
 *
 * @return 0 on success, negative errno otherwise. -ETIMEDOUT means the
 *         peripheral never reported READY after ACTIVATE; see the comment on
 *         the IO1 pull-down in qspi_pattern.c.
 */
int qspi_pattern_init(nrf_qspi_frequency_t sck_div);

/** Change SCK on the fly. Same return codes as qspi_pattern_init(). */
int qspi_pattern_set_freq(nrf_qspi_frequency_t sck_div);

/** Resulting SCK in Hz for a given prescaler, for logging and sanity checks. */
uint32_t qspi_pattern_sck_hz(nrf_qspi_frequency_t sck_div);

/**
 * Emit one CSN-framed burst of @p len bytes from @p data.
 *
 * The bytes are copied into an internal word-aligned RAM buffer first, so
 * @p data may live anywhere (including flash).
 *
 * @return 0 on success, -EINVAL for a bad length, -EPERM if not initialised,
 *         otherwise whatever nrfx_qspi_write() returned.
 */
int qspi_pattern_burst(const void *data, size_t len);

/** Emit @p len bytes all equal to @p byte. */
int qspi_pattern_fill_burst(uint8_t byte, size_t len);

/**
 * Emit the fastest square wave the peripheral can produce: every IO line
 * toggles on every SCK clock, i.e. SCK/2 on IO0..IO3.
 */
int qspi_pattern_square(size_t len);

/** Release the peripheral and return the pins to their reset state. */
void qspi_pattern_stop(void);

#endif /* QSPI_PATTERN_H_ */
