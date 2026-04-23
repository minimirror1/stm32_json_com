/*
 * xbee_api.c
 *
 *  Created on: Jan 12, 2026
 *      Author: AI Assistant
 *
 *  XBee API Mode 2 (Escaped) Parser and Frame Builder
 *  Based on xBee_test C# implementation
 */

#include "xbee_api.h"
#include <string.h>

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @brief Read 64-bit big endian value from buffer
 */
static uint64_t read_uint64_be(const uint8_t* buf) {
    return ((uint64_t)buf[0] << 56) |
           ((uint64_t)buf[1] << 48) |
           ((uint64_t)buf[2] << 40) |
           ((uint64_t)buf[3] << 32) |
           ((uint64_t)buf[4] << 24) |
           ((uint64_t)buf[5] << 16) |
           ((uint64_t)buf[6] << 8) |
           buf[7];
}

/**
 * @brief Read 16-bit big endian value from buffer
 */
static uint16_t read_uint16_be(const uint8_t* buf) {
    return (uint16_t)((buf[0] << 8) | buf[1]);
}

/* Note: write_uint64_be and write_uint16_be removed - inline in send functions */

/**
 * @brief Parse RX Packet (0x90) frame
 * Format: [FrameType][64-bit Src][16-bit Src][Options][RF Data...]
 */
static bool parse_rx_packet(XBeeFrame_t* frame) {
    /* Minimum: 1 (type) + 8 (addr64) + 2 (addr16) + 1 (options) = 12 */
    if (frame->data_len < 11) {
        return false;
    }
    
    frame->parsed.rx_packet.source_addr64 = read_uint64_be(frame->data);
    frame->parsed.rx_packet.source_addr16 = read_uint16_be(&frame->data[8]);
    frame->parsed.rx_packet.receive_options = frame->data[10];
    
    uint16_t rf_data_len = frame->data_len - 11;
    if (rf_data_len > 0) {
        frame->parsed.rx_packet.rf_data = &frame->data[11];
        frame->parsed.rx_packet.rf_data_len = rf_data_len;
    } else {
        frame->parsed.rx_packet.rf_data = NULL;
        frame->parsed.rx_packet.rf_data_len = 0;
    }
    
    return true;
}

/**
 * @brief Parse Explicit RX Indicator (0x91) frame
 * Format: [FrameType][64-bit Src][16-bit Src][SrcEP][DestEP][ClusterID][ProfileID][Options][RF Data...]
 */
static bool parse_explicit_rx(XBeeFrame_t* frame) {
    /* Minimum: 1 (type) + 8 (addr64) + 2 (addr16) + 1 (srcEP) + 1 (destEP) + 2 (cluster) + 2 (profile) + 1 (options) = 18 - 1 = 17 */
    if (frame->data_len < 17) {
        return false;
    }
    
    frame->parsed.explicit_rx.source_addr64 = read_uint64_be(frame->data);
    frame->parsed.explicit_rx.source_addr16 = read_uint16_be(&frame->data[8]);
    frame->parsed.explicit_rx.source_endpoint = frame->data[10];
    frame->parsed.explicit_rx.dest_endpoint = frame->data[11];
    frame->parsed.explicit_rx.cluster_id = read_uint16_be(&frame->data[12]);
    frame->parsed.explicit_rx.profile_id = read_uint16_be(&frame->data[14]);
    frame->parsed.explicit_rx.receive_options = frame->data[16];
    
    uint16_t rf_data_len = frame->data_len - 17;
    if (rf_data_len > 0) {
        frame->parsed.explicit_rx.rf_data = &frame->data[17];
        frame->parsed.explicit_rx.rf_data_len = rf_data_len;
    } else {
        frame->parsed.explicit_rx.rf_data = NULL;
        frame->parsed.explicit_rx.rf_data_len = 0;
    }
    
    return true;
}

/**
 * @brief Parse TX Status (0x8B) frame
 * Format: [FrameType][FrameId][16-bit Dest][RetryCount][DeliveryStatus][DiscoveryStatus]
 */
