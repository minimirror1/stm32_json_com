/*
 * crc16.h
 *
 *  Created on: Jan 12, 2026
 *      Author: AI Assistant
 *
 *  CRC-16-CCITT implementation for Fragment Protocol
 *  Polynomial: 0x1021, Initial: 0xFFFF
 */

#ifndef INC_CRC16_H_
#define INC_CRC16_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Compute CRC-16-CCITT for given data
 * @param data Pointer to data buffer
 * @param len Length of data in bytes
 * @return Computed CRC-16 value
 */
uint16_t crc16_compute(const uint8_t* data, uint16_t len);

/**
 * @brief Verify CRC-16 of data (data should include CRC bytes at end)
 * @param data Pointer to data buffer (including 2 CRC bytes at end)
 * @param len_with_crc Total length including CRC bytes
 * @return true if CRC is valid, false otherwise
 */
bool crc16_verify(const uint8_t* data, uint16_t len_with_crc);

/**
 * @brief Append CRC-16 to data buffer (Big Endian)
 * @param data Pointer to data buffer (must have 2 extra bytes at end)
 * @param len_without_crc Length of data without CRC bytes
 */
void crc16_append(uint8_t* data, uint16_t len_without_crc);

#endif /* INC_CRC16_H_ */
