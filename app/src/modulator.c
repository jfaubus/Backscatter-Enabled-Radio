#include "modulator.h"

#include <errno.h>

// Indexed by rotation state: 0 -> 120 -> 240 degrees
static const uint8_t phase_codes[3] = {
	MOD_CODE_PHASE_0,
	MOD_CODE_PHASE_120,
	MOD_CODE_PHASE_240,
};

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
	unsigned int state = 0;
	unsigned int ticks_left;

	if (nbits == 0U || out_size < modulator_out_size(nbits)) {
		return -EINVAL;
	}

	total = total_ticks(nbits);

	while (n < MOD_LEAD_IN_TICKS) {
		put_nibble(out, n++, MOD_CODE_HARVEST);
	}

	// The first state takes its length from the first bit
	ticks_left = (bits[0] & 1U) ? MOD_TICKS_PER_STATE_ONE
				    : MOD_TICKS_PER_STATE_ZERO;

	for (size_t i = 0; i < nbits; i++) {
		unsigned int bit = (bits[i / 8U] >> (i % 8U)) & 1U;
		unsigned int state_len = bit ? MOD_TICKS_PER_STATE_ONE
					     : MOD_TICKS_PER_STATE_ZERO;

		for (unsigned int t = 0; t < MOD_TICKS_PER_BIT; t++) {
			put_nibble(out, n++, phase_codes[state]);

			if (--ticks_left == 0U) {
				state = (state + 1U) % 3U;
				ticks_left = state_len;
			}
		}
	}

	while (n < total) {
		put_nibble(out, n++, MOD_CODE_HARVEST);
	}

	return (int)(total / 2U);
}