static bool parse_tx_status(XBeeFrame_t* frame) {
    /* Expect: 1 + 2 + 1 + 1 + 1 = 6 (excluding frame type in data) */
    if (frame->data_len < 6) {
        return false;
    }
    
    frame->parsed.tx_status.frame_id = frame->data[0];
    frame->parsed.tx_status.dest_addr16 = read_uint16_be(&frame->data[1]);
    frame->parsed.tx_status.transmit_retry_count = frame->data[3];
    frame->parsed.tx_status.delivery_status = frame->data[4];
    frame->parsed.tx_status.discovery_status = frame->data[5];
    
    return true;
}

/**
 * @brief Parse AT Command Response (0x88) frame
 * Format: [FrameType][FrameId][AT Command 2 chars][Status][Data...]
 */
static bool parse_at_response(XBeeFrame_t* frame) {
    /* Minimum: 1 (frameId) + 2 (command) + 1 (status) = 4 */
    if (frame->data_len < 4) {
        return false;
    }
    
    frame->parsed.at_response.frame_id = frame->data[0];
    frame->parsed.at_response.command[0] = (char)frame->data[1];
    frame->parsed.at_response.command[1] = (char)frame->data[2];
    frame->parsed.at_response.command[2] = '\0';
    frame->parsed.at_response.status = frame->data[3];
    
    uint16_t data_len = frame->data_len - 4;
    if (data_len > 0) {
        frame->parsed.at_response.data = &frame->data[4];
        frame->parsed.at_response.data_len = data_len;
    } else {
        frame->parsed.at_response.data = NULL;
        frame->parsed.at_response.data_len = 0;
    }
    
    return true;
}

/**
 * @brief Parse complete frame based on frame type
 */
static void parse_frame(XBeeFrame_t* frame, XBeeContext_t* ctx) {
    bool success = true;
    
    switch (frame->frame_type) {
        case XBEE_FRAME_RX_PACKET:
            success = parse_rx_packet(frame);
            break;
            
        case XBEE_FRAME_EXPLICIT_RX:
            success = parse_explicit_rx(frame);
            break;
            
        case XBEE_FRAME_TX_STATUS:
            success = parse_tx_status(frame);
            break;
            
        case XBEE_FRAME_AT_COMMAND_RESPONSE:
            success = parse_at_response(frame);
            break;
            
        default:
            /* Unknown frame type - data is still available in frame->data */
            break;
    }
    
    if (!success && ctx->on_error) {
        ctx->on_error("Frame parse error", ctx->callback_user_data);
    }
}

/* ============================================================================
 * API Functions - Initialization
 * ============================================================================ */

void xbee_init(XBeeContext_t* ctx, UART_Context* uart) {
    memset(ctx, 0, sizeof(XBeeContext_t));
    ctx->uart = uart;
    ctx->frame_id_counter = 1;
    xbee_parser_reset(ctx);
}

void xbee_set_callbacks(XBeeContext_t* ctx,
                        XBeeFrameCallback_t on_frame,
                        XBeeErrorCallback_t on_error,
                        void* user_data) {
    ctx->on_frame = on_frame;
    ctx->on_error = on_error;
    ctx->callback_user_data = user_data;
}

void xbee_parser_reset(XBeeContext_t* ctx) {
    ctx->parser.state = XBEE_PARSE_WAITING_START;
    ctx->parser.escaped = false;
    ctx->parser.data_index = 0;
    ctx->parser.frame_length = 0;
    ctx->parser.checksum = 0;
}

/* ============================================================================
 * API Functions - Processing
 * ============================================================================ */

