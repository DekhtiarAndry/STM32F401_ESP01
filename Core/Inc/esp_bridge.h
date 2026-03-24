#ifndef ESP_BRIDGE_H
#define ESP_BRIDGE_H

#include "main.h" // Включення стандартних для STM32 заголовочних файлів
#include <stdint.h>
#include <string.h>

// Оголошення глобальних змінних, до яких потрібно звертатися ззовні
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart6;

extern uint8_t esp_dma_rx_buf[1024];
extern uint8_t process_buf[1024];
extern volatile uint16_t process_len;
extern volatile uint8_t data_ready_flag;

extern volatile uint8_t led_state;

extern float Temperature, Pressure, Humidity, Altitude;

// Прототипи функцій, які будуть доступні ззовні
void ESP_Bridge_Init(void);
void ESP_Bridge_Process(void);
void esp_process_data(uint8_t* data, uint16_t len);
void uart1_printf(const char *fmt, ...);
void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);

// Допоміжні функції, які також можуть бути корисними ззовні
static void esp_send_at(const char *fmt, ...);

#endif /* ESP_BRIDGE_H */
