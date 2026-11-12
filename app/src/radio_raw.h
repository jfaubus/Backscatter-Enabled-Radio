/*
 * Bare-metal BLE 1M transmitter on the nRF52840 RADIO peripheral, for
 * checking ble_adv_build() output against a real receiver (a phone running
 * nRF Connect). Zephyr's Bluetooth stack is deliberately not used: it would
 * build its own packets and the test would prove nothing about ours.
 *
 * Two modes:
 *
 *   raw      Hardware CRC and whitening OFF. The radio sends our bytes
 *            verbatim after its own preamble and access address, so the
 *            phone only sees the packet if ble_adv_build()'s CRC and
 *            whitening are right. This is the actual test.
 *
 *   control  Hardware CRC and whitening ON, fed an un-whitened PDU. If the
 *            phone sees this but not raw mode, the radio setup is fine and
 *            the bug is in ble_adv.c.
 *
 * The RADIO needs the 32 MHz crystal; radio_raw_init() requests it.
 */

#ifndef RADIO_RAW_H_
#define RADIO_RAW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Configure the RADIO for BLE 1M advertising on one channel.
 *
 * @param channel Advertising channel index: 37, 38 or 39.
 * @param hw_crc  false for raw mode, true for control mode (see above).
 *
 * @return 0 on success, -EINVAL for a bad channel, otherwise whatever
 *         hfxo_request() returned.
 */
int radio_raw_init(uint8_t channel, bool hw_crc);

/**
 * Transmit one packet and block until it is out (~300 us).
 *
 * @param pdu Raw mode: ble_adv_build() output from the PDU header onward
 *            (header, AdvA, AdvData, CRC -- whitened). Control mode: the
 *            un-whitened header + AdvA + AdvData, no CRC.
 * @param len Bytes in @p pdu, at most 255.
 *
 * @return 0 on success, -EPERM if not initialised, -EINVAL for a bad length.
 */
int radio_raw_send(const uint8_t *pdu, size_t len);

#endif /* RADIO_RAW_H_ */
