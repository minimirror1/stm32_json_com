/*
 * json_com.c
 *
 *  Created on: Dec 2, 2025
 *      Author: AI Assistant
 *
 *  JSON Communication Library with XBee Fragment Protocol support
 *  - Handles all JSON parsing and response formatting
 *  - Uses XBee API Mode 2 for RF communication
 *  - Fragment Protocol for reliable large message transfer
 *  - Application operations delegated to App_* functions (device_hal.h)
 */

#include "json_com.h"
#include "uart_queue.h"
#include "cJSON.h"
#include "device_hal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void HandleProcessPacket(JSON_Context *ctx, const char *json_str);
static void OnXBeeFrame(const XBeeFrame_t* frame, void* user_data);
static void OnXBeeError(const char* error, void* user_data);
static void OnFragRxMessage(const uint8_t* data, uint32_t len, uint64_t source_addr, void* user_data);
static void OnFragRxLog(const char* message, void* user_data);
static void OnFragTxComplete(uint16_t msg_id, bool success, void* user_data);
static void OnFragTxLog(const char* message, void* user_data);

/* ============================================================================
 * Initialization
 * ============================================================================ */

void JSON_COM_Init(JSON_Context *ctx, UART_Context *uart, uint8_t my_id) {
    memset(ctx, 0, sizeof(JSON_Context));
    ctx->uart = uart;
    ctx->my_id = my_id;
    ctx->rx_index = 0;
    
    /* Initialize XBee context */
    xbee_init(&ctx->xbee, uart);
    xbee_set_callbacks(&ctx->xbee, OnXBeeFrame, OnXBeeError, ctx);
    
    /* Initialize Fragment RX */
    frag_rx_init(&ctx->frag_rx, &ctx->xbee);
    frag_rx_set_callbacks(&ctx->frag_rx, OnFragRxMessage, OnFragRxLog, ctx);
    
    /* Initialize Fragment TX */
    frag_tx_init(&ctx->frag_tx, &ctx->xbee);
    frag_tx_set_callbacks(&ctx->frag_tx, OnFragTxComplete, OnFragTxLog, ctx);
}

/* ============================================================================
 * Response Sending via Fragment Protocol
 * ============================================================================ */

static void SendResponse(JSON_Context *ctx, cJSON *response_json) {
    char *str = cJSON_PrintUnformatted(response_json);
    if (str) {
        /* Copy to TX buffer and send via Fragment Protocol */
        uint32_t len = strlen(str);
        if (len < JSON_TX_BUFFER_SIZE) {
            memcpy(ctx->tx_buffer, str, len);
            ctx->tx_buffer_len = len;
            
            /* Send via Fragment TX to the source address */
            frag_tx_send(&ctx->frag_tx, ctx->tx_buffer, len, ctx->current_source_addr);
        }
        free(str);
    }
    cJSON_Delete(response_json);
}

static bool TryGetU8(cJSON *item, uint8_t *out) {
    if (!cJSON_IsNumber(item)) return false;
    int v = (int)item->valuedouble;
    if (v < 0 || v > 255) return false;
    *out = (uint8_t)v;
    return true;
}

static bool TryGetI32(cJSON *item, int32_t *out) {
    double value;

    if (!cJSON_IsNumber(item) || out == NULL) {
        return false;
    }

    value = item->valuedouble;
    if (value < (double)INT32_MIN || value > (double)INT32_MAX) {
        return false;
    }

    *out = (int32_t)value;
    return true;
}

static void SendError(JSON_Context *ctx, uint8_t req_src_id, const char *resp_cmd, const char *msg, bool respond) {
    if (!respond) return;
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "msg", "resp");
    cJSON_AddNumberToObject(root, "src_id", ctx->my_id);
    cJSON_AddNumberToObject(root, "tar_id", req_src_id);
    cJSON_AddStringToObject(root, "cmd", resp_cmd ? resp_cmd : "error");
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "message", msg);
    SendResponse(ctx, root);
}

static void SendSuccess(JSON_Context *ctx, uint8_t req_src_id, const char *resp_cmd, cJSON *payload, bool respond) {
    if (!respond) {
        if (payload) cJSON_Delete(payload);
        return;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        if (payload) cJSON_Delete(payload);
        return;
    }
    cJSON_AddStringToObject(root, "msg", "resp");
    cJSON_AddNumberToObject(root, "src_id", ctx->my_id);
    cJSON_AddNumberToObject(root, "tar_id", req_src_id);
    cJSON_AddStringToObject(root, "cmd", resp_cmd ? resp_cmd : "ok");
    cJSON_AddStringToObject(root, "status", "ok");
    if (payload) {
        cJSON_AddItemToObject(root, "payload", payload);
    }
    SendResponse(ctx, root);
}

