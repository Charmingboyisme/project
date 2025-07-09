#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MQTTClient.h"
#include "MQTTClientPersistence.h"
#include "osal_debug.h"
#include "MQTTClient.h"
#include "los_memory.h"
#include "los_task.h"
#include "soc_osal.h"
#include "app_init.h"
#include "common_def.h"
#include "wifi_connect.h"
#include "watchdog.h"
#include "cjson_demo.h"
#include "mqtt_demo.h"

// MQTT连接配置
#define ADDRESS "tcp://23d5afc34a.st1.iotda-device.cn-north-4.myhuaweicloud.com"
#define CLIENTID "686a28e232771f177b495865_114514_0_0_2025070609"
#define QOS 1
#define TIMEOUT 10000L
#define MSG_MAX_LEN 64
#define MSG_QUEUE_SIZE 32

// 任务配置
#define MQTT_TASK_PRIO 24
#define MQTT_TASK_STACK_SIZE 0x1000

// 全局变量
static unsigned long g_toilet_msg_queue = 0;
volatile MQTTClient_deliveryToken deliveredtoken;
char *g_username = "686a28e232771f177b495865_114514"; // deviceId
char *g_password = "938b5d2af1145bbf90d67da872907a0dd09df0bb0df816d87f4008493a138e52";
MQTTClient client;
extern int MQTTClient_init(void);

// 外部函数声明
extern void send_command_to_queue(uint8_t toilet_id, uint8_t cmd_type, uint8_t param);

// 设置厕位消息队列句柄
void set_toilet_msg_queue(unsigned long queue_handle) {
    g_toilet_msg_queue = queue_handle;
}

// 消息传递回调
void delivered(void *context, MQTTClient_deliveryToken dt)
{
    unused(context);
    printf("Message with token value %d delivery confirmed\r\n", dt);
    deliveredtoken = dt;
}

// 消息接收回调
int msgArrived(void *context, char *topic_name, int topic_len, MQTTClient_message *message)
{
    unused(context);
    unused(topic_len);
    
    printf("MQTT message arrived: topic=%s, payload=%s\n", topic_name, (char*)message->payload);
    
    // 解析命令消息
    ToiletCommandMsg *cmd_msg = parse_toilet_command_json((char*)message->payload);
    if (cmd_msg != NULL) {
        printf("Parsed command: toilet_id=%d, cmd_type=%d, param=%d\n", 
               cmd_msg->toilet_id, cmd_msg->cmd_type, cmd_msg->param);
        
        // 发送命令到处理队列
        send_command_to_queue(cmd_msg->toilet_id, cmd_msg->cmd_type, cmd_msg->param);
        
        // 释放内存
        osal_kfree(cmd_msg);
    }
    
    return 1;
}

// 连接丢失回调
void connlost(void *context, char *cause)
{
    unused(context);
    printf("MQTT connection lost: %s\n", cause);
}

// 订阅主题
int mqtt_subscribe(const char *topic)
{
    printf("Subscribing to topic: %s\r\n", topic);
    int rc = MQTTClient_subscribe(client, topic, QOS);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to subscribe, return code %d\n", rc);
        return -1;
    }
    return 0;
}

// 发布消息
int mqtt_publish(const char *topic, const char *payload)
{
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc = 0;
    
    pubmsg.payload = (void*)payload;
    pubmsg.payloadlen = (int)strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to publish message, return code %d\r\n", rc);
        return -1;
    }
    
    printf("Message published: topic=%s, payload=%s\r\n", topic, payload);
    return 0;
}

// 连接MQTT服务器
int mqtt_connect(void)
{
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    printf("Starting MQTT connection...\r\n");
    MQTTClient_init();
    MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 120;
    conn_opts.cleansession = 1;
    if (g_username != NULL) {
        conn_opts.username = g_username;
        conn_opts.password = g_password;
    }
    
    MQTTClient_setCallbacks(client, NULL, connlost, msgArrived, delivered);
    
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return -1;
    }
    
    printf("MQTT connected successfully\r\n");
    return 0;
}

// MQTT主任务
static void *mqtt_task(const char *arg)
{
    unused(arg);
    int ret = 0;
    ToiletMqttMsg toilet_msg = {0};
    
    // 等待网络连接
    osal_msleep(3000);
    
    // 连接MQTT服务器
    ret = mqtt_connect();
    if (ret != 0) {
        printf("MQTT connect failed, result %d\n", ret);
        return NULL;
    }
    
    // 等待连接稳定
    osal_msleep(1000);
    
    // 订阅命令主题
    char *cmd_topic = combine_strings(3, "$oc/devices/", g_username, "/sys/commands/#");
    ret = mqtt_subscribe(cmd_topic);
    if (ret < 0) {
        printf("Subscribe topic error, result %d\n", ret);
    }
    osal_kfree(cmd_topic);
    
    // 构建数据上报主题
    char *report_topic = combine_strings(3, "$oc/devices/", g_username, "/sys/properties/report");
    
    // 主循环：处理厕位状态上报
    while (1) {
        // 从队列读取厕位状态消息
        unsigned int buffer_size = sizeof(ToiletMqttMsg);
        ret = osal_msg_queue_read_copy(g_toilet_msg_queue, &toilet_msg, &buffer_size, 1000); // 1秒超时
        if (ret == 0 && toilet_msg.msg_type == EN_MSG_TOILET_REPORT) {
            // 构建JSON消息
            char *json_msg = make_toilet_status_json("toilet_service", &toilet_msg);
            if (json_msg != NULL) {
                // 发布到MQTT
                mqtt_publish(report_topic, json_msg);
                osal_kfree(json_msg);
            }
        }
        
        osal_msleep(100);
    }
    
    osal_kfree(report_topic);
    return NULL;
}

// 初始化MQTT
int mqtt_init(void)
{
    osal_task *task_handle = NULL;
    
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)mqtt_task, 
                                     0, "MqttTask", MQTT_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, MQTT_TASK_PRIO);
    }
    osal_kthread_unlock();
    
    return 0;
}