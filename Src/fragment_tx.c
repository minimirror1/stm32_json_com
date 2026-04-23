/*
 * fragment_tx.c
 *
 *  Created on: Jan 12, 2026
 *      Author: AI Assistant
 *
 *  Fragment Transmitter - handles message fragmentation and transmission with NACK-based retransmission
 *  Based on xBee_test C# FragmentTransmitter implementation
 */

#include "fragment_tx.h"
#include "crc16.h"
#include "main.h"
#include <string.h>

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @brief Find session by message ID
 */
static TxSessionEx_t* find_session(FragTxContext_t* ctx, uint16_t msg_id) {
    for (int i = 0; i < FRAG_MAX_TX_SESSIONS; i++) {
        if (ctx->sessions[i].base.active && ctx->sessions[i].base.msg_id == msg_id) {
            return &ctx->sessions[i];
        }
    }
    return NULL;
}

/**
 * @brief Find free session slot
 */
static TxSessionEx_t* find_free_slot(FragTxContext_t* ctx) {
    for (int i = 0; i < FRAG_MAX_TX_SESSIONS; i++) {
        if (!ctx->sessions[i].base.active) {
            return &ctx->sessions[i];
        }
    }
    return NULL;
}

/**
 * @brief Calculate send delay based on fragment count
 */
static uint16_t calculate_send_delay(uint16_t frag_cnt) {
    if (frag_cnt <= 10) {
        return 10;   /* Small messages: 10ms */
    } else if (frag_cnt <= 30) {
        return 15;   /* Medium messages: 15ms */
    } else if (frag_cnt <= 50) {
        return 20;   /* Large messages: 20ms */
    } else {
        return 30;   /* Very large messages: 30ms */
    }
}

/**
 * @brief Build and send a single fragment
 */
static int send_fragment(FragTxContext_t* ctx, TxSessionEx_t* session, uint16_t frag_idx) {
    TxSession_t* base = &session->base;
    
    /* Calculate payload for this fragment */
    uint32_t offset = (uint32_t)frag_idx * ctx->payload_size;
    uint16_t remaining = base->data_len - offset;
    uint8_t payload_len = (remaining > ctx->payload_size) ? ctx->payload_size : (uint8_t)remaining;
    
    /* Build fragment: header + payload + CRC */
    uint8_t frag_buf[FRAG_HEADER_SIZE + FRAG_MAX_PAYLOAD + FRAG_CRC_SIZE];
    
    FragmentHeader_t header;
    header.version = FRAG_VERSION;
    header.type = FRAG_TYPE_DATA;
    header.msg_id = base->msg_id;
    header.total_len = base->data_len;
    header.frag_idx = frag_idx;
    header.frag_cnt = base->frag_cnt;
    header.payload_len = payload_len;
    
    frag_header_write(&header, frag_buf);
    
    /* Copy payload */
    if (payload_len > 0) {
        memcpy(&frag_buf[FRAG_HEADER_SIZE], &base->data[offset], payload_len);
    }
    
    /* Append CRC */
    uint16_t total_len = FRAG_HEADER_SIZE + payload_len;
    crc16_append(frag_buf, total_len);
    total_len += FRAG_CRC_SIZE;
    
    /* Send via XBee */
    int result = xbee_send_data_no_wait(ctx->xbee, base->dest_address, frag_buf, total_len);
    
    if (result == 0) {
        ctx->total_fragments_sent++;
    }
    
    return result;
}

/**
 * @brief Complete session with result
 */
static void complete_session(FragTxContext_t* ctx, TxSessionEx_t* session, bool success) {
    if (success) {
        session->state = FRAG_TX_STATE_COMPLETE;
        ctx->messages_completed++;
    } else {
        session->state = FRAG_TX_STATE_FAILED;
        ctx->messages_failed++;
    }
    
    /* Notify callback */
    if (ctx->on_complete) {
        ctx->on_complete(session->base.msg_id, success, ctx->callback_user_data);
    }
    
    /* Mark session as inactive */
    session->base.active = 0;
    
    if (ctx->on_log) {
        ctx->on_log(success ? "TX complete" : "TX failed", ctx->callback_user_data);
    }
}