/* ============================================================================
 * Command Handlers
 * All handlers call App_* functions and build responses in this layer
 * ============================================================================ */

static void HandlePing(JSON_Context *ctx, uint8_t req_src_id, bool respond) {
    bool success = App_Ping();
    
    if (!success) {
        SendError(ctx, req_src_id, "pong", "Not implemented", respond);
        return;
    }
    
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return;
    cJSON_AddStringToObject(payload, "message", "pong");
    SendSuccess(ctx, req_src_id, "pong", payload, respond);
}

static void HandleMove(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *motor_id_item;
    cJSON *pos_item;
    uint8_t motor_id = 0;
    int32_t raw_pos = 0;
    bool success;

    if (!cJSON_IsObject(req_payload)) {
        SendError(ctx, req_src_id, "move", "Missing or invalid payload", respond);
        return;
    }

    motor_id_item = cJSON_GetObjectItem(req_payload, "motorId");
    pos_item = cJSON_GetObjectItem(req_payload, "pos");

    if (!TryGetU8(motor_id_item, &motor_id)) {
        SendError(ctx, req_src_id, "move", "Missing or invalid motorId", respond);
        return;
    }

    if (!TryGetI32(pos_item, &raw_pos)) {
        SendError(ctx, req_src_id, "move", "Missing or invalid pos", respond);
        return;
    }

    success = App_Move(motor_id, raw_pos);
    
    if (!success) {
        SendError(ctx, req_src_id, "move", "Not implemented", respond);
        return;
    }
    
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return;
    cJSON_AddStringToObject(payload, "status", "moved");
    cJSON_AddNumberToObject(payload, "motorId", motor_id);
    cJSON_AddNumberToObject(payload, "pos", raw_pos);
    SendSuccess(ctx, req_src_id, "move", payload, respond);
}

static void HandleMotionCtrl(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *action_item = cJSON_GetObjectItem(req_payload, "action");
    const char *action = action_item ? action_item->valuestring : NULL;
    
    if (action == NULL) {
        SendError(ctx, req_src_id, "motion_ctrl", "Missing action", respond);
        return;
    }
    
    bool success = false;
    if (strcmp(action, "play") == 0) {
        success = App_MotionPlay(ctx->my_id);
    } else if (strcmp(action, "stop") == 0) {
        success = App_MotionStop(ctx->my_id);
    } else if (strcmp(action, "pause") == 0) {
        success = App_MotionPause(ctx->my_id);
    } else {
        SendError(ctx, req_src_id, "motion_ctrl", "Unknown action", respond);
        return;
    }
    
    if (!success) {
        SendError(ctx, req_src_id, "motion_ctrl", "Not implemented", respond);
        return;
    }
    
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return;
    cJSON_AddStringToObject(payload, "status", "executed");
    cJSON_AddStringToObject(payload, "action", action);
    cJSON_AddNumberToObject(payload, "deviceId", ctx->my_id);
    SendSuccess(ctx, req_src_id, "motion_ctrl", payload, respond);
}

static void HandleGetFile(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *path_item = cJSON_GetObjectItem(req_payload, "path");
    const char *path = path_item ? path_item->valuestring : "";
    
    static char content_buffer[APP_CONTENT_MAX_LEN];
    
    bool success = App_GetFile(path, content_buffer, APP_CONTENT_MAX_LEN);
    
    if (!success) {
        SendError(ctx, req_src_id, "get_file", "Not implemented or file not found", respond);
        return;
    }
    
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return;
    cJSON_AddStringToObject(payload, "path", path);
    cJSON_AddStringToObject(payload, "content", content_buffer);
    SendSuccess(ctx, req_src_id, "get_file", payload, respond);
}

static void HandleSaveFile(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *path_item = cJSON_GetObjectItem(req_payload, "path");
    cJSON *content_item = cJSON_GetObjectItem(req_payload, "content");
    const char *path = path_item ? path_item->valuestring : "";
    const char *content = content_item ? content_item->valuestring : "";
    
    bool success = App_SaveFile(path, content);
    
    if (!success) {
        SendError(ctx, req_src_id, "save_file", "Not implemented or save failed", respond);
        return;
    }
    
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return;
    cJSON_AddStringToObject(payload, "status", "saved");
    cJSON_AddStringToObject(payload, "path", path);
    SendSuccess(ctx, req_src_id, "save_file", payload, respond);
}

