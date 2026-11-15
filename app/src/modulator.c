#include "modulator.h"

#include <errno.h>

// Indexed by rotation state: 0 -> 90 -> 180 -> 270 degrees
static const uint8_t phase_codes[4] = {
	MOD_CODE_PHASE_0,
	MOD_CODE_PHASE_90,
	MOD_CODE_PHASE_180,
	MOD_CODE_PHASE_270,
};

/*
 * The phase accumulator counts one full rotation as 2^32, so it wraps by
 * itself. Each tick advances it by f / SCK of a turn.
 */
#define PHASE_INC(f_hz) ((uint32_t)(((uint64_t)(f_hz) << 32) / MOD_SCK_HZ))

#define PHASE_INC_ONE PHASE_INC(MOD_SHIFT_HZ + MOD_DEVIATION_HZ)
#define PHASE_INC_ZERO PHASE_INC(MOD_SHIFT_HZ - MOD_DEVIATION_HZ)

// Each of the four states covers 2^30; adding half of that rounds to nearest
#define PHASE_HALF_STATE (1UL << 29)

// Nibbles are packed two per byte, high nibble first (it goes out first)
static void put_nibble(uint8_t *out, size_t n, uint8_t code)
{
	uint8_t nib = (uint8_t)(code << MOD_CODE_SHIFT) & 0x0FU;

	if ((n & 1U) == 0U) {
		out[n / 2U] = (uint8_t)(nib << 4);
	} else {
		out[n / 2U] |= nib;
	}
}

static size_t total_ticks(size_t nbits)
{
	size_t ticks = MOD_LEAD_IN_TICKS + nbits * MOD_TICKS_PER_BIT +
		       MOD_LEAD_OUT_TICKS;

	// 8 nibbles = 4 bytes
	return (ticks + 7U) & ~(size_t)7U;
}

size_t modulator_out_size(size_t nbits)
{
	return total_ticks(nbits) / 2U;
}

int modulator_build(const uint8_t *bits, size_t nbits,
		    uint8_t *out, size_t out_size)
{
	size_t total;
	size_t n = 0;
	uint32_t phase = 0;

	if (nbits == 0U || out_size < modulator_out_size(nbits)) {
		return -EINVAL;
	}

	total = total_ticks(nbits);

	while (n < MOD_LEAD_IN_TICKS) {
		put_nibble(out, n++, MOD_CODE_IDLE);
	}

	for (size_t i = 0; i < nbits; i++) {
		unsigned int bit = (bits[i / 8U] >> (i % 8U)) & 1U;
		uint32_t inc = bit ? PHASE_INC_ONE : PHASE_INC_ZERO;

		for (unsigned int t = 0; t < MOD_TICKS_PER_BIT; t++) {
			unsigned int state = ((phase + PHASE_HALF_STATE) >> 30) & 3U;

			put_nibble(out, n++, phase_codes[state]);
			phase += inc;
		}
	}

	while (n < total) {
		put_nibble(out, n++, MOD_CODE_IDLE);
	}

	return (int)(total / 2U);
}
