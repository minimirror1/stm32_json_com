/*
 * json_com.h
 *
 *  Created on: Dec 2, 2025
 *      Author: AI Assistant
 *
 *  JSON Communication Library with XBee Fragment Protocol support
 */

#ifndef INC_JSON_COM_H_
#define INC_JSON_COM_H_

#include "uart_queue.h"
#include "xbee_api.h"
#include "fragment_rx.h"
#include "fragment_tx.h"
#include <stdint.h>

#define MAX_JSON_LEN 1024

/** @brief Broadcast address - message will be received by all devices */
#define RS485_BROADCAST_ID 0xFF

/**
 * JSON packet protocol (Fragment Protocol over XBee DigiMesh)
 *
 * The protocol now uses XBee API Mode 2 with Fragment Protocol for reliable
 * transmission of large JSON messages (up to 10KB).
 *
 * Common fields:
 * - msg   : required, one of "req" | "resp" | "evt"
 * - src_id: sender device id (0-255)
 * - tar_id: target device id (0-255) or broadcast id (see RS485_BROADCAST_ID)
 * - cmd   : command/event name (string)
 * - payload: optional object/array with command data
 *
 * Request ("req"):
 * - msg:"req"
 * - MUST NOT include top-level "status" (reserved for responses)
 *
 * Response ("resp"):
 * - msg:"resp"
 * - includes status:"ok"|"error" and optional payload/message
 *
 * Event ("evt"):
 * - msg:"evt"
 * - typically no top-level status (unless separately specified)
 */

#define JSON_MSG_REQ  "req"
#define JSON_MSG_RESP "resp"
#define JSON_MSG_EVT  "evt"

/**
 * @brief TX Buffer for response building
 * 
 * Since Fragment TX needs a stable pointer to data during async transmission,
 * we need a dedicated buffer for building responses.
 */
#define JSON_TX_BUFFER_SIZE 2048

typedef struct {
    /* UART context (dependency) */
    UART_Context *uart;
    
    /* Device ID */
    uint8_t my_id;
    
    /* XBee context for RF communication */
    XBeeContext_t xbee;
    
    /* Fragment Protocol contexts */
    FragRxContext_t frag_rx;
    FragTxContext_t frag_tx;
    
    /* Current source address for responding */
    uint64_t current_source_addr;
    
    /* TX buffer for building responses */
    uint8_t tx_buffer[JSON_TX_BUFFER_SIZE];
    uint32_t tx_buffer_len;
    
    /* RX line buffer (for reassembled messages) */
    char rx_line_buffer[MAX_JSON_LEN];
    uint16_t rx_index;
} JSON_Context;

/**
 * @brief Initialize JSON communication with XBee Fragment Protocol
 * @param ctx Pointer to JSON context
 * @param uart Pointer to UART context (connected to XBee)
 * @param my_id Device ID for addressing
 */
void JSON_COM_Init(JSON_Context *ctx, UART_Context *uart, uint8_t my_id);

/**
 * @brief Process incoming data (call from main loop)
 * @param ctx Pointer to JSON context
 * 
 * This function:
 * 1. Reads bytes from UART and feeds to XBee parser
 * 2. When XBee frame is received, processes Fragment Protocol
 * 3. When message is reassembled, parses JSON and executes commands
 * 4. Sends responses via Fragment Protocol
 */
void JSON_COM_Process(JSON_Context *ctx);

/**
 * @brief Periodic tick for timeout handling
 * @param ctx Pointer to JSON context
 * 
 * This should be called periodically (e.g., every 100ms) from main loop
 * or a timer. Handles Fragment Protocol timeouts and retransmissions.
 */
void JSON_COM_Tick(JSON_Context *ctx);

/**
 * @brief Set destination XBee address for responses
 * @param ctx Pointer to JSON context
 * @param addr64 64-bit XBee address
 */
void JSON_COM_SetDestAddress(JSON_Context *ctx, uint64_t addr64);

/**
 * @brief Send a JSON string via Fragment Protocol
 * @param ctx Pointer to JSON context
 * @param json_str JSON string to send
 * @param dest_addr64 Destination 64-bit address
 * @return Message ID on success, 0 on failure
 */
uint16_t JSON_COM_SendString(JSON_Context *ctx, const char *json_str, uint64_t dest_addr64);

/**
 * @brief Check if there are active transmissions
 * @param ctx Pointer to JSON context
 * @return true if TX is busy
 */
bool JSON_COM_IsTxBusy(JSON_Context *ctx);

#endif /* INC_JSON_COM_H_ */
