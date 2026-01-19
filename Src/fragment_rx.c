/*
 * fragment_rx.c
 *
 *  Created on: Jan 12, 2026
 *      Author: AI Assistant
 *
 *  Fragment Receiver - handles fragment reception, NACK generation, and message reassembly
 *  Based on xBee_test C# FragmentReceiver implementation
 */

#include "fragment_rx.h"
#include "crc16.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @brief Find session by message ID
 * @return Session pointer or NULL if not found
 */
static RxSession_t* find_session(FragRxContext_t* ctx, uint16_t msg_id) {
    for (int i = 0; i < FRAG_MAX_RX_SESSIONS; i++) {
        if (ctx->sessions[i].active && ctx->sessions[i].msg_id == msg_id) {
            return &ctx->sessions[i];
        }
    }
    return NULL;
}

/**
 * @brief Find or create session for message ID
 * @return Session pointer or NULL if no slots available
 */
static RxSession_t* get_or_create_session(FragRxContext_t* ctx, uint16_t msg_id, 
                                           uint32_t total_len, uint16_t frag_cnt,
                                           uint64_t source_addr) {
    /* First, try to find existing session */
    RxSession_t* session = find_session(ctx, msg_id);
    if (session) {
        return session;
    }
    
    /* Find free slot */
    for (int i = 0; i < FRAG_MAX_RX_SESSIONS; i++) {
        if (!ctx->sessions[i].active) {
            session = &ctx->sessions[i];
            memset(session, 0, sizeof(RxSession_t));
            session->active = 1;
            session->msg_id = msg_id;
            session->total_len = total_len;
            session->frag_cnt = frag_cnt;
            session->source_address = source_addr;
            session->start_tick = HAL_GetTick();
            session->last_activity_tick = session->start_tick;
            frag_bitmap_clear(session->received_bitmap);
            return session;
        }
    }
    
    return NULL; /* No free slots */
}

/**
 * @brief Check if all fragments are received
 */