void xbee_process_byte(XBeeContext_t* ctx, uint8_t byte) {
    XBeeParser_t* parser = &ctx->parser;
    
    /* Handle escape sequence (API Mode 2) */
    if (parser->escaped) {
        byte = byte ^ XBEE_ESCAPE_XOR;
        parser->escaped = false;
    } else if (byte == XBEE_ESCAPE_CHAR && parser->state != XBEE_PARSE_WAITING_START) {
        parser->escaped = true;
        return;
    }
    
    switch (parser->state) {
        case XBEE_PARSE_WAITING_START:
            if (byte == XBEE_START_DELIMITER) {
                parser->state = XBEE_PARSE_LENGTH_MSB;
                parser->checksum = 0;
                parser->data_index = 0;
            }
            break;
            
        case XBEE_PARSE_LENGTH_MSB:
            parser->frame_length = (uint16_t)(byte << 8);
            parser->state = XBEE_PARSE_LENGTH_LSB;
            break;
            
        case XBEE_PARSE_LENGTH_LSB:
            parser->frame_length |= byte;
            if (parser->frame_length > XBEE_MAX_FRAME_LEN || parser->frame_length == 0) {
                if (ctx->on_error) {
                    ctx->on_error("Invalid frame length", ctx->callback_user_data);
                }
                xbee_parser_reset(ctx);
            } else {
                parser->state = XBEE_PARSE_FRAME_DATA;
            }
            break;
            
        case XBEE_PARSE_FRAME_DATA:
            parser->frame_buffer[parser->data_index++] = byte;
            parser->checksum += byte;
            if (parser->data_index >= parser->frame_length) {
                parser->state = XBEE_PARSE_CHECKSUM;
            }
            break;
            
        case XBEE_PARSE_CHECKSUM:
            parser->checksum += byte;
            if (parser->checksum == 0xFF) {
                /* Valid frame - parse and emit */
                XBeeFrame_t frame;
                memset(&frame, 0, sizeof(XBeeFrame_t));
                
                frame.frame_type = parser->frame_buffer[0];
                frame.data_len = parser->frame_length - 1;
                if (frame.data_len > 0) {
                    memcpy(frame.data, &parser->frame_buffer[1], frame.data_len);
                }
                
                /* Parse specific frame type */
                parse_frame(&frame, ctx);
                
                /* Invoke callback */
                if (ctx->on_frame) {
                    ctx->on_frame(&frame, ctx->callback_user_data);
                }
            } else {
                if (ctx->on_error) {
                    ctx->on_error("Checksum error", ctx->callback_user_data);
                }
            }
            xbee_parser_reset(ctx);
            break;
    }
}

void xbee_process(XBeeContext_t* ctx) {
    uint8_t byte;
    while (UART_ReadByte(ctx->uart, &byte) == 0) {
        xbee_process_byte(ctx, byte);
    }
}

/* ============================================================================
 * API Functions - Frame Building & Transmission
 * ============================================================================ */

uint8_t xbee_get_next_frame_id(XBeeContext_t* ctx) {
    uint8_t id = ctx->frame_id_counter++;
    if (ctx->frame_id_counter == 0) {
        ctx->frame_id_counter = 1;  /* Skip 0, which means no response */
    }
    return id;
}

/**
 * @brief Send a byte with escaping if needed (API Mode 2)
 */
static int send_byte_escaped(UART_Context* uart, uint8_t byte, uint8_t* checksum) {
    if (checksum) {
        *checksum += byte;
    }
    
    if (xbee_needs_escape(byte)) {
        if (UART_SendByte(uart, XBEE_ESCAPE_CHAR) != 0) return -1;
        if (UART_SendByte(uart, byte ^ XBEE_ESCAPE_XOR) != 0) return -1;
    } else {
        if (UART_SendByte(uart, byte) != 0) return -1;
    }
    return 0;
}

/**
 * @brief Send frame length with escaping
 */
static int send_length_escaped(UART_Context* uart, uint16_t length) {
    uint8_t msb = (uint8_t)(length >> 8);
    uint8_t lsb = (uint8_t)(length & 0xFF);
    
    if (send_byte_escaped(uart, msb, NULL) != 0) return -1;
    if (send_byte_escaped(uart, lsb, NULL) != 0) return -1;
    return 0;
}

