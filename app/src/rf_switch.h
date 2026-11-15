/*
 * SPDT control: antenna to the rectifier (harvest) or to the SP4T modulator.
 *
 * The antenna should only ever leave harvest for the duration of a burst:
 *
 *   rf_switch_modulate();
 *   qspi_pattern_burst(...);
 *   rf_switch_harvest();
 *
 * A tag left on the modulator stops charging and never recovers.
 */

#ifndef RF_SWITCH_H_
#define RF_SWITCH_H_

#include <stdint.h>

/** Configure CTRL_SPDT as a high-drive output and select harvest. */
int rf_switch_init(void);

/** Antenna to the rectifier. */
void rf_switch_harvest(void);

/** Antenna to the SP4T, then wait for the SPDT to settle. */
void rf_switch_modulate(void);

/**
 * Drive CTRL_SPDT to a raw level, regardless of which one means harvest.
 * Bench use only: for working out the SPDT's polarity.
 */
void rf_switch_set_level(uint32_t level);

#endif /* RF_SWITCH_H_ */
