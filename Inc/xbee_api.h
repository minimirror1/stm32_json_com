/*
 * xbee_api.h
 *
 *  Created on: Jan 12, 2026
 *      Author: AI Assistant
 *
 *  XBee API Mode 2 (Escaped) Parser and Frame Builder
 *  Based on xBee_test C# implementation
 */

#ifndef INC_XBEE_API_H_
#define INC_XBEE_API_H_

#include <stdint.h>
#include <stdbool.h>
#include "uart_queue.h"

/* ============================================================================
 * XBee API Constants
 * ============================================================================ */

/** Start delimiter for API frames */
#define XBEE_START_DELIMITER    0x7E

/** Escape character for API Mode 2 */
#define XBEE_ESCAPE_CHAR        0x7D

/** XOR value for escape sequence */
#define XBEE_ESCAPE_XOR         0x20

/** Maximum frame data length (excluding start, length, checksum) */
#define XBEE_MAX_FRAME_LEN      256

/** Broadcast address (64-bit) */
#define XBEE_ADDR64_BROADCAST   0x000000000000FFFFULL

/** Unknown/use 16-bit address */
#define XBEE_ADDR16_UNKNOWN     0xFFFE

/* ============================================================================
 * XBee API Frame Types
 * ============================================================================ */

typedef enum {
    XBEE_FRAME_AT_COMMAND           = 0x08,
    XBEE_FRAME_AT_COMMAND_QUEUE     = 0x09,
    XBEE_FRAME_TX_REQUEST           = 0x10,
    XBEE_FRAME_EXPLICIT_TX          = 0x11,
    XBEE_FRAME_REMOTE_AT_COMMAND    = 0x17,
    XBEE_FRAME_AT_COMMAND_RESPONSE  = 0x88,
    XBEE_FRAME_MODEM_STATUS         = 0x8A,
    XBEE_FRAME_TX_STATUS            = 0x8B,
    XBEE_FRAME_RX_PACKET            = 0x90,
    XBEE_FRAME_EXPLICIT_RX          = 0x91,
    XBEE_FRAME_REMOTE_AT_RESPONSE   = 0x97
} XBeeFrameType_t;

/* ============================================================================
 * TX Status Delivery Status Codes
 * ============================================================================ */

typedef enum {
    XBEE_DELIVERY_SUCCESS           = 0x00,
    XBEE_DELIVERY_MAC_ACK_FAIL      = 0x01,
    XBEE_DELIVERY_CCA_FAIL          = 0x02,
    XBEE_DELIVERY_INVALID_DEST      = 0x15,
    XBEE_DELIVERY_NETWORK_ACK_FAIL  = 0x21,
    XBEE_DELIVERY_NOT_JOINED        = 0x22,
    XBEE_DELIVERY_SELF_ADDR         = 0x23,
    XBEE_DELIVERY_ADDR_NOT_FOUND    = 0x24,
    XBEE_DELIVERY_ROUTE_NOT_FOUND   = 0x25,
    XBEE_DELIVERY_PAYLOAD_TOO_LARGE = 0x74
} XBeeDeliveryStatus_t;

/* ============================================================================
 * Parser State Machine
 * ============================================================================ */

typedef enum {
    XBEE_PARSE_WAITING_START,
    XBEE_PARSE_LENGTH_MSB,
    XBEE_PARSE_LENGTH_LSB,
    XBEE_PARSE_FRAME_DATA,
    XBEE_PARSE_CHECKSUM
} XBeeParseState_t;

/* ============================================================================
 * Parsed Frame Structures
 * ============================================================================ */

/** RX Packet (0x90) parsed data */
typedef struct {
    uint64_t source_addr64;
    uint16_t source_addr16;
    uint8_t  receive_options;
    uint8_t* rf_data;
    uint16_t rf_data_len;
} XBeeRxPacket_t;

/** Explicit RX Indicator (0x91) parsed data */
typedef struct {
    uint64_t source_addr64;
    uint16_t source_addr16;
    uint8_t  source_endpoint;
    uint8_t  dest_endpoint;
    uint16_t cluster_id;
    uint16_t profile_id;
    uint8_t  receive_options;
    uint8_t* rf_data;
    uint16_t rf_data_len;
} XBeeExplicitRx_t;

/** TX Status (0x8B) parsed data */
typedef struct {
    uint8_t  frame_id;
    uint16_t dest_addr16;
    uint8_t  transmit_retry_count;
    uint8_t  delivery_status;
    uint8_t  discovery_status;
} XBeeTxStatus_t;

/** AT Command Response (0x88) parsed data */
typedef struct {
    uint8_t  frame_id;
    char     command[3];
    uint8_t  status;
    uint8_t* data;
    uint16_t data_len;
} XBeeAtResponse_t;

/* ============================================================================
 * Generic API Frame
 * ============================================================================ */

typedef struct {
    uint8_t  frame_type;
    uint8_t  data[XBEE_MAX_FRAME_LEN];
    uint16_t data_len;
    
    /* Union of parsed frame types (set based on frame_type) */
    union {
        XBeeRxPacket_t   rx_packet;
        XBeeExplicitRx_t explicit_rx;
        XBeeTxStatus_t   tx_status;
        XBeeAtResponse_t at_response;
    } parsed;
} XBeeFrame_t;

