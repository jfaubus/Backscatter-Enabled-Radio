/*
 * RF switch control for bench testing, driven from the Zephyr shell.
 *
 * Two switches sit between the antenna and the rest of the tag:
 *
 *   SPDT  (CTRL_SPDT)          antenna -> rectifier (harvest) or -> SP4T
 *   SP4T  (Mod_Sel1:Mod_Sel0)  which of the 0/90/180/270 degree loads
 *
 * Two modes:
 *
 *   static   Mod_Sel0/1 are plain GPIO outputs holding one load, and the
 *            SPDT is held wherever the last command put it. QSPI is shut
 *            down, because it does not hold its pins between bursts.
 *
 *   pattern  QSPI owns Mod_Sel0/1 and main() fires the loaded burst every
 *            few ms (see switch_ctrl_burst()): either the scope test pattern
 *            or a real advertising packet. The SPDT goes to the SP4T for
 *            each burst and back to harvest in between, as it will in the
 *            real firmware.
 *
 * Shell commands (type "sw" then Tab for help):
 *
 *   sw harvest                SPDT to the rectifier
 *   sw phase <0|90|180|270>   SPDT to the SP4T and hold one load
 *   sw code <0-3>             same, with a raw Mod_Sel1:Mod_Sel0 code
 *   sw spdt <0|1>             drive CTRL_SPDT to a raw level (polarity check)
 *   sw test                   start the modulated test bursts
 *   sw adv [bpm]              start sending a heart-rate advertising packet
 *   sw status                 print the current state
 *
 * The tag starts in harvest at boot.
 */

#ifndef SWITCH_CTRL_H_
#define SWITCH_CTRL_H_

#include <stddef.h>
#include <stdint.h>

/** Take Mod_Sel0/1 as GPIO on the 0 degree load and put the SPDT on harvest. */
int switch_ctrl_init(void);

/** Stop any pattern and put the SPDT on harvest. */
void switch_ctrl_harvest(void);

/**
 * Stop any pattern, hold one SP4T load and route the antenna to it.
 *
 * @param code Mod_Sel1:Mod_Sel0, 0..3 (see MOD_CODE_PHASE_* in modulator.h).
 *
 * @return 0 on success, -EINVAL for a code above 3.
 */
int switch_ctrl_hold(uint8_t code);

/**
 * Stop any pattern and drive CTRL_SPDT to a raw level.
 *
 * @return 0 on success, -EINVAL for a level other than 0 or 1.
 */
int switch_ctrl_spdt_level(uint32_t level);

/**
 * Modulate a bitstream into the burst buffer, hand Mod_Sel0/1 to QSPI and
 * enter pattern mode. Replaces whatever burst was loaded before.
 *
 * @param bits  Packed bitstream, LSB first (see modulator_build()).
 * @param nbits Number of bits.
 *
 * @return Burst length in bytes, or a negative errno from modulator_build()
 *         or qspi_pattern_init(). On failure the tag is left in harvest.
 */
int switch_ctrl_pattern_start(const uint8_t *bits, size_t nbits);

/**
 * Fire the loaded burst once if pattern mode is active, with the SPDT on the
 * SP4T only for the duration of the burst.
 *
 * @return 0 if the burst was sent, -EAGAIN when in static mode (nothing to
 *         do), otherwise whatever qspi_pattern_burst() returned.
 */
int switch_ctrl_burst(void);

#endif /* SWITCH_CTRL_H_ */
