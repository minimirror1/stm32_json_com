/*
 * fragment_protocol.h
 *
 *  Created on: Jan 12, 2026
 *      Author: AI Assistant
 *
 *  Fragment Protocol constants, structures and types
 *  Based on xBee_test C# implementation
 */

#ifndef INC_FRAGMENT_PROTOCOL_H_
#define INC_FRAGMENT_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Protocol Constants
 * ============================================================================ */

/** Protocol version */
#define FRAG_VERSION            0x01

/** Header size (without CRC): ver(1) + type(1) + msg_id(2) + total_len(4) + frag_idx(2) + frag_cnt(2) + payload_len(1) = 13 */
#define FRAG_HEADER_SIZE        13

/** CRC size in bytes */
#define FRAG_CRC_SIZE           2

/** Maximum payload per fragment (DigiMesh with encryption: NP=49, safe: 30) */
#define FRAG_MAX_PAYLOAD        30

/** Maximum total message size (2KB - RAM constraint) */
#define FRAG_MAX_MESSAGE_SIZE   4096

/** Maximum number of fragments (10KB / 30B = ~342) */
#define FRAG_MAX_FRAGMENTS      ((FRAG_MAX_MESSAGE_SIZE + FRAG_MAX_PAYLOAD - 1) / FRAG_MAX_PAYLOAD)

/* ============================================================================
 * Timing Constants (milliseconds)
 * ============================================================================ */

/** Fragment receive timeout - triggers NACK if no new fragments */
#define FRAG_TIMEOUT_MS         500

/** Session timeout - entire transfer must complete within this */
#define FRAG_SESSION_TIMEOUT_MS 30000

/** Minimum interval between NACKs */
#define FRAG_NACK_INTERVAL_MS   200

/** Maximum NACK retransmission rounds before giving up */
#define FRAG_MAX_NACK_ROUNDS    10

/* ============================================================================
 * Session Limits
 * ============================================================================ */

/** Maximum concurrent RX sessions */
#define FRAG_MAX_RX_SESSIONS    1

/** Maximum concurrent TX sessions */
#define FRAG_MAX_TX_SESSIONS    1

/** Bitmap size for tracking received fragments (512 fragments max) */
#define FRAG_BITMAP_SIZE        64

/* ============================================================================
 * Message Types
 * ============================================================================ */

typedef enum {
    FRAG_TYPE_DATA = 0x01,  /**< Data fragment */
    FRAG_TYPE_NACK = 0x02,  /**< Negative acknowledgment (request retransmit) */
    FRAG_TYPE_DONE = 0x03   /**< Transfer complete acknowledgment */
} FragmentType_t;

/* ============================================================================
 * Fragment Header Structure (13 bytes, Big Endian)
 * ============================================================================ */

typedef struct {
    uint8_t  version;       /**< Protocol version (0x01) */
    uint8_t  type;          /**< Message type (FragmentType_t) */
    uint16_t msg_id;        /**< Message ID (unique per transfer) */
    uint32_t total_len;     /**< Total message length in bytes */
    uint16_t frag_idx;      /**< Fragment index (0-based) */
    uint16_t frag_cnt;      /**< Total fragment count */
    uint8_t  payload_len;   /**< Payload length in this fragment (0-30) */
} FragmentHeader_t;

/* ============================================================================
 * RX Session Structure
 * ============================================================================ */

typedef struct {
    uint8_t  active;                                /**< Session active flag */
    uint16_t msg_id;                                /**< Message ID */
    uint32_t total_len;                             /**< Expected total length */
    uint16_t frag_cnt;                              /**< Expected fragment count */
    uint8_t  received_bitmap[FRAG_BITMAP_SIZE];     /**< Bitmap of received fragments */
    uint8_t  payload_buffer[FRAG_MAX_MESSAGE_SIZE]; /**< Reassembly buffer */
    uint32_t last_activity_tick;                    /**< Last fragment received tick */
    uint32_t start_tick;                            /**< Session start tick */
    uint8_t  nacks_sent;                            /**< NACK counter */
    uint64_t source_address;                        /**< Source XBee address */
} RxSession_t;

/* ============================================================================
 * TX Session Structure
 * ============================================================================ */

