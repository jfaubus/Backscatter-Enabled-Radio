#include "ble_adv.h"

#include <errno.h>
#include <string.h>

// Fixed access address for all advertising packets
#define ADV_ACCESS_ADDRESS 0x8E89BED6U

// Alternating 0101... whose first bit matches the access address LSB (0)
#define ADV_PREAMBLE 0xAAU

// PDU header byte 0: PDU type ADV_NONCONN_IND (0b0010), TxAdd = 1 (random)
#define ADV_PDU_TYPE_NONCONN_IND 0x02U
#define ADV_PDU_TXADD_RANDOM 0x40U

// The CRC register starts at 0x555555 for every advertising packet
#define ADV_CRC_INIT 0x555555U

/*
 * CRC-24, polynomial x^24 + x^10 + x^9 + x^6 + x^4 + x^3 + x + 1 (0x00065B).
 *
 * Data is fed in air order (LSB of each byte first), so the register is kept
 * bit-reversed: bit 0 here is the spec's position 23. That makes the result
 * come out ready to send LSB first  which puts the spec's MSB on air first
 */
static uint32_t crc24(const uint8_t *data, size_t len)
{
	// 0x555555 is 0xAAAAAA reversed; written this way to match the spec
	uint32_t state = 0;

	for (unsigned int i = 0; i < 24U; i++) {
		if (ADV_CRC_INIT & (1U << i)) {
			state |= 1U << (23U - i);
		}
	}

	for (size_t i = 0; i < len; i++) {
		uint8_t byte = data[i];

		for (unsigned int j = 0; j < 8U; j++) {
			unsigned int fb = (state ^ byte) & 1U;

			byte >>= 1;
			state >>= 1;
			if (fb) {
				state ^= 0xDA6000U;
			}
		}
	}

	return state;
}

/*
 * Data whitening, polynomial x^7 + x^4 + 1. The spec seeds position 0 with 1
 * and positions 1..6 with the channel index, MSB at position 1. Here bit 6
 * is position 0 and bit 0 is position 6 (the output tap), so the seed is just
 * the channel index with bit 6 set.
 */
static void whiten(uint8_t *data, size_t len, uint8_t channel)
{
	uint8_t lfsr = (uint8_t)(channel | 0x40U);

	for (size_t i = 0; i < len; i++) {
		for (uint8_t m = 1U; m != 0U; m <<= 1) {
			if (lfsr & 0x01U) {
				lfsr ^= 0x88U;
				data[i] ^= m;
			}
			lfsr >>= 1;
		}
	}
}

int ble_adv_build(const uint8_t adv_addr[BLE_ADDR_LEN],
		  const uint8_t *adv_data, size_t adv_data_len,
		  uint8_t channel, uint8_t *out, size_t out_size)
{
	size_t pdu_len = 2U + BLE_ADDR_LEN + adv_data_len;
	size_t total = 1U + 4U + pdu_len + 3U;
	uint8_t *pdu = &out[5];
	uint32_t crc;
	size_t n = 0;

	if (channel < BLE_ADV_CHANNEL_37 || channel > BLE_ADV_CHANNEL_39 ||
	    adv_data_len > BLE_ADV_DATA_MAX ||
	    (adv_data_len > 0U && adv_data == NULL) || out_size < total) {
		return -EINVAL;
	}

	out[n++] = ADV_PREAMBLE;

	// Access address goes out least significant byte first
	for (unsigned int i = 0; i < 4U; i++) {
		out[n++] = (uint8_t)(ADV_ACCESS_ADDRESS >> (8U * i));
	}

	out[n++] = ADV_PDU_TYPE_NONCONN_IND | ADV_PDU_TXADD_RANDOM;
	out[n++] = (uint8_t)(BLE_ADDR_LEN + adv_data_len);

	memcpy(&out[n], adv_addr, BLE_ADDR_LEN);
	n += BLE_ADDR_LEN;

	if (adv_data_len > 0U) {
		memcpy(&out[n], adv_data, adv_data_len);
		n += adv_data_len;
	}

	crc = crc24(pdu, pdu_len);
	out[n++] = (uint8_t)crc;
	out[n++] = (uint8_t)(crc >> 8);
	out[n++] = (uint8_t)(crc >> 16);

	// Whitening covers the PDU and CRC, not the preamble or access address
	whiten(pdu, pdu_len + 3U, channel);

	return (int)n;
}

int ble_adv_hr_data(uint8_t bpm, uint8_t *out, size_t out_size)
{
	static const uint8_t name[] = { 'J', 'U', 'L', 'I', 'A', 'C', 'A', 'P', 'S', 'T', 'O', 'N', 'E' };
	size_t n = 0;

	if (out_size < 3U + (2U + sizeof(name)) + 6U) {
		return -EINVAL;
	}

	// Flags: LE General Discoverable, BR/EDR not supported
	out[n++] = 0x02;
	out[n++] = 0x01;
	out[n++] = 0x06;

	// Complete Local Name
	out[n++] = (uint8_t)(1U + sizeof(name));
	out[n++] = 0x09;
	memcpy(&out[n], name, sizeof(name));
	n += sizeof(name);

	// Service Data, 16-bit UUID 0x180D (Heart Rate) little-endian
	out[n++] = 0x05;
	out[n++] = 0x16;
	out[n++] = 0x0D;
	out[n++] = 0x18;
	out[n++] = 0x00; // Heart Rate Measurement flags: uint8 BPM follows
	out[n++] = bpm;

	return (int)n;
}
