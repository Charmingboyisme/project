#ifndef MQTT_DEMO_H
#define MQTT_DEMO_H

#include <stdint.h>

// 消息类型枚举
typedef enum {
    EN_MSG_TOILET_REPORT = 1,  // 厕位状态上报
    EN_MSG_TOILET_COMMAND = 2, // 厕位命令
} en_msg_type_t;

// 厕位类型枚举
typedef enum {
    TOILET_TYPE_UNISEX = 0,    // 通用厕位
    TOILET_TYPE_MALE = 1,      // 男厕
    TOILET_TYPE_FEMALE = 2,    // 女厕
} toilet_type_t;

// 命令类型枚举
typedef enum {
    CMD_SET_TYPE = 1,          // 设置厕位类型
    CMD_FORCE_OPEN = 2,        // 强制开门
    CMD_SEND_TISSUE = 3,       // 发送厕纸
} toilet_cmd_type_t;

// 厕位MQTT消息结构
typedef struct {
    int msg_type;              // 消息类型 (EN_MSG_TOILET_REPORT/EN_MSG_TOILET_COMMAND)
    uint8_t toilet_id;         // 厕位ID
    uint8_t state;             // 使用状态 0=空闲, 1=使用中
    uint8_t smoke;             // 烟雾状态 0=无, 1=有
    uint8_t paper;             // 纸巾状态 0=不足, 1=充足
    uint8_t overtime;          // 超时状态 0=正常, 1=超时
    uint8_t toilet_type;       // 厕位类型 0=通用, 1=男厕, 2=女厕
    uint8_t entry_side;        // 新增：进入侧 0=无, 1=左侧, 2=右侧
    uint8_t alert_type;        // 新增：警报类型 0=正常, 1=紧急, 2=超时
} ToiletMqttMsg;
// 厕位命令消息结构
typedef struct {
    uint8_t toilet_id;         // 厕位ID
    uint8_t cmd_type;          // 命令类型
    uint8_t param;             // 命令参数
} ToiletCommandMsg;

// 函数声明
int mqtt_init(void);
int mqtt_connect(void);
int mqtt_subscribe(const char *topic);
int mqtt_publish(const char *topic, const char *payload);
void set_toilet_msg_queue(unsigned long queue_handle);

#endif // MQTT_DEMO_H