static void HandleVerifyFile(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *path_item = cJSON_GetObjectItem(req_payload, "path");
    cJSON *content_item = cJSON_GetObjectItem(req_payload, "content");
    const char *path = path_item ? path_item->valuestring : "";
    const char *content = content_item ? content_item->valuestring : "";
    
    bool match = false;
    bool success = App_VerifyFile(path, content, &match);
    
    if (!success) {
        SendError(ctx, req_src_id, "verify_file", "Not implemented or file not found", respond);
        return;
    }
    
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return;
    cJSON_AddBoolToObject(payload, "match", match);
    SendSuccess(ctx, req_src_id, "verify_file", payload, respond);
}

static void HandleGetFiles(JSON_Context *ctx, uint8_t req_src_id, bool respond) {
    if (!respond) return;
    
    static AppFileInfo files[APP_MAX_FILES];
    
    int count = App_GetFiles(files, APP_MAX_FILES);
    
    if (count < 0) {
        SendError(ctx, req_src_id, "get_files", "Not implemented", respond);
        return;
    }
    
    /* Build JSON response using cJSON (since we now use Fragment Protocol for large messages) */
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    
    cJSON_AddStringToObject(root, "msg", "resp");
    cJSON_AddNumberToObject(root, "src_id", ctx->my_id);
    cJSON_AddNumberToObject(root, "tar_id", req_src_id);
    cJSON_AddStringToObject(root, "cmd", "get_files");
    cJSON_AddStringToObject(root, "status", "ok");
    
    cJSON *files_arr = cJSON_CreateArray();
    if (!files_arr) {
        cJSON_Delete(root);
        return;
    }
    
    for (int i = 0; i < count; i++) {
        cJSON *file_obj = cJSON_CreateObject();
        if (!file_obj) continue;
        
        cJSON_AddStringToObject(file_obj, "name", files[i].name);
        cJSON_AddStringToObject(file_obj, "path", files[i].path);
        cJSON_AddBoolToObject(file_obj, "isDirectory", files[i].is_directory);
        cJSON_AddNumberToObject(file_obj, "size", files[i].size);
        cJSON_AddNumberToObject(file_obj, "depth", files[i].depth);
        cJSON_AddNumberToObject(file_obj, "parentIndex", files[i].parent_index);
        
        cJSON_AddItemToArray(files_arr, file_obj);
    }
    
    cJSON_AddItemToObject(root, "payload", files_arr);
    SendResponse(ctx, root);
}

static void HandleGetMotors(JSON_Context *ctx, uint8_t req_src_id, bool respond) {
    if (!respond) return;
    
    static AppMotorInfo motors[APP_MAX_MOTORS];
    
    int count = App_GetMotors(motors, APP_MAX_MOTORS);
    
    if (count < 0) {
        SendError(ctx, req_src_id, "get_motors", "Not implemented", respond);
        return;
    }
    
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return;
    
    cJSON *motors_arr = cJSON_CreateArray();
    if (!motors_arr) {
        cJSON_Delete(payload);
        return;
    }
    
    for (int i = 0; i < count; i++) {
        cJSON *motor = cJSON_CreateObject();
        if (!motor) continue;
        
        cJSON_AddNumberToObject(motor, "id", motors[i].id);
        cJSON_AddNumberToObject(motor, "groupId", motors[i].group_id);
        cJSON_AddNumberToObject(motor, "subId", motors[i].sub_id);
        cJSON_AddStringToObject(motor, "type", motors[i].type);
        cJSON_AddStringToObject(motor, "status", motors[i].status);
        cJSON_AddNumberToObject(motor, "position", motors[i].position);
        cJSON_AddNumberToObject(motor, "velocity", motors[i].velocity);
        cJSON_AddNumberToObject(motor, "minAngle", motors[i].min_angle);
        cJSON_AddNumberToObject(motor, "maxAngle", motors[i].max_angle);
        cJSON_AddNumberToObject(motor, "minRaw", motors[i].min_raw);
        cJSON_AddNumberToObject(motor, "maxRaw", motors[i].max_raw);
        
        cJSON_AddItemToArray(motors_arr, motor);
    }
    
    cJSON_AddItemToObject(payload, "motors", motors_arr);
    SendSuccess(ctx, req_src_id, "get_motors", payload, respond);
}

