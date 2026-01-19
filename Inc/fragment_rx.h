/*
 * fragment_rx.h
 *
 *  Created on: Jan 12, 2026
 *      Author: AI Assistant
 *
 *  Fragment Receiver - handles fragment reception, NACK generation, and message reassembly
 *  Based on xBee_test C# FragmentReceiver implementation
 */

#ifndef INC_FRAGMENT_RX_H_
#define INC_FRAGMENT_RX_H_

#include <stdint.h>
#include <stdbool.h>
#include "fragment_protocol.h"
#include "xbee_api.h"

/* ============================================================================
 * Callback Types
 * ============================================================================ */

/**
 * @brief Callback when a complete message is received
 * @param data Pointer to reassembled message data
 * @param len Length of message in bytes
 * @param source_addr Source XBee 64-bit address
 * @param user_data User data passed during init
 */
typedef void (*FragRxMessageCallback_t)(const uint8_t* data, uint32_t len, 
                                         uint64_t source_addr, void* user_data);

/**
 * @brief Callback for NACK transmission
 * @param nack Pointer to NACK message to send
 * @param dest_addr Destination XBee address
 * @param user_data User data
 */
typedef void (*FragRxNackCallback_t)(const NackMessage_t* nack, uint64_t dest_addr, void* user_data);

/**
 * @brief Callback for DONE transmission
 * @param msg_id Message ID
 * @param dest_addr Destination XBee address
 * @param user_data User data
 */
typedef void (*FragRxDoneCallback_t)(uint16_t msg_id, uint64_t dest_addr, void* user_data);

/**
 * @brief Callback for logging
 * @param message Log message
 * @param user_data User data
 */
typedef void (*FragRxLogCallback_t)(const char* message, void* user_data);

/* ============================================================================
 * Fragment Receiver Context
 * ============================================================================ */

typedef struct {
    /* RX Sessions (static allocation) */
    RxSession_t sessions[FRAG_MAX_RX_SESSIONS];
    
    /* XBee context for sending NACK/DONE */
    XBeeContext_t* xbee;
    
    /* Callbacks */
    FragRxMessageCallback_t on_message;
    FragRxNackCallback_t    on_nack;
    FragRxDoneCallback_t    on_done;
    FragRxLogCallback_t     on_log;
    void* callback_user_data;
    
    /* Statistics */
    uint32_t total_fragments_received;
    uint32_t crc_failures;
    uint32_t nacks_sent;
    uint32_t messages_completed;
} FragRxContext_t;

/* ============================================================================
 * API Functions
 * ============================================================================ */

/**
 * @brief Initialize fragment receiver
 * @param ctx Pointer to receiver context
 * @param xbee Pointer to XBee context (for sending NACK/DONE)
 */
void frag_rx_init(FragRxContext_t* ctx, XBeeContext_t* xbee);

/**
 * @brief Set callbacks for the receiver
 * @param ctx Pointer to receiver context
 * @param on_message Callback for complete messages
 * @param on_log Callback for logging (optional)
 * @param user_data User data passed to callbacks
 */
void frag_rx_set_callbacks(FragRxContext_t* ctx,
                           FragRxMessageCallback_t on_message,
                           FragRxLogCallback_t on_log,
                           void* user_data);

/**
 * @brief Process incoming RF data (fragment, NACK, or DONE)
 * @param ctx Pointer to receiver context
 * @param data RF data from XBee
 * @param len Length of data
 * @param source_addr Source XBee 64-bit address
 * 
 * This should be called when an XBee RX Packet (0x90) is received.
 * The data should be the RF payload (Fragment Protocol data).
 */
void frag_rx_process(FragRxContext_t* ctx, const uint8_t* data, uint16_t len, uint64_t source_addr);

/**
 * @brief Periodic tick function for timeout handling
 * @param ctx Pointer to receiver context
 * 
 * This should be called periodically (e.g., every 100ms) from the main loop.
 * Handles:
 * - Activity timeout: sends NACK if no fragments received recently
 * - Session timeout: cleans up stale sessions
 */
void frag_rx_tick(FragRxContext_t* ctx);

/**
 * @brief Check if a NACK message should be forwarded to TX
 * @param data RF data
 * @param len Length of data
 * @return true if this is a NACK message
 */
bool frag_rx_is_nack(const uint8_t* data, uint16_t len);

/**
 * @brief Check if a DONE message should be forwarded to TX
 * @param data RF data
 * @param len Length of data
 * @return true if this is a DONE message
 */
bool frag_rx_is_done(const uint8_t* data, uint16_t len);

/**
 * @brief Parse NACK message from RF data
 * @param data RF data
 * @param len Length of data
 * @param nack Output NACK message
 * @return true if successfully parsed
 */
bool frag_rx_parse_nack(const uint8_t* data, uint16_t len, NackMessage_t* nack);

/**
 * @brief Parse DONE message from RF data
 * @param data RF data
 * @param len Length of data
 * @param msg_id Output message ID
 * @return true if successfully parsed
 */
bool frag_rx_parse_done(const uint8_t* data, uint16_t len, uint16_t* msg_id);

/**
 * @brief Clear all active sessions
 * @param ctx Pointer to receiver context
 */
void frag_rx_clear_sessions(FragRxContext_t* ctx);

/**
 * @brief Reset statistics
 * @param ctx Pointer to receiver context
 */
void frag_rx_reset_stats(FragRxContext_t* ctx);

/**
 * @brief Get number of active sessions
 * @param ctx Pointer to receiver context
 * @return Number of active RX sessions
 */
int frag_rx_get_active_sessions(FragRxContext_t* ctx);

#endif /* INC_FRAGMENT_RX_H_ */