typedef struct {
    uint8_t  active;            /**< Session active flag */
    uint16_t msg_id;            /**< Message ID */
    const uint8_t* data;        /**< Pointer to data to send */
    uint32_t data_len;          /**< Data length */
    uint16_t frag_cnt;          /**< Total fragment count */
    uint16_t current_frag;      /**< Current fragment being sent */
    uint8_t  nack_rounds;       /**< NACK round counter */
    uint32_t start_tick;        /**< Session start tick */
    uint8_t  waiting_done;      /**< Waiting for DONE flag */
    uint64_t dest_address;      /**< Destination XBee address */
} TxSession_t;

/* ============================================================================
 * NACK Message Structure
 * ============================================================================ */

/** Maximum missing indices in a single NACK */
#define FRAG_MAX_NACK_INDICES   20

typedef struct {
    uint16_t msg_id;                                /**< Message ID */
    uint8_t  count;                                 /**< Number of missing indices */
    uint16_t missing_indices[FRAG_MAX_NACK_INDICES]; /**< Missing fragment indices */
} NackMessage_t;

/* ============================================================================
 * Helper Functions (inline for header serialization)
 * ============================================================================ */

/**
 * @brief Write fragment header to buffer (Big Endian)
 * @param header Pointer to header structure
 * @param buffer Output buffer (must be at least FRAG_HEADER_SIZE bytes)
 */
static inline void frag_header_write(const FragmentHeader_t* header, uint8_t* buffer) {
    buffer[0] = header->version;
    buffer[1] = header->type;
    buffer[2] = (uint8_t)(header->msg_id >> 8);
    buffer[3] = (uint8_t)(header->msg_id & 0xFF);
    buffer[4] = (uint8_t)(header->total_len >> 24);
    buffer[5] = (uint8_t)(header->total_len >> 16);
    buffer[6] = (uint8_t)(header->total_len >> 8);
    buffer[7] = (uint8_t)(header->total_len & 0xFF);
    buffer[8] = (uint8_t)(header->frag_idx >> 8);
    buffer[9] = (uint8_t)(header->frag_idx & 0xFF);
    buffer[10] = (uint8_t)(header->frag_cnt >> 8);
    buffer[11] = (uint8_t)(header->frag_cnt & 0xFF);
    buffer[12] = header->payload_len;
}

/**
 * @brief Read fragment header from buffer (Big Endian)
 * @param buffer Input buffer (must be at least FRAG_HEADER_SIZE bytes)
 * @param header Output header structure
 */
static inline void frag_header_read(const uint8_t* buffer, FragmentHeader_t* header) {
    header->version = buffer[0];
    header->type = buffer[1];
    header->msg_id = (uint16_t)((buffer[2] << 8) | buffer[3]);
    header->total_len = ((uint32_t)buffer[4] << 24) |
                        ((uint32_t)buffer[5] << 16) |
                        ((uint32_t)buffer[6] << 8) |
                        buffer[7];
    header->frag_idx = (uint16_t)((buffer[8] << 8) | buffer[9]);
    header->frag_cnt = (uint16_t)((buffer[10] << 8) | buffer[11]);
    header->payload_len = buffer[12];
}

/**
 * @brief Check if a fragment index is marked as received in bitmap
 * @param bitmap Received bitmap array
 * @param idx Fragment index
 * @return true if received, false otherwise
 */
static inline bool frag_bitmap_get(const uint8_t* bitmap, uint16_t idx) {
    return (bitmap[idx / 8] & (1 << (idx % 8))) != 0;
}

/**
 * @brief Mark a fragment index as received in bitmap
 * @param bitmap Received bitmap array
 * @param idx Fragment index
 */
static inline void frag_bitmap_set(uint8_t* bitmap, uint16_t idx) {
    bitmap[idx / 8] |= (1 << (idx % 8));
}

/**
 * @brief Clear the received bitmap
 * @param bitmap Received bitmap array
 */
static inline void frag_bitmap_clear(uint8_t* bitmap) {
    for (int i = 0; i < FRAG_BITMAP_SIZE; i++) {
        bitmap[i] = 0;
    }
}

#endif /* INC_FRAGMENT_PROTOCOL_H_ */