/* ============================================================================
 * API Functions
 * ============================================================================ */

void frag_tx_init(FragTxContext_t* ctx, XBeeContext_t* xbee) {
    memset(ctx, 0, sizeof(FragTxContext_t));
    ctx->xbee = xbee;
    ctx->next_msg_id = 1;
    ctx->payload_size = FRAG_MAX_PAYLOAD;
}

void frag_tx_set_callbacks(FragTxContext_t* ctx,
                           FragTxCompleteCallback_t on_complete,
                           FragTxLogCallback_t on_log,
                           void* user_data) {
    ctx->on_complete = on_complete;
    ctx->on_log = on_log;
    ctx->callback_user_data = user_data;
}

void frag_tx_set_payload_size(FragTxContext_t* ctx, uint8_t size) {
    if (size < 10) size = 10;
    if (size > 34) size = 34;
    ctx->payload_size = size;
}

uint16_t frag_tx_get_next_msg_id(FragTxContext_t* ctx) {
    uint16_t id = ctx->next_msg_id++;
    if (ctx->next_msg_id == 0) {
        ctx->next_msg_id = 1;
    }
    return id;
}

uint16_t frag_tx_send(FragTxContext_t* ctx, const uint8_t* data, uint32_t len, uint64_t dest_addr64) {
    if (data == NULL || len == 0 || len > FRAG_MAX_MESSAGE_SIZE) {
        if (ctx->on_log) {
            ctx->on_log("Invalid message size", ctx->callback_user_data);
        }
        return 0;
    }
    
    /* Find free slot */
    TxSessionEx_t* session = find_free_slot(ctx);
    if (!session) {
        if (ctx->on_log) {
            ctx->on_log("No free TX session slots", ctx->callback_user_data);
        }
        return 0;
    }
    
    /* Initialize session */
    memset(session, 0, sizeof(TxSessionEx_t));
    session->base.active = 1;
    session->base.msg_id = frag_tx_get_next_msg_id(ctx);
    memcpy(session->tx_storage, data, len);
    session->base.data = session->tx_storage;
    session->base.data_len = len;
    session->base.frag_cnt = (len + ctx->payload_size - 1) / ctx->payload_size;
    if (session->base.frag_cnt == 0) {
        session->base.frag_cnt = 1;
    }
    session->base.current_frag = 0;
    session->base.dest_address = dest_addr64;
    session->base.start_tick = HAL_GetTick();
    
    session->state = FRAG_TX_STATE_SENDING;
    session->last_send_tick = 0; /* Will send first fragment immediately */
    session->send_delay_ms = calculate_send_delay(session->base.frag_cnt);
    
    if (ctx->on_log) {
        ctx->on_log("TX started", ctx->callback_user_data);
    }
    
    return session->base.msg_id;
}

void frag_tx_tick(FragTxContext_t* ctx) {
    uint32_t now = HAL_GetTick();
    
    for (int i = 0; i < FRAG_MAX_TX_SESSIONS; i++) {
        TxSessionEx_t* session = &ctx->sessions[i];
        if (!session->base.active) {
            continue;
        }
        
        TxSession_t* base = &session->base;
        uint32_t session_age = now - base->start_tick;
        
        /* Check session timeout */
        if (session_age > FRAG_SESSION_TIMEOUT_MS) {
            complete_session(ctx, session, false);
            continue;
        }
        
        switch (session->state) {
            case FRAG_TX_STATE_SENDING:
                /* Send next fragment if delay has passed */
                if (now - session->last_send_tick >= session->send_delay_ms) {
                    if (base->current_frag < base->frag_cnt) {
                        if (send_fragment(ctx, session, base->current_frag) == 0) {
                            base->current_frag++;
                            session->last_send_tick = now;
                        }
                    } else {
                        /* All fragments sent, wait for DONE */
                        session->state = FRAG_TX_STATE_WAITING_DONE;
                        base->waiting_done = 1;
                        session->last_send_tick = now;
                    }
                }
                break;
                
            case FRAG_TX_STATE_WAITING_DONE:
                /* Timeout waiting for DONE is treated as failure. */
                if (now - session->last_send_tick > FRAG_TIMEOUT_MS * 2) {
                    complete_session(ctx, session, false);
                }
                break;
                
            case FRAG_TX_STATE_COMPLETE:
            case FRAG_TX_STATE_FAILED:
                /* Session should be inactive, cleanup */
                base->active = 0;
                break;
                
            case FRAG_TX_STATE_IDLE:
            default:
                break;
        }
    }
}