static void HandleGetMotorState(JSON_Context *ctx, uint8_t req_src_id, bool respond) {
    if (!respond) return;
    
    static AppMotorState states[APP_MAX_MOTORS];
    
    int count = App_GetMotorState(states, APP_MAX_MOTORS);
    
    if (count < 0) {
        SendError(ctx, req_src_id, "get_motor_state", "Not implemented", respond);
        return;
    }
    
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return;
    
    cJSON *motors_arr = cJSON_CreateArray();
    if (!motors_arr) {
        cJSON_Delete(payload);
        return;
    }
    
    for (int i = 0; i < count; i++) {
        cJSON *motor = cJSON_CreateObject();
        if (!motor) continue;
        
        cJSON_AddNumberToObject(motor, "id", states[i].id);
        cJSON_AddNumberToObject(motor, "position", states[i].position);
        cJSON_AddNumberToObject(motor, "velocity", states[i].velocity);
        cJSON_AddStringToObject(motor, "status", states[i].status);
        
        cJSON_AddItemToArray(motors_arr, motor);
    }
    
    cJSON_AddItemToObject(payload, "motors", motors_arr);
    SendSuccess(ctx, req_src_id, "get_motor_state", payload, respond);
}

/* ============================================================================
 * Packet Processing
 * ============================================================================ */

static void HandleProcessPacket(JSON_Context *ctx, const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        return;
    }

    cJSON *msg_item = cJSON_GetObjectItem(root, "msg");
    cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
    cJSON *src_id_item = cJSON_GetObjectItem(root, "src_id");
    cJSON *tar_id_item = cJSON_GetObjectItem(root, "tar_id");
    cJSON *payload_item = cJSON_GetObjectItem(root, "payload");

    uint8_t tar_id = 0;
    if (!TryGetU8(tar_id_item, &tar_id)) {
        cJSON_Delete(root);
        return;
    }

    if (tar_id != ctx->my_id && tar_id != RS485_BROADCAST_ID) {
        cJSON_Delete(root);
        return;
    }

    bool respond = (tar_id == ctx->my_id);

    uint8_t src_id = 0;
    if (!TryGetU8(src_id_item, &src_id)) {
        cJSON_Delete(root);
        return;
    }

    const char *msg_type = NULL;
    bool legacy_no_msg = (msg_item == NULL);
    if (!legacy_no_msg) {
        if (!cJSON_IsString(msg_item) || msg_item->valuestring == NULL) {
            SendError(ctx, src_id, "error", "Missing or invalid msg", respond);
            cJSON_Delete(root);
            return;
        }
        msg_type = msg_item->valuestring;
    } else {
        msg_type = "req";
    }

    if (strcmp(msg_type, "req") != 0) {
        cJSON_Delete(root);
        return;
    }

    if (cJSON_GetObjectItem(root, "status") != NULL) {
        SendError(ctx, src_id, "error", "status not allowed in req", respond);
        cJSON_Delete(root);
        return;
    }

    if (!cJSON_IsString(cmd_item)) {
        SendError(ctx, src_id, "error", "Missing or invalid cmd", respond);
        cJSON_Delete(root);
        return;
    }

    char *cmd = cmd_item->valuestring;

    if (strcmp(cmd, "ping") == 0) HandlePing(ctx, src_id, respond);
    else if (strcmp(cmd, "move") == 0) HandleMove(ctx, src_id, payload_item, respond);
    else if (strcmp(cmd, "motion_ctrl") == 0) HandleMotionCtrl(ctx, src_id, payload_item, respond);
    else if (strcmp(cmd, "get_files") == 0) HandleGetFiles(ctx, src_id, respond);
    else if (strcmp(cmd, "get_file") == 0) HandleGetFile(ctx, src_id, payload_item, respond);
    else if (strcmp(cmd, "save_file") == 0) HandleSaveFile(ctx, src_id, payload_item, respond);
    else if (strcmp(cmd, "verify_file") == 0) HandleVerifyFile(ctx, src_id, payload_item, respond);
    else if (strcmp(cmd, "get_motors") == 0) HandleGetMotors(ctx, src_id, respond);
    else if (strcmp(cmd, "get_motor_state") == 0) HandleGetMotorState(ctx, src_id, respond);
    else {
        SendError(ctx, src_id, "error", "Unknown command", respond);
    }

    cJSON_Delete(root);
}

/* ============================================================================
 * XBee Callbacks
 * ============================================================================ */