/* ============================================================================
 * Parser Context
 * ============================================================================ */

typedef struct {
    XBeeParseState_t state;
    bool     escaped;
    uint16_t frame_length;
    uint16_t data_index;
    uint8_t  frame_buffer[XBEE_MAX_FRAME_LEN];
    uint8_t  checksum;
} XBeeParser_t;

/* ============================================================================
 * Callback Types
 * ============================================================================ */

/** Callback when a complete frame is received */
typedef void (*XBeeFrameCallback_t)(const XBeeFrame_t* frame, void* user_data);

/** Callback for parse errors */
typedef void (*XBeeErrorCallback_t)(const char* error, void* user_data);

/* ============================================================================
 * XBee Context (combines parser and UART)
 * ============================================================================ */

typedef struct {
    UART_Context* uart;
    XBeeParser_t  parser;
    uint8_t       frame_id_counter;
    uint64_t      local_addr64;         /**< This device's 64-bit address */
    
    /* Callbacks */
    XBeeFrameCallback_t on_frame;
    XBeeErrorCallback_t on_error;
    void* callback_user_data;
} XBeeContext_t;

/* ============================================================================
 * API Functions - Initialization
 * ============================================================================ */

/**
 * @brief Initialize XBee context
 * @param ctx Pointer to XBee context
 * @param uart Pointer to UART context
 */
void xbee_init(XBeeContext_t* ctx, UART_Context* uart);

/**
 * @brief Set callbacks for frame reception
 * @param ctx Pointer to XBee context
 * @param on_frame Callback for received frames
 * @param on_error Callback for parse errors
 * @param user_data User data passed to callbacks
 */
void xbee_set_callbacks(XBeeContext_t* ctx, 
                        XBeeFrameCallback_t on_frame,
                        XBeeErrorCallback_t on_error,
                        void* user_data);

/**
 * @brief Reset parser state
 * @param ctx Pointer to XBee context
 */
void xbee_parser_reset(XBeeContext_t* ctx);

/* ============================================================================
 * API Functions - Processing
 * ============================================================================ */

/**
 * @brief Process incoming bytes from UART (call from main loop)
 * @param ctx Pointer to XBee context
 * 
 * This reads bytes from the UART ring buffer and feeds them to the parser.
 * When a complete frame is received, the on_frame callback is invoked.
 */
void xbee_process(XBeeContext_t* ctx);

/**
 * @brief Process a single byte (for manual byte feeding)
 * @param ctx Pointer to XBee context
 * @param byte Byte to process
 */
void xbee_process_byte(XBeeContext_t* ctx, uint8_t byte);

/* ============================================================================
 * API Functions - Frame Building & Transmission
 * ============================================================================ */

/**
 * @brief Get next frame ID
 * @param ctx Pointer to XBee context
 * @return Next frame ID (1-255, wraps around)
 */
uint8_t xbee_get_next_frame_id(XBeeContext_t* ctx);

/**
 * @brief Send TX Request (0x10) frame
 * @param ctx Pointer to XBee context
 * @param dest_addr64 Destination 64-bit address
 * @param dest_addr16 Destination 16-bit address (use XBEE_ADDR16_UNKNOWN)
 * @param rf_data RF data to send
 * @param rf_data_len Length of RF data
 * @param frame_id Frame ID (0 = no response expected)
 * @return 0 on success, -1 on error
 */
int xbee_send_tx_request(XBeeContext_t* ctx,
                         uint64_t dest_addr64,
                         uint16_t dest_addr16,
                         const uint8_t* rf_data,
                         uint16_t rf_data_len,
                         uint8_t frame_id);

/**
 * @brief Send TX Request without waiting for status (frame_id = 0)
 * @param ctx Pointer to XBee context
 * @param dest_addr64 Destination 64-bit address
 * @param rf_data RF data to send
 * @param rf_data_len Length of RF data
 * @return 0 on success, -1 on error
 */
int xbee_send_data_no_wait(XBeeContext_t* ctx,
                           uint64_t dest_addr64,
                           const uint8_t* rf_data,
                           uint16_t rf_data_len);

/**
 * @brief Send AT Command
 * @param ctx Pointer to XBee context
 * @param command 2-character AT command
 * @param parameter Optional parameter data (can be NULL)
 * @param param_len Parameter length
 * @param frame_id Frame ID for response matching
 * @return 0 on success, -1 on error
 */
int xbee_send_at_command(XBeeContext_t* ctx,
                         const char* command,
                         const uint8_t* parameter,
                         uint16_t param_len,
                         uint8_t frame_id);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Check if a byte needs escaping in API Mode 2
 * @param byte Byte to check
 * @return true if byte needs escaping
 */
static inline bool xbee_needs_escape(uint8_t byte) {
    return (byte == XBEE_START_DELIMITER ||
            byte == XBEE_ESCAPE_CHAR ||
            byte == 0x11 ||  /* XON */
            byte == 0x13);   /* XOFF */
}

#endif /* INC_XBEE_API_H_ */