void frag_tx_handle_nack(FragTxContext_t* ctx, const NackMessage_t* nack, uint64_t source_addr) {
    TxSessionEx_t* session = find_session(ctx, nack->msg_id);
    if (!session) {
        if (ctx->on_log) {
            ctx->on_log("NACK for unknown msg_id", ctx->callback_user_data);
        }
        return;
    }
    
    /* Verify source address matches destination */
    if (session->base.dest_address != source_addr) {
        return;
    }
    
    session->base.nack_rounds++;
    if (session->base.nack_rounds > FRAG_MAX_NACK_ROUNDS) {
        if (ctx->on_log) {
            ctx->on_log("Max NACK rounds exceeded", ctx->callback_user_data);
        }
        complete_session(ctx, session, false);
        return;
    }
    
    /* Retransmit missing fragments */
    for (int i = 0; i < nack->count; i++) {
        uint16_t idx = nack->missing_indices[i];
        if (idx < session->base.frag_cnt) {
            if (send_fragment(ctx, session, idx) == 0) {
                ctx->retransmitted_fragments++;
            }
            
            /* Small delay between retransmits to avoid flooding */
            /* In a state machine, we'd queue these instead */
        }
    }
    
    /* Update state to keep waiting for DONE */
    session->state = FRAG_TX_STATE_WAITING_DONE;
    session->last_send_tick = HAL_GetTick();
    
    if (ctx->on_log) {
        ctx->on_log("NACK handled, fragments retransmitted", ctx->callback_user_data);
    }
}

void frag_tx_handle_done(FragTxContext_t* ctx, uint16_t msg_id, uint64_t source_addr) {
    TxSessionEx_t* session = find_session(ctx, msg_id);
    if (!session) {
        /* Might be a duplicate DONE for already completed session */
        return;
    }

    /* DONE source must match the original destination to avoid spoofed completion. */
    if (session->base.dest_address != source_addr) {
        return;
    }
    
    complete_session(ctx, session, true);
}

bool frag_tx_is_busy(FragTxContext_t* ctx) {
    for (int i = 0; i < FRAG_MAX_TX_SESSIONS; i++) {
        if (ctx->sessions[i].base.active) {
            return true;
        }
    }
    return false;
}

FragTxState_t frag_tx_get_state(FragTxContext_t* ctx, uint16_t msg_id) {
    TxSessionEx_t* session = find_session(ctx, msg_id);
    if (session) {
        return session->state;
    }
    return FRAG_TX_STATE_IDLE;
}

void frag_tx_cancel(FragTxContext_t* ctx, uint16_t msg_id) {
    TxSessionEx_t* session = find_session(ctx, msg_id);
    if (session) {
        session->base.active = 0;
        session->state = FRAG_TX_STATE_IDLE;
    }
}

void frag_tx_clear_sessions(FragTxContext_t* ctx) {
    for (int i = 0; i < FRAG_MAX_TX_SESSIONS; i++) {
        ctx->sessions[i].base.active = 0;
        ctx->sessions[i].state = FRAG_TX_STATE_IDLE;
    }
}

void frag_tx_reset_stats(FragTxContext_t* ctx) {
    ctx->total_fragments_sent = 0;
    ctx->retransmitted_fragments = 0;
    ctx->messages_completed = 0;
    ctx->messages_failed = 0;
}
