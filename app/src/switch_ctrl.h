/*
 * SP4T switch control for bench testing, driven from the Zephyr shell.
 *
 * Two modes:
 *
 *   static   The switch control lines are plain GPIO outputs holding one
 *            code: harvest or one of the three reflect loads. QSPI is shut
 *            down, because it does not hold its pins between bursts.
 *
 *   pattern  QSPI owns the pins and main() fires the modulated test burst
 *            every few ms (see switch_ctrl_burst()).
 *
 * Shell commands (type "sw" then Tab for help):
 *
 *   sw harvest            antenna to the rectifier
 *   sw phase <0|120|240>  hold one reflect load
 *   sw code <0-3>         hold a raw V2:V1 code (for checking the truth table)
 *   sw test               start the modulated test bursts
 *   sw status             print the current mode
 *
 * The switch starts in harvest at boot.
 */

#ifndef SWITCH_CTRL_H_
#define SWITCH_CTRL_H_

#include <stddef.h>
#include <stdint.h>

/** Drive the control lines as GPIO and park the switch in harvest. */
int switch_ctrl_init(void);

/**
 * Hold one switch code until told otherwise. Stops QSPI first if pattern
 * mode was running.
 *
 * @param code V2:V1, 0..3 (see the MOD_CODE_* values in modulator.h).
 *
 * @return 0 on success, -EINVAL for a code above 3.
 */
int switch_ctrl_hold(uint8_t code);

/** Hand the pins to QSPI and enter pattern mode. */
int switch_ctrl_pattern_start(void);

/**
 * Fire one QSPI burst if pattern mode is active.
 *
 * @return 0 if the burst was sent, -EAGAIN when in static mode (nothing to
 *         do), otherwise whatever qspi_pattern_burst() returned.
 */
int switch_ctrl_burst(const void *data, size_t len);

#endif /* SWITCH_CTRL_H_ */