int xbee_send_tx_request(XBeeContext_t* ctx,
                         uint64_t dest_addr64,
                         uint16_t dest_addr16,
                         const uint8_t* rf_data,
                         uint16_t rf_data_len,
                         uint8_t frame_id) {
    uint32_t frame_length;

    if (ctx == NULL || ctx->uart == NULL) {
        return -1;
    }
    if (rf_data_len > 0u && rf_data == NULL) {
        return -1;
    }

    frame_length = 14u + (uint32_t)rf_data_len;
    if (frame_length > XBEE_MAX_FRAME_LEN) {
        return -1;
    }

    UART_Context* uart = ctx->uart;
    
    /* Frame structure:
     * [FrameType: 1] [FrameID: 1] [Dest64: 8] [Dest16: 2] [BroadcastRadius: 1] [Options: 1] [RF Data: N]
     * Total length = 14 + rf_data_len
     */
    uint8_t checksum = 0;
    
    /* Send start delimiter (not escaped) */
    if (UART_SendByte(uart, XBEE_START_DELIMITER) != 0) return -1;
    
    /* Send length (escaped) */
    if (send_length_escaped(uart, (uint16_t)frame_length) != 0) return -1;
    
    /* Frame type */
    if (send_byte_escaped(uart, XBEE_FRAME_TX_REQUEST, &checksum) != 0) return -1;
    
    /* Frame ID */
    if (send_byte_escaped(uart, frame_id, &checksum) != 0) return -1;
    
    /* Destination 64-bit address */
    for (int i = 7; i >= 0; i--) {
        uint8_t byte = (uint8_t)(dest_addr64 >> (i * 8));
        if (send_byte_escaped(uart, byte, &checksum) != 0) return -1;
    }
    
    /* Destination 16-bit address */
    if (send_byte_escaped(uart, (uint8_t)(dest_addr16 >> 8), &checksum) != 0) return -1;
    if (send_byte_escaped(uart, (uint8_t)(dest_addr16 & 0xFF), &checksum) != 0) return -1;
    
    /* Broadcast radius (0 = max hops) */
    if (send_byte_escaped(uart, 0x00, &checksum) != 0) return -1;
    
    /* Options (0 = default) */
    if (send_byte_escaped(uart, 0x00, &checksum) != 0) return -1;
    
    /* RF Data */
    for (uint16_t i = 0; i < rf_data_len; i++) {
        if (send_byte_escaped(uart, rf_data[i], &checksum) != 0) return -1;
    }
    
    /* Checksum */
    uint8_t final_checksum = 0xFF - checksum;
    if (send_byte_escaped(uart, final_checksum, NULL) != 0) return -1;
    
    return 0;
}

int xbee_send_data_no_wait(XBeeContext_t* ctx,
                           uint64_t dest_addr64,
                           const uint8_t* rf_data,
                           uint16_t rf_data_len) {
    return xbee_send_tx_request(ctx, dest_addr64, XBEE_ADDR16_UNKNOWN, 
                                rf_data, rf_data_len, 0);
}

int xbee_send_at_command(XBeeContext_t* ctx,
                         const char* command,
                         const uint8_t* parameter,
                         uint16_t param_len,
                         uint8_t frame_id) {
    uint32_t frame_length;

    if (ctx == NULL || ctx->uart == NULL) {
        return -1;
    }
    if (command == NULL || command[0] == '\0' || command[1] == '\0') {
        return -1;
    }
    if (param_len > 0u && parameter == NULL) {
        return -1;
    }

    frame_length = 4u + (uint32_t)param_len;
    if (frame_length > XBEE_MAX_FRAME_LEN) {
        return -1;
    }

    UART_Context* uart = ctx->uart;
    
    /* Frame structure:
     * [FrameType: 1] [FrameID: 1] [Command: 2] [Parameter: N]
     * Total length = 4 + param_len
     */
    uint8_t checksum = 0;
    
    /* Send start delimiter */
    if (UART_SendByte(uart, XBEE_START_DELIMITER) != 0) return -1;
    
    /* Send length */
    if (send_length_escaped(uart, (uint16_t)frame_length) != 0) return -1;
    
    /* Frame type */
    if (send_byte_escaped(uart, XBEE_FRAME_AT_COMMAND, &checksum) != 0) return -1;
    
    /* Frame ID */
    if (send_byte_escaped(uart, frame_id, &checksum) != 0) return -1;
    
    /* AT Command (2 bytes) */
    if (send_byte_escaped(uart, (uint8_t)command[0], &checksum) != 0) return -1;
    if (send_byte_escaped(uart, (uint8_t)command[1], &checksum) != 0) return -1;
    
    /* Parameter */
    for (uint16_t i = 0; i < param_len; i++) {
        if (send_byte_escaped(uart, parameter[i], &checksum) != 0) return -1;
    }
    
    /* Checksum */
    uint8_t final_checksum = 0xFF - checksum;
    if (send_byte_escaped(uart, final_checksum, NULL) != 0) return -1;
    
    return 0;
}
