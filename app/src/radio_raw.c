#include "radio_raw.h"

#include <errno.h>
#include <string.h>

#include <nrfx.h>

#include "hfxo.h"

// Advertising access address, split the way the RADIO wants it
#define ADV_AA_PREFIX 0x8EU    // most significant byte
#define ADV_AA_BASE 0x89BED6U  // lower three bytes
#define ADV_AA_BASE_LEN 3U

#define ADV_CRC_POLY 0x00065BU
#define ADV_CRC_INIT 0x555555U

// The RADIO reads the packet over EasyDMA, so it has to live in RAM
static uint8_t tx_buf[255];

static bool initialised;
static bool control_mode;

// RF frequency as an offset from 2400 MHz
static int channel_to_freq(uint8_t channel)
{
	switch (channel) {
	case 37:
		return 2;
	case 38:
		return 26;
	case 39:
		return 80;
	default:
		return -EINVAL;
	}
}

int radio_raw_init(uint8_t channel, bool hw_crc)
{
	int freq = channel_to_freq(channel);
	int err;

	if (freq < 0) {
		return freq;
	}

	err = hfxo_request();
	if (err != 0) {
		return err;
	}

	NRF_RADIO->POWER = 1;
	NRF_RADIO->MODE = RADIO_MODE_MODE_Ble_1Mbit;
	NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_0dBm;
	NRF_RADIO->FREQUENCY = (uint32_t)freq;

	// Logical address 0 = 0x8E89BED6. BASE0 is left-aligned in the register
	NRF_RADIO->PREFIX0 = ADV_AA_PREFIX;
	NRF_RADIO->BASE0 = ADV_AA_BASE << 8;
	NRF_RADIO->TXADDRESS = 0;

	if (hw_crc) {
		/*
		 * Standard BLE layout: S0 = header byte 0, LENGTH = header byte 1,
		 * then the payload. The radio appends the CRC and whitens.
		 */
		NRF_RADIO->PCNF0 = (8UL << RADIO_PCNF0_LFLEN_Pos) |
				   (1UL << RADIO_PCNF0_S0LEN_Pos) |
				   (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos);
		NRF_RADIO->PCNF1 = (255UL << RADIO_PCNF1_MAXLEN_Pos) |
				   (ADV_AA_BASE_LEN << RADIO_PCNF1_BALEN_Pos) |
				   (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) |
				   (1UL << RADIO_PCNF1_WHITEEN_Pos);
		NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos) |
				    (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
		NRF_RADIO->CRCPOLY = ADV_CRC_POLY;
		NRF_RADIO->CRCINIT = ADV_CRC_INIT;
		NRF_RADIO->DATAWHITEIV = channel;
	} else {
		/*
		 * No S0, no LENGTH field, no CRC, no whitening: every byte is
		 * static payload and goes out exactly as given. STATLEN is set
		 * per packet in radio_raw_send().
		 */
		NRF_RADIO->PCNF0 = (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos);
		NRF_RADIO->CRCCNF = RADIO_CRCCNF_LEN_Disabled << RADIO_CRCCNF_LEN_Pos;
	}

	NRF_RADIO->PACKETPTR = (uint32_t)tx_buf;

	// Ramp up -> send -> disable, with no CPU involvement in between
	NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk |
			    RADIO_SHORTS_END_DISABLE_Msk;

	control_mode = hw_crc;
	initialised = true;

	return 0;
}

int radio_raw_send(const uint8_t *pdu, size_t len)
{
	if (!initialised) {
		return -EPERM;
	}

	if (len == 0U || len > sizeof(tx_buf)) {
		return -EINVAL;
	}

	memcpy(tx_buf, pdu, len);

	if (!control_mode) {
		NRF_RADIO->PCNF1 = ((uint32_t)len << RADIO_PCNF1_MAXLEN_Pos) |
				   ((uint32_t)len << RADIO_PCNF1_STATLEN_Pos) |
				   (ADV_AA_BASE_LEN << RADIO_PCNF1_BALEN_Pos) |
				   (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos);
	}

	NRF_RADIO->EVENTS_DISABLED = 0;
	NRF_RADIO->TASKS_TXEN = 1;

	while (NRF_RADIO->EVENTS_DISABLED == 0) {
	}

	return 0;
}
