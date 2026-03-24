#include "esp_bridge.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

// Приватні функції для внутрішнього використання
static void esp_send_at(const char *fmt, ...);
static void esp_send_raw(const uint8_t *data, uint16_t len);

// Глобальні змінні, які вже оголошені у .h файлі
const char* name_wifi = "Fox";
const char* password = "0685652120";

#define ESP_DMA_RX_SIZE 1024
uint8_t esp_dma_rx_buf[ESP_DMA_RX_SIZE] = {0};
uint8_t process_buf[ESP_DMA_RX_SIZE] = {0};
volatile uint16_t process_len = 0;
volatile uint8_t data_ready_flag = 0;

// Функція ініціалізації ESP
void ESP_Bridge_Init(void) {
    __HAL_UART_ENABLE_IT(&huart6, UART_IT_IDLE);
    if (HAL_UART_Receive_DMA(&huart6, esp_dma_rx_buf, ESP_DMA_RX_SIZE) != HAL_OK) {
        uart1_printf("DMA start failed\r\n");
        Error_Handler();
    }

    // Послідовність команд для налаштування ESP
    uart1_printf("Starting ESP web-server bridge...\r\n");
    esp_send_at("AT+RST\r\n"); HAL_Delay(2000);
    esp_send_at("AT+CWMODE=1\r\n"); HAL_Delay(500);
    uart1_printf("Connecting to Wi-Fi: %s\r\n", name_wifi);
    esp_send_at("AT+CWJAP=\"%s\",\"%s\"\r\n", name_wifi, password);
    HAL_Delay(8000);
    uart1_printf("Requesting IP address...\r\n");
    esp_send_at("AT+CIFSR\r\n");

    uint32_t start_time = HAL_GetTick();
    while (HAL_GetTick() - start_time < 2000) {
        if (data_ready_flag) {
            data_ready_flag = 0;
            esp_process_data(process_buf, process_len);
        }
    }

    esp_send_at("AT+CIPMUX=1\r\n"); HAL_Delay(500);
    esp_send_at("AT+CIPSERVER=1,80\r\n"); HAL_Delay(500);
    uart1_printf("Server started!\r\n");
}

// Функція для циклічної обробки даних
void ESP_Bridge_Process(void) {
    if (data_ready_flag) {
        data_ready_flag = 0;
        esp_process_data(process_buf, process_len);
    }
}

