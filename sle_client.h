/*
 * Copyright (c) 2024 HiSilicon Technologies CO., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SLE_CLIENT_H
#define SLE_CLIENT_H

#include "sle_ssap_client.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* 厕位状态扩展消息结构 */
typedef struct {
    uint8_t toilet_id;      // 厕位ID
    uint8_t state;          // 0=free, 1=in_use
    uint8_t smoke;          // 0=no, 1=yes (预留)
    uint8_t paper;          // 0=sufficient, 1=insufficient
} toilet_status_msg_t;
/* 扩展厕位状态消息结构 */
typedef struct {
    uint8_t toilet_id;      // 厕位ID
    uint8_t state;          // 0=free, 1=in_use
    uint8_t entry_side;     // 0=none, 1=left, 2=right
    uint8_t paper;          // 0=sufficient, 1=insufficient
    uint8_t msg_type;       // 0=normal, 1=emergency, 2=timeout
} extended_toilet_status_msg_t;

/* 服务端命令结构 */
typedef struct {
    uint8_t toilet_id;      // 厕位ID
    uint8_t cmd_type;       // 命令类型：1=设置类型, 2=强制开门, 3=厕纸下发
    uint8_t param;          // 命令参数
} server_command_t;

/* 命令类型定义 */
typedef enum {
    CMD_SET_TYPE = 1,       // 设置厕位类型
    CMD_FORCE_OPEN = 2,     // 强制开门
    CMD_TISSUE_DELIVERY = 3 // 厕纸下发
} command_type_t;

/* 命令处理回调函数类型 */
typedef void (*command_handler_t)(server_command_t *cmd);

// 基础SLE客户端函数
void sle_client_init(void);
void sle_uart_start_scan(void);
void sle_uart_notification_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data, errcode_t status);
void sle_uart_indication_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data, errcode_t status);
void app_uart_init_config(void);

// 扩展功能函数
errcode_t sle_client_send_toilet_status(uint8_t toilet_id, uint8_t state, uint8_t smoke, uint8_t paper);
errcode_t sle_client_send_simple_status(uint8_t toilet_id, uint8_t state);
void sle_client_register_command_handler(command_handler_t handler);
bool sle_client_is_connected(void);
// 扩展功能函数
errcode_t sle_client_send_extended_status(uint8_t toilet_id, uint8_t state, 
                                         uint8_t entry_side, uint8_t paper, uint8_t msg_type);
// 消息队列操作
errcode_t sle_client_send_message(uint8_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif