/*
 * Bitstream -> SP4T switch-code sequence for SSB backscatter.
 *
 * The tag cannot make RF; it can only pick which of three reflect loads
 * (0, 120, 240 degrees) the antenna sees. Stepping 0 -> 1 -> 2 -> 0 rotates
 * the phase of the reflected carrier, which shifts it up in frequency by
 * SCK / (3 * ticks_per_state). BLE's GFSK is then just "rotate a bit faster"
 * for a 1 and "a bit slower" for a 0:
 *
 *   bit 1: 4 ticks per state -> 32 MHz / 12 = 2.667 MHz
 *   bit 0: 5 ticks per state -> 32 MHz / 15 = 2.133 MHz
 *
 * i.e. a 2.400 MHz shift with +/-267 kHz deviation (BLE allows a modulation
 * index of 0.45-0.55, roughly +/-225..275 kHz). With the receiver on channel
 * 38 (2426 MHz) the carrier has to sit at 2423.6 MHz.
 *
 * One tick is one QSPI SCK clock, i.e. one nibble on IO0..IO3. These numbers
 * only hold with SCK at 32 MHz (NRF_QSPI_FREQ_DIV1).
 *
 * This file has no Zephyr dependencies so it can be compiled and tested on a
 * host as-is.
 */

#ifndef MODULATOR_H_
#define MODULATOR_H_

#include <stddef.h>
#include <stdint.h>

/** QSPI SCK clocks per 1 us BLE bit at 32 MHz. */
#define MOD_TICKS_PER_BIT 32U

/** Switch-state lengths in ticks. See the frequency table above. */
#define MOD_TICKS_PER_STATE_ONE 4U
#define MOD_TICKS_PER_STATE_ZERO 5U

/*
 * SP4T control codes (V2:V1). The 0->120->240->0 cycle has to flip both bits
 * on one transition (11 -> 00), and the switch may pass through 10 or 01 on
 * the way. Putting harvest on 10 means a glitch there briefly absorbs rather
 * than reflecting the wrong phase -- at least half the time.
 *
 * PROVISIONAL: confirm against the PE42440 truth table and the board's load
 * assignment.
 */
#define MOD_CODE_PHASE_0 0x0U
#define MOD_CODE_PHASE_120 0x1U
#define MOD_CODE_PHASE_240 0x3U
#define MOD_CODE_HARVEST 0x2U

/*
 * Bit position of V1 inside the nibble. Each nibble drives IO3..IO0 (bit 3 on
 * IO3), so a shift of 2 puts V1 on IO2 (P1.07) and V2 on IO3 (P1.08). IO0
 * carries the QSPI opcode and IO1 is an input during the single-line phases,
 * so neither is usable for the switch.
 *
 * PROVISIONAL: assumes high-nibble-first, which is still unconfirmed.
 */
#define MOD_CODE_SHIFT 2U

/** Harvest-state nibbles emitted before the first bit. */
#define MOD_LEAD_IN_TICKS 8U

/**
 * Minimum harvest-state nibbles emitted after the last bit. More are added as
 * needed, because nrfx_qspi_write() requires a length that is a multiple of
 * 4 bytes.
 */
#define MOD_LEAD_OUT_TICKS 8U

/**
 * Output buffer size in bytes that modulator_build() needs for @p nbits bits:
 * about 16 bytes per bit (a full 376-bit advertising packet is ~6 KB).
 */
size_t modulator_out_size(size_t nbits);

/**
 * Turn a bitstream into a QSPI-ready switch-code buffer.
 *
 * @p bits is read in BLE on-air order: bit 0 of bits[0] goes out first, then
 * bit 1 of bits[0], and so on (LSB first within each byte). Phase runs
 * continuously across bit boundaries -- a state still running when a bit ends
 * finishes at its old length, and the new bit's rate applies from the next
 * state.
 *
 * The output starts and ends in the harvest state and is padded to a
 * multiple of 4 bytes, so it can be handed straight to qspi_pattern_burst().
 *
 * @param bits     Packed bitstream.
 * @param nbits    Number of bits to send.
 * @param out      Output buffer, at least modulator_out_size(nbits) bytes.
 * @param out_size Size of @p out in bytes.
 *
 * @return Number of bytes written, or -EINVAL if @p nbits is 0 or @p out is
 *         too small.
 */
int modulator_build(const uint8_t *bits, size_t nbits,
		    uint8_t *out, size_t out_size);

#endif /* MODULATOR_H_ */