void esp_process_data(uint8_t* data, uint16_t len)
{
    // Друк отриманих даних для дебагу
    uart1_printf("--- Processing Packet (len: %d) ---\r\n", len);
    HAL_UART_Transmit(&huart1, data, len, 200);
    uart1_printf("\r\n--- End Packet ---\r\n");

    // Шукаємо вхідний веб-запит "+IPD"
    char *ipd = (char*)memmem(data, len, "+IPD,", 5);
    if (ipd) {
        int conn_id = atoi(ipd + 5);
        char *data_start = strchr(ipd, ':');
        if (data_start) {
            data_start++; // Переходимо до початку HTTP-даних


            // Керування світлодіодом
            if (memmem(data_start, len - (data_start - (char*)data), "GET /LED=ON", 11)) {
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
                led_state = 1; // Встановлюємо стан ON
                uart1_printf("Action: LED ON\r\n");
            } else if (memmem(data_start, len - (data_start - (char*)data), "GET /LED=OFF", 12)) {
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
                led_state = 0; // Встановлюємо стан OFF
                uart1_printf("Action: LED OFF\r\n");
            }
//            // Керування світлодіодом
//            if (memmem(data_start, len - (data_start - (char*)data), "GET /LED=ON", 11)) {
//                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
//                uart1_printf("Action: LED ON\r\n");
//            } else if (memmem(data_start, len - (data_start - (char*)data), "GET /LED=OFF", 12)) {
//                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
//                uart1_printf("Action: LED OFF\r\n");
//            }

            // Формування та відправка HTML-сторінки з даними датчика
            char http_page[512];
            const char page_template[] =
              "<!DOCTYPE html>"
              "<html>"
              "<head><meta charset=\"UTF-8\">"
              "<meta http-equiv=\"refresh\" content=\"5\">"
              "<title>STM32 Sensor Data</title>"
              "</head>"
              "<body>"
              "<h1>STM32 & ESP8266 Data</h1>"
              "<h2>Sensor Readings</h2>"
              "<p><b>Temperature:</b> %.2f &deg;C</p>"
              "<p><b>Humidity:</b> %.2f %%</p>"
              "<p><b>Pressure:</b> %.2f hPa</p>"
              "<p><b>Altitude:</b> %.2f m</p>"
              "<h2>LED Control</h2>"
              "<p>LED Status: **%s**</p>" // Нове поле для статусу
              "<p><a href=\"/LED=ON\">TURN ON LED</a></p>"
              "<p><a href=\"/LED=OFF\">TURN OFF LED</a></p>"
              "</body>"
              "</html>";

            // Генерація HTML-сторінки з поточними даними
            // Тиск перетворюємо в гектопаскалі (hPa), розділивши на 100
//            int page_len = snprintf(http_page, sizeof(http_page),
//                                    page_template,
//                                    Temperature, Humidity, Pressure / 100.0f, Altitude);
            int page_len = snprintf(http_page, sizeof(http_page),
                                    page_template,
                                    Temperature, Humidity, Pressure / 100.0f, Altitude,
                                    led_state ? "ON" : "OFF");

            // Формування та відправка HTTP-відповіді
            char response[1024];
            const char http_header_template[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n";
            char header[128];
            snprintf(header, sizeof(header), http_header_template, page_len);
            int response_len = snprintf(response, sizeof(response), "%s%s", header, http_page);

            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%d\r\n", conn_id, response_len);
            esp_send_at(cmd);
            HAL_Delay(100);
            HAL_UART_Transmit(&huart6, (uint8_t*)response, response_len, 1000);
            HAL_Delay(200);
            snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d\r\n", conn_id);
            esp_send_at(cmd);
        }
    }

    // Додаткова логіка для обробки IP-адреси
    char *cifsr_ip = (char*)memmem(data, len, "+CIFSR:STAIP,\"", 14);
    if (cifsr_ip) {
        cifsr_ip += 14;
        char *ip_end = strchr(cifsr_ip, '"');
        if (ip_end) {
            int ip_len = ip_end - cifsr_ip;
            uart1_printf("=====================================\r\n");
            uart1_printf(">>> ESP IP Address: %.*s\r\n", ip_len, cifsr_ip);
            uart1_printf("=====================================\r\n");
        }
    }
}

// Функція обробки даних, викликається з main
//void esp_process_data(uint8_t* data, uint16_t len)
//{
//    // Друк отриманих даних для дебагу
//    uart1_printf("--- Processing Packet (len: %d) ---\r\n", len);
//    HAL_UART_Transmit(&huart1, data, len, 200);
//    uart1_printf("\r\n--- End Packet ---\r\n");
//
//    // 1. Шукаємо IP-адресу у відповіді на AT+CIFSR
//    char *cifsr_ip = (char*)memmem(data, len, "+CIFSR:STAIP,\"", 14);
//    if (cifsr_ip) {
//        cifsr_ip += 14; // Пропускаємо текст "+CIFSR:STAIP,""
//        char *ip_end = strchr(cifsr_ip, '"');
//        if (ip_end) {
//            int ip_len = ip_end - cifsr_ip;
//            uart1_printf("=====================================\r\n");
//            uart1_printf(">>> ESP IP Address: %.*s\r\n", ip_len, cifsr_ip);
//            uart1_printf("=====================================\r\n");
//        }
//    }
//
//    // 2. Шукаємо вхідний веб-запит "+IPD"
//    char *ipd = (char*)memmem(data, len, "+IPD,", 5);
//    if (ipd) {
//        int conn_id = atoi(ipd + 5);
//        char *data_start = strchr(ipd, ':');
//        if (data_start) {
//            data_start++; // Переходимо до початку HTTP-даних
//
//            // Керування світлодіодом
//            if (memmem(data_start, len - (data_start - (char*)data), "GET /LED=ON", 11)) {
//                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
//                uart1_printf("Action: LED ON\r\n");
//            } else if (memmem(data_start, len - (data_start - (char*)data), "GET /LED=OFF", 12)) {
//                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
//                uart1_printf("Action: LED OFF\r\n");
//            }
//
//            // Формування та відправка HTML-сторінки
//            const char http_page[] = "<!DOCTYPE html><html><body><h1>STM32 Control</h1><p><a href=\"/LED=ON\">LED ON</a></p><p><a href=\"/LED=OFF\">LED OFF</a></p></body></html>";
//
//
//            const char http_header_template[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n";
//            char response[512];
//            char header[128];
//            snprintf(header, sizeof(header), http_header_template, (int)strlen(http_page));
//            int response_len = snprintf(response, sizeof(response), "%s%s", header, http_page);
//            char cmd[32];
//            snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%d\r\n", conn_id, response_len);
//            esp_send_at(cmd);
//            HAL_Delay(100);
//            HAL_UART_Transmit(&huart6, (uint8_t*)response, response_len, 1000);
//            HAL_Delay(200);
//            snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d\r\n", conn_id);
//            esp_send_at(cmd);
//        }
//    }
//}

// Ваші допоміжні функції
static void esp_send_at(const char *fmt, ...)
{
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  uint16_t l = strlen(buf);
  HAL_UART_Transmit(&huart6, (uint8_t*)buf, l, 500);
  uart1_printf("AT->ESP: %s", buf);
}

void uart1_printf(const char *fmt, ...)
{
  char buf[256];
  va_list args;
  va_start(args, fmt);
  int l = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (l>0) HAL_UART_Transmit(&huart1, (uint8_t*)buf, l, 200);
}

void *memmem(const void *haystack, size_t haystacklen,
             const void *needle, size_t needlelen)
{
  if (needlelen == 0) return (void*)haystack;
  const char *h = haystack;
  for (size_t i = 0; i + needlelen <= haystacklen; ++i) {
    if (memcmp(h + i, needle, needlelen) == 0) return (void*)(h + i);
  }
  return NULL;
}
