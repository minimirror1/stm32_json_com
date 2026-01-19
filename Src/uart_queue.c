/*
 * uart_queue.c
 *
 *  Created on: Dec 1, 2025
 *      Author: AI Assistant
 */

#include "uart_queue.h"
#include <string.h>

#define MAX_UARTS 4
static UART_Context *uart_registry[MAX_UARTS] = {NULL};

static void RegisterContext(UART_Context *ctx) {
    for (int i = 0; i < MAX_UARTS; i++) {
        if (uart_registry[i] == NULL) {
            uart_registry[i] = ctx;
            return;
        }
    }
}

static UART_Context* GetContext(UART_HandleTypeDef *huart) {
    for (int i = 0; i < MAX_UARTS; i++) {
        if (uart_registry[i] != NULL && uart_registry[i]->huart == huart) {
            return uart_registry[i];
        }
    }
    return NULL;
}

void UART_Queue_Init(UART_Context *ctx, UART_HandleTypeDef *huart) {
    ctx->huart = huart;

    // Initialize indices
    ctx->tx_buffer.head = 0;
    ctx->tx_buffer.tail = 0;
    ctx->rx_buffer.head = 0;
    ctx->rx_buffer.tail = 0;

    ctx->tx_busy = 0;
    ctx->enable_led = false;

    RegisterContext(ctx);

    // Start Reception
    HAL_UART_Receive_IT(ctx->huart, &ctx->rx_byte_tmp, 1);
}

void UART_ConfigLED(UART_Context *ctx, GPIO_TypeDef* tx_port, uint16_t tx_pin, GPIO_TypeDef* rx_port, uint16_t rx_pin) {
    ctx->enable_led = true;
    ctx->tx_led_port = tx_port;
    ctx->tx_led_pin = tx_pin;
    ctx->rx_led_port = rx_port;
    ctx->rx_led_pin = rx_pin;
}

// --- TX Functions ---

int UART_SendByte(UART_Context *ctx, uint8_t data) {
    uint16_t next_head = (ctx->tx_buffer.head + 1) % UART_BUFFER_SIZE;

    // Check if buffer is full
    if (next_head == ctx->tx_buffer.tail) {
        return -1; // Buffer Full
    }

    ctx->tx_buffer.buffer[ctx->tx_buffer.head] = data;
    ctx->tx_buffer.head = next_head;

    // Start transmission if idle
    if (!ctx->tx_busy) {
        ctx->tx_busy = 1;
        
        // Turn on TX LED if enabled
        if (ctx->enable_led) {
            HAL_GPIO_WritePin(ctx->tx_led_port, ctx->tx_led_pin, GPIO_PIN_SET);
        }

        uint8_t *pData = &ctx->tx_buffer.buffer[ctx->tx_buffer.tail];
        // Only transmit 1 byte at a time to simplify buffer wrapping logic
        // and let ISR handle the rest
        if (HAL_UART_Transmit_IT(ctx->huart, pData, 1) != HAL_OK) {
             ctx->tx_busy = 0;
             if (ctx->enable_led) {
                 HAL_GPIO_WritePin(ctx->tx_led_port, ctx->tx_led_pin, GPIO_PIN_RESET);
             }
             return -2; // Error starting transmission
        }
    }
    return 0; // Success
}

int UART_SendArray(UART_Context *ctx, uint8_t *data, uint16_t length) {
    for (uint16_t i = 0; i < length; i++) {
        if (UART_SendByte(ctx, data[i]) != 0) {
            return -1; // Error or Buffer Full
        }
    }
    return 0;
}

int UART_SendString(UART_Context *ctx, char *str) {
    while (*str) {
        if (UART_SendByte(ctx, (uint8_t)*str++) != 0) {
            return -1;
        }
    }
    return 0;
}

int UART_SendStringBlocking(UART_Context *ctx, const char *str) {
    while (*str) {
        int rc = UART_SendByte(ctx, (uint8_t)*str);
        if (rc == 0) {
            str++;
            continue;
        }
        if (rc == -1) {
            // Buffer full: wait until ISR drains some bytes.
            continue;
        }
        // Other errors (e.g. couldn't start TX)
        return rc;
    }
    return 0;
}

// --- RX Functions ---

int UART_IsRxNotEmpty(UART_Context *ctx) {
    return (ctx->rx_buffer.head != ctx->rx_buffer.tail);
}

int UART_ReadByte(UART_Context *ctx, uint8_t *data) {
    if (ctx->rx_buffer.head == ctx->rx_buffer.tail) {
        return -1; // Buffer Empty
    }

    *data = ctx->rx_buffer.buffer[ctx->rx_buffer.tail];
    ctx->rx_buffer.tail = (ctx->rx_buffer.tail + 1) % UART_BUFFER_SIZE;
    
    // Turn off RX LED when buffer becomes empty
    if (ctx->enable_led && ctx->rx_buffer.head == ctx->rx_buffer.tail) {
        HAL_GPIO_WritePin(ctx->rx_led_port, ctx->rx_led_pin, GPIO_PIN_RESET);
    }
    
    return 0;
}

// --- HAL Callbacks ---

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    UART_Context *ctx = GetContext(huart);
    if (ctx == NULL) return;

    // Advance tail since the previous byte was sent
    ctx->tx_buffer.tail = (ctx->tx_buffer.tail + 1) % UART_BUFFER_SIZE;

    if (ctx->tx_buffer.tail != ctx->tx_buffer.head) {
        // More data to send
        uint8_t *pData = &ctx->tx_buffer.buffer[ctx->tx_buffer.tail];
        HAL_UART_Transmit_IT(ctx->huart, pData, 1);
    } else {
        // No more data
        ctx->tx_busy = 0;
        // Turn off TX LED if enabled
        if (ctx->enable_led) {
            HAL_GPIO_WritePin(ctx->tx_led_port, ctx->tx_led_pin, GPIO_PIN_RESET);
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    UART_Context *ctx = GetContext(huart);
    if (ctx == NULL) return;

    // Store received byte
    uint16_t next_head = (ctx->rx_buffer.head + 1) % UART_BUFFER_SIZE;
    if (next_head != ctx->rx_buffer.tail) {
        ctx->rx_buffer.buffer[ctx->rx_buffer.head] = ctx->rx_byte_tmp;
        ctx->rx_buffer.head = next_head;
    }
    // Else: Buffer overflow, drop byte

    // Turn on RX LED if enabled (will be turned off when buffer is drained)
    if (ctx->enable_led) {
        HAL_GPIO_WritePin(ctx->rx_led_port, ctx->rx_led_pin, GPIO_PIN_SET);
    }

    // Restart Reception
    HAL_UART_Receive_IT(ctx->huart, &ctx->rx_byte_tmp, 1);
}