static void OnXBeeFrame(const XBeeFrame_t* frame, void* user_data) {
    JSON_Context* ctx = (JSON_Context*)user_data;
    
    /* Handle RX Packet (0x90) and Explicit RX (0x91) */
    if (frame->frame_type == XBEE_FRAME_RX_PACKET) {
        const XBeeRxPacket_t* rx = &frame->parsed.rx_packet;
        
        if (rx->rf_data && rx->rf_data_len > 0) {
            /* Check if this is a NACK or DONE for TX */
            if (frag_rx_is_nack(rx->rf_data, rx->rf_data_len)) {
                NackMessage_t nack;
                if (frag_rx_parse_nack(rx->rf_data, rx->rf_data_len, &nack)) {
                    frag_tx_handle_nack(&ctx->frag_tx, &nack, rx->source_addr64);
                }
            } else if (frag_rx_is_done(rx->rf_data, rx->rf_data_len)) {
                uint16_t msg_id;
                if (frag_rx_parse_done(rx->rf_data, rx->rf_data_len, &msg_id)) {
                    frag_tx_handle_done(&ctx->frag_tx, msg_id);
                }
            } else {
                /* Process as Fragment Protocol data */
                frag_rx_process(&ctx->frag_rx, rx->rf_data, rx->rf_data_len, rx->source_addr64);
            }
        }
    } else if (frame->frame_type == XBEE_FRAME_EXPLICIT_RX) {
        const XBeeExplicitRx_t* rx = &frame->parsed.explicit_rx;
        
        if (rx->rf_data && rx->rf_data_len > 0) {
            /* Same handling as RX Packet */
            if (frag_rx_is_nack(rx->rf_data, rx->rf_data_len)) {
                NackMessage_t nack;
                if (frag_rx_parse_nack(rx->rf_data, rx->rf_data_len, &nack)) {
                    frag_tx_handle_nack(&ctx->frag_tx, &nack, rx->source_addr64);
                }
            } else if (frag_rx_is_done(rx->rf_data, rx->rf_data_len)) {
                uint16_t msg_id;
                if (frag_rx_parse_done(rx->rf_data, rx->rf_data_len, &msg_id)) {
                    frag_tx_handle_done(&ctx->frag_tx, msg_id);
                }
            } else {
                frag_rx_process(&ctx->frag_rx, rx->rf_data, rx->rf_data_len, rx->source_addr64);
            }
        }
    }
    /* TX Status (0x8B) could be handled here for retry logic if needed */
}

static void OnXBeeError(const char* error, void* user_data) {
    (void)user_data;
    (void)error;
    /* Could log errors here if needed */
}

/* ============================================================================
 * Fragment RX Callbacks
 * ============================================================================ */

static void OnFragRxMessage(const uint8_t* data, uint32_t len, uint64_t source_addr, void* user_data) {
    JSON_Context* ctx = (JSON_Context*)user_data;
    
    /* Store source address for response */
    ctx->current_source_addr = source_addr;
    
    /* Ensure null-termination and process as JSON */
    if (len < MAX_JSON_LEN) {
        memcpy(ctx->rx_line_buffer, data, len);
        ctx->rx_line_buffer[len] = '\0';
        HandleProcessPacket(ctx, ctx->rx_line_buffer);
    }
}

static void OnFragRxLog(const char* message, void* user_data) {
    (void)user_data;
    (void)message;
    /* Could log messages here if needed */
}

/* ============================================================================
 * Fragment TX Callbacks
 * ============================================================================ */

static void OnFragTxComplete(uint16_t msg_id, bool success, void* user_data) {
    (void)user_data;
    (void)msg_id;
    (void)success;
    /* Could track completion status here if needed */
}

static void OnFragTxLog(const char* message, void* user_data) {
    (void)user_data;
    (void)message;
    /* Could log messages here if needed */
}

/* ============================================================================
 * Main Processing Functions
 * ============================================================================ */

void JSON_COM_Process(JSON_Context *ctx) {
    /* Process incoming XBee frames from UART */
    xbee_process(&ctx->xbee);
    
    /* Run TX state machine (sends pending fragments) */
    frag_tx_tick(&ctx->frag_tx);
}

void JSON_COM_Tick(JSON_Context *ctx) {
    /* Periodic timeout handling for Fragment Protocol */
    frag_rx_tick(&ctx->frag_rx);
    frag_tx_tick(&ctx->frag_tx);
}

void JSON_COM_SetDestAddress(JSON_Context *ctx, uint64_t addr64) {
    ctx->current_source_addr = addr64;
}

uint16_t JSON_COM_SendString(JSON_Context *ctx, const char *json_str, uint64_t dest_addr64) {
    uint32_t len = strlen(json_str);
    if (len >= JSON_TX_BUFFER_SIZE) {
        return 0;
    }
    
    memcpy(ctx->tx_buffer, json_str, len);
    ctx->tx_buffer_len = len;
    
    return frag_tx_send(&ctx->frag_tx, ctx->tx_buffer, len, dest_addr64);
}

bool JSON_COM_IsTxBusy(JSON_Context *ctx) {
    return frag_tx_is_busy(&ctx->frag_tx);
}
