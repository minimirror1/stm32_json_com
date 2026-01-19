/*
 * fragment_tx.h
 *
 *  Created on: Jan 12, 2026
 *      Author: AI Assistant
 *
 *  Fragment Transmitter - handles message fragmentation and transmission with NACK-based retransmission
 *  Based on xBee_test C# FragmentTransmitter implementation
 */

#ifndef INC_FRAGMENT_TX_H_
#define INC_FRAGMENT_TX_H_

#include <stdint.h>
#include <stdbool.h>
#include "fragment_protocol.h"
#include "xbee_api.h"

/* ============================================================================
 * TX State
 * ============================================================================ */

typedef enum {
    FRAG_TX_STATE_IDLE,         /**< No active transmission */
    FRAG_TX_STATE_SENDING,      /**< Sending fragments */
    FRAG_TX_STATE_WAITING_DONE, /**< All fragments sent, waiting for DONE */
    FRAG_TX_STATE_COMPLETE,     /**< Transmission complete */
    FRAG_TX_STATE_FAILED        /**< Transmission failed */
} FragTxState_t;

/* ============================================================================
 * Callback Types
 * ============================================================================ */

/**
 * @brief Callback when transmission is complete
 * @param msg_id Message ID
 * @param success true if DONE received, false if timeout/failed
 * @param user_data User data
 */
typedef void (*FragTxCompleteCallback_t)(uint16_t msg_id, bool success, void* user_data);

/**
 * @brief Callback for logging
 * @param message Log message
 * @param user_data User data
 */
typedef void (*FragTxLogCallback_t)(const char* message, void* user_data);

/* ============================================================================
 * TX Session Structure (Extended with state machine)
 * ============================================================================ */

typedef struct {
    TxSession_t base;           /**< Base session data */
    FragTxState_t state;        /**< Current state */
    uint32_t last_send_tick;    /**< Last fragment send time */
    uint16_t send_delay_ms;     /**< Delay between fragments */
} TxSessionEx_t;

/* ============================================================================
 * Fragment Transmitter Context
 * ============================================================================ */

typedef struct {
    /* TX Sessions (static allocation) */
    TxSessionEx_t sessions[FRAG_MAX_TX_SESSIONS];
    
    /* Message ID counter */
    uint16_t next_msg_id;
    
    /* XBee context for sending */
    XBeeContext_t* xbee;
    
    /* Payload size per fragment (configurable MTU) */
    uint8_t payload_size;
    
    /* Callbacks */
    FragTxCompleteCallback_t on_complete;
    FragTxLogCallback_t on_log;
    void* callback_user_data;
    
    /* Statistics */
    uint32_t total_fragments_sent;
    uint32_t retransmitted_fragments;
    uint32_t messages_completed;
    uint32_t messages_failed;
} FragTxContext_t;

/* ============================================================================
 * API Functions
 * ============================================================================ */

/**
 * @brief Initialize fragment transmitter
 * @param ctx Pointer to transmitter context
 * @param xbee Pointer to XBee context
 */
void frag_tx_init(FragTxContext_t* ctx, XBeeContext_t* xbee);

/**
 * @brief Set callbacks for the transmitter
 * @param ctx Pointer to transmitter context
 * @param on_complete Callback for transmission complete
 * @param on_log Callback for logging (optional)
 * @param user_data User data passed to callbacks
 */
void frag_tx_set_callbacks(FragTxContext_t* ctx,
                           FragTxCompleteCallback_t on_complete,
                           FragTxLogCallback_t on_log,
                           void* user_data);

/**
 * @brief Set payload size (MTU) per fragment
 * @param ctx Pointer to transmitter context
 * @param size Payload size (10-34 bytes, default 30)
 */
void frag_tx_set_payload_size(FragTxContext_t* ctx, uint8_t size);

/**
 * @brief Start sending a message (non-blocking)
 * @param ctx Pointer to transmitter context
 * @param data Data to send
 * @param len Length of data
 * @param dest_addr64 Destination XBee 64-bit address
 * @return Message ID on success, 0 on failure (no free slots)
 * 
 * This starts the transmission. Call frag_tx_tick() periodically to
 * actually send the fragments and handle the state machine.
 */
uint16_t frag_tx_send(FragTxContext_t* ctx, const uint8_t* data, uint32_t len, uint64_t dest_addr64);

/**
 * @brief Periodic tick function for transmission state machine
 * @param ctx Pointer to transmitter context
 * 
 * This should be called periodically (e.g., every 10-50ms) from the main loop.
 * Handles:
 * - Sending fragments with proper timing
 * - Timeout handling
 */
void frag_tx_tick(FragTxContext_t* ctx);

/**
 * @brief Handle incoming NACK - retransmit missing fragments
 * @param ctx Pointer to transmitter context
 * @param nack NACK message
 * @param source_addr Source address (for verification)
 */
void frag_tx_handle_nack(FragTxContext_t* ctx, const NackMessage_t* nack, uint64_t source_addr);

/**
 * @brief Handle incoming DONE - mark session complete
 * @param ctx Pointer to transmitter context
 * @param msg_id Message ID from DONE
 */
void frag_tx_handle_done(FragTxContext_t* ctx, uint16_t msg_id);

/**
 * @brief Check if transmitter is busy (has active sessions)
 * @param ctx Pointer to transmitter context
 * @return true if there are active transmissions
 */
bool frag_tx_is_busy(FragTxContext_t* ctx);

/**
 * @brief Get state of a specific message transmission
 * @param ctx Pointer to transmitter context
 * @param msg_id Message ID
 * @return State of the transmission
 */
FragTxState_t frag_tx_get_state(FragTxContext_t* ctx, uint16_t msg_id);

/**
 * @brief Cancel a pending transmission
 * @param ctx Pointer to transmitter context
 * @param msg_id Message ID
 */
void frag_tx_cancel(FragTxContext_t* ctx, uint16_t msg_id);

/**
 * @brief Clear all active sessions
 * @param ctx Pointer to transmitter context
 */
void frag_tx_clear_sessions(FragTxContext_t* ctx);

/**
 * @brief Reset statistics
 * @param ctx Pointer to transmitter context
 */
void frag_tx_reset_stats(FragTxContext_t* ctx);

/**
 * @brief Get next message ID
 * @param ctx Pointer to transmitter context
 * @return Next message ID
 */
uint16_t frag_tx_get_next_msg_id(FragTxContext_t* ctx);

#endif /* INC_FRAGMENT_TX_H_ */