static bool is_complete(RxSession_t* session) {
    for (uint16_t i = 0; i < session->frag_cnt; i++) {
        if (!frag_bitmap_get(session->received_bitmap, i)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Get missing fragment indices
 * @return Number of missing fragments (up to max)
 */
static int get_missing_fragments(RxSession_t* session, uint16_t* missing, int max_missing) {
    int count = 0;
    for (uint16_t i = 0; i < session->frag_cnt && count < max_missing; i++) {
        if (!frag_bitmap_get(session->received_bitmap, i)) {
            missing[count++] = i;
        }
    }
    return count;
}

/**
 * @brief Build and send DONE message
 */
static void send_done(FragRxContext_t* ctx, uint16_t msg_id, uint64_t dest_addr) {
    /* DONE format: ver(1) + type(1) + msg_id(2) + crc(2) = 6 bytes */
    uint8_t done_buf[6];
    
    done_buf[0] = FRAG_VERSION;
    done_buf[1] = FRAG_TYPE_DONE;
    done_buf[2] = (uint8_t)(msg_id >> 8);
    done_buf[3] = (uint8_t)(msg_id & 0xFF);
    crc16_append(done_buf, 4);
    
    xbee_send_data_no_wait(ctx->xbee, dest_addr, done_buf, 6);
}

/**
 * @brief Build and send NACK message
 */
static void send_nack(FragRxContext_t* ctx, RxSession_t* session) {
    uint16_t missing[FRAG_MAX_NACK_INDICES];
    int count = get_missing_fragments(session, missing, FRAG_MAX_NACK_INDICES);
    
    if (count == 0) {
        return;
    }
    
    session->nacks_sent++;
    if (session->nacks_sent > FRAG_MAX_NACK_ROUNDS) {
        if (ctx->on_log) {
            ctx->on_log("Max NACK rounds exceeded, dropping session", ctx->callback_user_data);
        }
        session->active = 0;
        return;
    }
    
    /* NACK format: ver(1) + type(1) + msg_id(2) + count(1) + indices(N*2) + crc(2) */
    uint8_t nack_buf[5 + FRAG_MAX_NACK_INDICES * 2 + 2];
    int len = 0;
    
    nack_buf[len++] = FRAG_VERSION;
    nack_buf[len++] = FRAG_TYPE_NACK;
    nack_buf[len++] = (uint8_t)(session->msg_id >> 8);
    nack_buf[len++] = (uint8_t)(session->msg_id & 0xFF);
    nack_buf[len++] = (uint8_t)count;
    
    for (int i = 0; i < count; i++) {
        nack_buf[len++] = (uint8_t)(missing[i] >> 8);
        nack_buf[len++] = (uint8_t)(missing[i] & 0xFF);
    }
    
    crc16_append(nack_buf, len);
    len += 2;
    
    xbee_send_data_no_wait(ctx->xbee, session->source_address, nack_buf, len);
    ctx->nacks_sent++;
    
    if (ctx->on_log) {
        ctx->on_log("NACK sent", ctx->callback_user_data);
    }
}

/**
 * @brief Complete message reassembly and notify callback
 */
static void complete_message(FragRxContext_t* ctx, RxSession_t* session) {
    ctx->messages_completed++;
    
    /* Send DONE to sender */
    send_done(ctx, session->msg_id, session->source_address);
    
    /* Notify callback */
    if (ctx->on_message) {
        ctx->on_message(session->payload_buffer, session->total_len,
                        session->source_address, ctx->callback_user_data);
    }
    
    /* Cleanup session */
    session->active = 0;
    
    if (ctx->on_log) {
        ctx->on_log("Message completed", ctx->callback_user_data);
    }
}

/**
 * @brief Process DATA fragment
 */
static void process_data_fragment(FragRxContext_t* ctx, const uint8_t* data, 
                                   uint16_t len, uint64_t source_addr) {
    /* Minimum size: header + CRC */
    if (len < FRAG_HEADER_SIZE + FRAG_CRC_SIZE) {
        if (ctx->on_log) {
            ctx->on_log("Fragment too short", ctx->callback_user_data);
        }
        return;
    }
    
    /* Verify CRC */
    if (!crc16_verify(data, len)) {
        ctx->crc_failures++;
        if (ctx->on_log) {
            ctx->on_log("Fragment CRC failure", ctx->callback_user_data);
        }
        return;
    }
    
    ctx->total_fragments_received++;
    
    /* Parse header */
    FragmentHeader_t header;
    frag_header_read(data, &header);
    
    /* Validate header */
    if (header.version != FRAG_VERSION) {
        if (ctx->on_log) {
            ctx->on_log("Unknown protocol version", ctx->callback_user_data);
        }
        return;
    }
    
    if (header.total_len > FRAG_MAX_MESSAGE_SIZE) {
        if (ctx->on_log) {
            ctx->on_log("Message too large", ctx->callback_user_data);
        }
        return;
    }
    
    if (header.frag_idx >= header.frag_cnt) {
        if (ctx->on_log) {
            ctx->on_log("Invalid fragment index", ctx->callback_user_data);
        }
        return;
    }
    
    /* Get or create session */
    RxSession_t* session = get_or_create_session(ctx, header.msg_id, 
                                                  header.total_len, header.frag_cnt,
                                                  source_addr);
    if (!session) {
        if (ctx->on_log) {
            ctx->on_log("No free RX session slots", ctx->callback_user_data);
        }
        return;
    }
    
    /* Check if already received */
    if (frag_bitmap_get(session->received_bitmap, header.frag_idx)) {
        /* Already received, ignore duplicate */
        return;
    }
    
    /* Store fragment payload */
    uint32_t offset = (uint32_t)header.frag_idx * FRAG_MAX_PAYLOAD;
    if (offset + header.payload_len <= FRAG_MAX_MESSAGE_SIZE) {
        memcpy(&session->payload_buffer[offset], &data[FRAG_HEADER_SIZE], header.payload_len);
        frag_bitmap_set(session->received_bitmap, header.frag_idx);
        session->last_activity_tick = HAL_GetTick();
    }
    
    /* Check if complete */
    if (is_complete(session)) {
        complete_message(ctx, session);
    }
    /* Check if this is the last fragment and we have missing pieces */
    else if (header.frag_idx == header.frag_cnt - 1) {
        send_nack(ctx, session);
    }
}

/* ============================================================================
 * API Functions
 * ============================================================================ */

void frag_rx_init(FragRxContext_t* ctx, XBeeContext_t* xbee) {
    memset(ctx, 0, sizeof(FragRxContext_t));
    ctx->xbee = xbee;
}

void frag_rx_set_callbacks(FragRxContext_t* ctx,
                           FragRxMessageCallback_t on_message,
                           FragRxLogCallback_t on_log,
                           void* user_data) {
    ctx->on_message = on_message;
    ctx->on_log = on_log;
    ctx->callback_user_data = user_data;
}

void frag_rx_process(FragRxContext_t* ctx, const uint8_t* data, uint16_t len, uint64_t source_addr) {
    if (len < 2) {
        return;
    }
    
    uint8_t type = data[1];
    
    switch (type) {
        case FRAG_TYPE_DATA:
            process_data_fragment(ctx, data, len, source_addr);
            break;
            
        case FRAG_TYPE_NACK:
        case FRAG_TYPE_DONE:
            /* These are handled by TX side - just return */
            /* The caller should check with frag_rx_is_nack() / frag_rx_is_done() first */
            break;
            
        default:
            if (ctx->on_log) {
                ctx->on_log("Unknown fragment type", ctx->callback_user_data);
            }
            break;
    }
}

void frag_rx_tick(FragRxContext_t* ctx) {
    uint32_t now = HAL_GetTick();
    
    for (int i = 0; i < FRAG_MAX_RX_SESSIONS; i++) {
        RxSession_t* session = &ctx->sessions[i];
        if (!session->active) {
            continue;
        }
        
        uint32_t session_age = now - session->start_tick;
        uint32_t inactive_time = now - session->last_activity_tick;
        
        /* Session expired */
        if (session_age > FRAG_SESSION_TIMEOUT_MS) {
            if (ctx->on_log) {
                ctx->on_log("RX session timeout", ctx->callback_user_data);
            }
            session->active = 0;
            continue;
        }
        
        /* Activity timeout - may need NACK */
        if (inactive_time > FRAG_TIMEOUT_MS && !is_complete(session)) {
            send_nack(ctx, session);
            session->last_activity_tick = now; /* Reset to avoid immediate re-trigger */
        }
    }
}

bool frag_rx_is_nack(const uint8_t* data, uint16_t len) {
    return (len >= 2 && data[1] == FRAG_TYPE_NACK);
}

bool frag_rx_is_done(const uint8_t* data, uint16_t len) {
    return (len >= 2 && data[1] == FRAG_TYPE_DONE);
}

bool frag_rx_parse_nack(const uint8_t* data, uint16_t len, NackMessage_t* nack) {
    /* Minimum: ver(1) + type(1) + msg_id(2) + count(1) + crc(2) = 7 */
    if (len < 7) {
        return false;
    }
    
    if (!crc16_verify(data, len)) {
        return false;
    }
    
    if (data[0] != FRAG_VERSION || data[1] != FRAG_TYPE_NACK) {
        return false;
    }
    
    nack->msg_id = (uint16_t)((data[2] << 8) | data[3]);
    nack->count = data[4];
    
    if (nack->count > FRAG_MAX_NACK_INDICES) {
        nack->count = FRAG_MAX_NACK_INDICES;
    }
    
    /* Check if we have enough data for all indices */
    if (len < 5 + nack->count * 2 + 2) {
        return false;
    }
    
    int offset = 5;
    for (int i = 0; i < nack->count; i++) {
        nack->missing_indices[i] = (uint16_t)((data[offset] << 8) | data[offset + 1]);
        offset += 2;
    }
    
    return true;
}

bool frag_rx_parse_done(const uint8_t* data, uint16_t len, uint16_t* msg_id) {
    /* DONE: ver(1) + type(1) + msg_id(2) + crc(2) = 6 */
    if (len != 6) {
        return false;
    }
    
    if (!crc16_verify(data, len)) {
        return false;
    }
    
    if (data[0] != FRAG_VERSION || data[1] != FRAG_TYPE_DONE) {
        return false;
    }
    
    *msg_id = (uint16_t)((data[2] << 8) | data[3]);
    return true;
}

void frag_rx_clear_sessions(FragRxContext_t* ctx) {
    for (int i = 0; i < FRAG_MAX_RX_SESSIONS; i++) {
        ctx->sessions[i].active = 0;
    }
}

void frag_rx_reset_stats(FragRxContext_t* ctx) {
    ctx->total_fragments_received = 0;
    ctx->crc_failures = 0;
    ctx->nacks_sent = 0;
    ctx->messages_completed = 0;
}

int frag_rx_get_active_sessions(FragRxContext_t* ctx) {
    int count = 0;
    for (int i = 0; i < FRAG_MAX_RX_SESSIONS; i++) {
        if (ctx->sessions[i].active) {
            count++;
        }
    }
    return count;
}
