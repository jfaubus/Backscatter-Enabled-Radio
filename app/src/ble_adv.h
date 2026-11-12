/*
 * BLE advertising packet builder (LE 1M, legacy advertising).
 *
 * Produces the exact byte stream that goes on air
 * (ready for modulator.c)
 *
 *   preamble | access address | PDU header | AdvA | AdvData | CRC
 *     1 B          4 B             2 B        6 B   0-31 B    3 B
 *                            '---------- whitened ---------------'
 *
 * Bytes are in transmit order and each byte goes out LSB first
 * (Spec required)
 *
 * The PDU is ADV_NONCONN_IND: broadcast only, so the tag never has to
 * receive anything (because we can't receive anything on our pcb)
 *
 * no Zephyr dependencies in this file
 */

#ifndef BLE_ADV_H_
#define BLE_ADV_H_

#include <stddef.h>
#include <stdint.h>

// Advertising channel index so we can test multiple 
#define BLE_ADV_CHANNEL_37 37U // 2402 MHz
#define BLE_ADV_CHANNEL_38 38U // 2426 MHz
#define BLE_ADV_CHANNEL_39 39U // 2480 MHz

// Longest AdvData a legacy advertising PDU can carry 
// Legacy advertising = broadcaster can announce presence and send data
#define BLE_ADV_DATA_MAX 31U

// Preamble + access address + header + AdvA + max AdvData + CRC
#define BLE_ADV_PACKET_MAX (1U + 4U + 2U + 6U + BLE_ADV_DATA_MAX + 3U)

// Bytes in a BLE device address
#define BLE_ADDR_LEN 6U

/**
 * Build a whitened ADV_NONCONN_IND packet
 *
 * @param adv_addr     Advertiser address, least significant byte first
 *                     (the order it goes on air). Treated as a random
 *                     address, so a static one needs the top two bits of
 *                     adv_addr[5] set.
 * @param adv_data     AD structures (length, type, data...), or NULL if
 *                     @p adv_data_len is 0.
 * @param adv_data_len 0..BLE_ADV_DATA_MAX bytes.
 * @param channel      Advertising channel index the packet will be received
 *                     on (37, 38 or 39). Whitening depends on it, so a packet
 *                     built for one channel will not decode on another.
 * @param out          Output buffer.
 * @param out_size     Size of @p out; BLE_ADV_PACKET_MAX always suffices.
 *
 * @return Packet length in bytes (multiply by 8 for modulator_build()), or
 *         -EINVAL for a bad channel, oversized AdvData or a short buffer.
 */
int ble_adv_build(const uint8_t adv_addr[BLE_ADDR_LEN],
		  const uint8_t *adv_data, size_t adv_data_len,
		  uint8_t channel, uint8_t *out, size_t out_size);

/**
 * Fill in the AdvData for a heart-rate reading:
 *
 *   Flags                 02 01 06
 *   Complete Local Name   07 09 'B' 'E' 'A' 'C' 'O' 'N'
 *   Service Data (0x180D) 05 16 0D 18 00 <bpm>
 *
 * The service data payload follows the Heart Rate Measurement layout (a flags
 * byte of 0x00, meaning a uint8 BPM follows), so it reads naturally in tools
 * like nRF Connect. The name makes the tag easy to spot in a scan list.
 *
 * @return Number of AdvData bytes written, or -EINVAL if @p out_size is too
 *         small.
 */
int ble_adv_hr_data(uint8_t bpm, uint8_t *out, size_t out_size);

#endif /* BLE_ADV_H_ */
