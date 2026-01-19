/*
 * uart_queue.h
 *
 *  Created on: Dec 1, 2025
 *      Author: AI Assistant
 *
 *  STM32 UART Queue Library
 *  Supports: STM32F3, STM32F4, STM32F7, STM32H7 series
 */

#ifndef INC_UART_QUEUE_H_
#define INC_UART_QUEUE_H_

/* STM32 HAL Auto-detection */
#if defined(STM32F3)
    #include "stm32f3xx_hal.h"
#elif defined(STM32F4)
    #include "stm32f4xx_hal.h"
#elif defined(STM32F7)
    #include "stm32f7xx_hal.h"
#elif defined(STM32H7)
    #include "stm32h7xx_hal.h"
#elif defined(STM32G4)
    #include "stm32g4xx_hal.h"
#elif defined(STM32L4)
    #include "stm32l4xx_hal.h"
#else
    #error "Unsupported STM32 series. Define STM32F3, STM32F4, STM32F7, STM32H7, STM32G4, or STM32L4"
#endif

#include <stdbool.h>

#define UART_BUFFER_SIZE 2048U

typedef struct {
	uint8_t buffer[UART_BUFFER_SIZE];
	volatile uint16_t head;
	volatile uint16_t tail;
} RingBuffer;

typedef struct {
    UART_HandleTypeDef *huart;
    
    RingBuffer tx_buffer;
    RingBuffer rx_buffer;
    
    volatile uint8_t tx_busy;
    uint8_t rx_byte_tmp;

    // LED Configuration
    bool enable_led;
    GPIO_TypeDef* tx_led_port;
    uint16_t tx_led_pin;
    GPIO_TypeDef* rx_led_port;
    uint16_t rx_led_pin;
} UART_Context;

void UART_Queue_Init(UART_Context *ctx, UART_HandleTypeDef *huart);
void UART_ConfigLED(UART_Context *ctx, GPIO_TypeDef* tx_port, uint16_t tx_pin, GPIO_TypeDef* rx_port, uint16_t rx_pin);

// TX Functions
int UART_SendByte(UART_Context *ctx, uint8_t data);
int UART_SendArray(UART_Context *ctx, uint8_t *data, uint16_t length);
int UART_SendString(UART_Context *ctx, char *str);
int UART_SendStringBlocking(UART_Context *ctx, const char *str);

// RX Functions
int UART_IsRxNotEmpty(UART_Context *ctx);
int UART_ReadByte(UART_Context *ctx, uint8_t *data);

#endif /* INC_UART_QUEUE_H_ */
