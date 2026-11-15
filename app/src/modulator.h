/*
 * Bitstream -> SP4T switch-code sequence for SSB backscatter.
 *
 * The tag cannot make RF; it can only pick which of four reflect loads
 * (0, 90, 180, 270 degrees) the antenna sees. Stepping 0 -> 90 -> 180 -> 270
 * -> 0 rotates the phase of the reflected carrier, which shifts it up in
 * frequency by however fast the rotation goes. BLE's GFSK is then just
 * "rotate a bit faster" for a 1 and "a bit slower" for a 0:
 *
 *   bit 1: 2.400 + 0.250 = 2.650 MHz
 *   bit 0: 2.400 - 0.250 = 2.150 MHz
 *
 * With the receiver on channel 38 (2426 MHz) the carrier has to sit at
 * 2423.6 MHz.
 *
 * One tick is one QSPI SCK clock, i.e. one nibble on IO0..IO3. With four
 * states, whole-tick state lengths can't hit those rates (3 and 4 ticks give
 * +/-333 kHz, outside BLE's +/-225..275 kHz), so a phase accumulator tracks
 * where the rotation should be and each tick picks the nearest load. States
 * last 3 or 4 ticks and average out to the exact frequency.
 *
 * This file has no Zephyr dependencies so it can be compiled and tested on a
 * host as-is.
 */

#ifndef MODULATOR_H_
#define MODULATOR_H_

#include <stddef.h>
#include <stdint.h>

/** QSPI SCK frequency the tables are built for (NRF_QSPI_FREQ_DIV1). */
#define MOD_SCK_HZ 32000000U

/** QSPI SCK clocks per 1 us BLE bit. */
#define MOD_TICKS_PER_BIT 32U

/** Frequency shift of the reflected signal and BLE's GFSK deviation. */
#define MOD_SHIFT_HZ 2400000U
#define MOD_DEVIATION_HZ 250000U

/*
 * SP4T control codes (Mod_Sel1:Mod_Sel0), in rotation order. Gray-code
 * order means every step -- including 270 -> 0 -- changes exactly one
 * control line, so the switch never passes through a wrong load mid-step.
 *
 * PROVISIONAL: the code-to-load mapping is set by how the PCB wires the
 * loads to the SP4T ports. Confirm with the EEs; ideally the board follows
 * this order.
 */
#define MOD_CODE_PHASE_0 0x0U
#define MOD_CODE_PHASE_90 0x1U
#define MOD_CODE_PHASE_180 0x3U
#define MOD_CODE_PHASE_270 0x2U

/*
 * Code held before the first bit and after the last. The rectifier/SP4T
 * choice is made by the SPDT, so this only needs to be a steady load.
 */
#define MOD_CODE_IDLE MOD_CODE_PHASE_0

/*
 * Bit position of Mod_Sel0 inside the nibble. Each nibble drives IO3..IO0
 * (bit 3 on IO3), so a shift of 2 puts Mod_Sel0 on IO2 and Mod_Sel1 on IO3.
 * IO0 carries the QSPI opcode and IO1 is an input during the single-line
 * phases, so neither is usable for the switch.
 *
 * PROVISIONAL: assumes high-nibble-first, which is still unconfirmed.
 */
#define MOD_CODE_SHIFT 2U

/** Idle-code nibbles emitted before the first bit. */
#define MOD_LEAD_IN_TICKS 8U

/**
 * Minimum idle-code nibbles emitted after the last bit. More are added as
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
 * continuously across bit boundaries; only the rotation rate changes.
 *
 * The output starts and ends on MOD_CODE_IDLE and is padded to a multiple of
 * 4 bytes, so it can be handed straight to qspi_pattern_burst().
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
