#ifndef SLE_SERVER_H
#define SLE_SERVER_H

#include <stdint.h>
#include "sle_ssap_server.h"
#include "errcode.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */
/* 任务相关 */
#define SLE_SERVER_TASK_PRIO 24
#define SLE_SERVER_STACK_SIZE 0x2000
/* 串口接收数据结构体 */
typedef struct {
    uint8_t *value;
    uint16_t value_len;
} msg_data_t;
/* 串口接收io */
#define CONFIG_UART_TXD_PIN 17
#define CONFIG_UART_RXD_PIN 18
#define CONFIG_UART_PIN_MODE 1
#define CONFIG_UART_ID UART_BUS_0

errcode_t sle_server_init(void);

errcode_t sle_server_send_report_by_uuid(msg_data_t msg_data);

errcode_t sle_server_send_report_by_handle(msg_data_t msg_data);

uint16_t sle_client_is_connected(void);

uint16_t get_connect_id(void);
// 添加到现有函数声明后面
int sle_server_send_command_to_client(uint8_t toilet_id, uint8_t cmd_type, uint8_t param);
void app_uart_init_config(void);
#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif