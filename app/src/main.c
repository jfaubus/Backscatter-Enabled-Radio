/*
 * Advertising packet test: nRF52840 DK
 *
 * Sends ble_adv_build() output straight out of the RADIO peripheral so a
 * phone running nRF Connect can confirm the packet bytes are valid BLE --
 * before any backscatter hardware is involved. Look for a device named
 * julia_capstone at C0:BE:AC:00:00:01 whose 0x180D service data counts 60..120 BPM.
 *
 * Throwaway test firmware: this replaces the real main.c, so this branch is
 * not meant to be merged.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ble_adv.h"
#include "radio_raw.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/*
 * 0: raw mode, the real test. Our CRC and whitening go on air untouched.
 * 1: control mode. The radio does CRC and whitening itself on an
 *    un-whitened PDU. If this shows up on the phone but 0 does not, the bug
 *    is in ble_adv.c rather than in the radio setup.
 */
#define RADIO_TEST_HW_CRC 0

#define TEST_CHANNEL BLE_ADV_CHANNEL_38

// Short enough that a scanning phone picks it up within a second or two
#define ADV_INTERVAL_MS 100U

// BPM steps once a second, so the phone visibly updates
#define PACKETS_PER_BPM_STEP (1000U / ADV_INTERVAL_MS)
#define BPM_MIN 60U
#define BPM_MAX 120U

// Random static address C0:BE:AC:00:00:01, least significant byte first
static const uint8_t adv_addr[BLE_ADDR_LEN] = {
	0x01, 0x00, 0x00, 0xAC, 0xBE, 0xC0
};

// Byte 5 onward of ble_adv_build() output: everything after the preamble
// and access address, which the radio generates itself
#define PDU_OFFSET 5U

static int build_pdu(uint8_t bpm, uint8_t *out, size_t out_size)
{
	uint8_t ad[BLE_ADV_DATA_MAX];
	int ad_len = ble_adv_hr_data(bpm, ad, sizeof(ad));

	if (ad_len < 0) {
		return ad_len;
	}

	if (RADIO_TEST_HW_CRC) {
		/* Built by hand rather than with ble_adv_build() so the control
		 * shares nothing with the code under test except the AdvData.
		 * ADV_NONCONN_IND, TxAdd = random.
		 */
		size_t len = 2U + BLE_ADDR_LEN + (size_t)ad_len;

		if (out_size < len) {
			return -EINVAL;
		}

		out[0] = 0x42;
		out[1] = (uint8_t)(BLE_ADDR_LEN + ad_len);
		memcpy(&out[2], adv_addr, BLE_ADDR_LEN);
		memcpy(&out[2 + BLE_ADDR_LEN], ad, (size_t)ad_len);

		return (int)len;
	}

	uint8_t pkt[BLE_ADV_PACKET_MAX];
	int pkt_len = ble_adv_build(adv_addr, ad, (size_t)ad_len, TEST_CHANNEL,
				    pkt, sizeof(pkt));

	if (pkt_len < 0) {
		return pkt_len;
	}

	if (out_size < (size_t)pkt_len - PDU_OFFSET) {
		return -EINVAL;
	}

	memcpy(out, &pkt[PDU_OFFSET], (size_t)pkt_len - PDU_OFFSET);

	return pkt_len - (int)PDU_OFFSET;
}

int main(void)
{
	uint8_t pdu[BLE_ADV_PACKET_MAX];
	uint8_t bpm = BPM_MIN;
	unsigned int sent = 0;
	int len;
	int err;

	err = radio_raw_init(TEST_CHANNEL, RADIO_TEST_HW_CRC);
	if (err != 0) {
		LOG_ERR("radio_raw_init failed (%d)", err);
		return err;
	}

	LOG_INF("advertising on channel %u every %u ms, %s mode", TEST_CHANNEL,
		ADV_INTERVAL_MS,
		RADIO_TEST_HW_CRC ? "control (hardware CRC/whitening)" : "raw");

	while (true) {
		len = build_pdu(bpm, pdu, sizeof(pdu));
		if (len < 0) {
			LOG_ERR("build_pdu failed (%d)", len);
			return len;
		}

		if (sent % PACKETS_PER_BPM_STEP == 0U) {
			LOG_INF("bpm %u", bpm);
			LOG_HEXDUMP_INF(pdu, (size_t)len, "pdu");
		}

		err = radio_raw_send(pdu, (size_t)len);
		if (err != 0) {
			LOG_ERR("radio_raw_send failed (%d)", err);
			return err;
		}

		if (++sent % PACKETS_PER_BPM_STEP == 0U) {
			bpm = (bpm >= BPM_MAX) ? BPM_MIN : (uint8_t)(bpm + 1U);
		}

		k_msleep(ADV_INTERVAL_MS);
	}

	return 0;
}
