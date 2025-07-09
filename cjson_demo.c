#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "cJSON.h"
#include "soc_osal.h"
#include "mqtt_demo.h"

// 计算字符串长度
int string_length(char *str)
{
    if (str == NULL) {
        return 0;
    }
    int len = 0;
    char *temp_str = str;
    while (*temp_str++ != '\0') {
        len++;
    }
    return len;
}

// 拼接字符串
char *combine_strings(int str_amount, char *str1, ...)
{
    int length = string_length(str1) + 1;
    if (length == 1) {
        return NULL; // 如果第一个字符串为空
    }

    char *result = osal_kmalloc(length, 0);
    if (result == NULL) {
        return NULL; // 内存分配失败
    }

    strcpy(result, str1); // 复制第一个字符串

    va_list args;
    va_start(args, str1);

    char *tem_str;
    while (--str_amount > 0) {
        tem_str = va_arg(args, char *);
        if (tem_str == NULL) {
            continue; // 跳过空字符串
        }
        length += string_length(tem_str);
        
        // 重新分配内存
        char *new_result = osal_kmalloc(length, 0);
        if (new_result == NULL) {
            osal_kfree(result);
            return NULL; // 内存重新分配失败
        }
        
        // 复制原有内容
        strcpy(new_result, result);
        osal_kfree(result);
        result = new_result;
        
        strcat(result, tem_str); // 拼接字符串
    }
    va_end(args);

    return result; // 返回拼接后的字符串
}

// 创建厕位状态JSON消息
char *make_toilet_status_json(char *service_id, ToiletMqttMsg *toilet_msg)
{
    if (service_id == NULL || toilet_msg == NULL) {
        return NULL;
    }

    // 创建根JSON对象
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    // 创建services数组
    cJSON *services = cJSON_CreateArray();
    if (services == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    // 创建service对象
    cJSON *service = cJSON_CreateObject();
    if (service == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddStringToObject(service, "service_id", service_id);

    // 创建properties对象
    cJSON *properties = cJSON_CreateObject();
    if (properties == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    // 添加厕位属性
    cJSON_AddNumberToObject(properties, "toilet_id", toilet_msg->toilet_id);
    cJSON_AddNumberToObject(properties, "state", toilet_msg->state);
    cJSON_AddNumberToObject(properties, "smoke", toilet_msg->smoke);
    cJSON_AddNumberToObject(properties, "paper", toilet_msg->paper);
    cJSON_AddNumberToObject(properties, "overtime", toilet_msg->overtime);
    cJSON_AddNumberToObject(properties, "toilet_type", toilet_msg->toilet_type);
    
    // 新增字段
    cJSON_AddNumberToObject(properties, "entry_side", toilet_msg->entry_side);
    cJSON_AddNumberToObject(properties, "alert_type", toilet_msg->alert_type);

    // 添加状态描述
    const char *state_desc = toilet_msg->state ? "in_use" : "free";
    const char *smoke_desc = toilet_msg->smoke ? "detected" : "none";
    const char *paper_desc = toilet_msg->paper ? "sufficient" : "insufficient";
    const char *overtime_desc = toilet_msg->overtime ? "yes" : "no";
    const char *type_desc = (toilet_msg->toilet_type == 1) ? "male" : 
                           (toilet_msg->toilet_type == 2) ? "female" : "unisex";
    
    // 新增描述字段
    const char *entry_desc = (toilet_msg->entry_side == 1) ? "left" : 
                            (toilet_msg->entry_side == 2) ? "right" : "none";
    const char *alert_desc = (toilet_msg->alert_type == 1) ? "emergency" : 
                            (toilet_msg->alert_type == 2) ? "timeout" : "normal";

    cJSON_AddStringToObject(properties, "state_desc", state_desc);
    cJSON_AddStringToObject(properties, "smoke_desc", smoke_desc);
    cJSON_AddStringToObject(properties, "paper_desc", paper_desc);
    cJSON_AddStringToObject(properties, "overtime_desc", overtime_desc);
    cJSON_AddStringToObject(properties, "type_desc", type_desc);
    
    // 新增描述字段
    cJSON_AddStringToObject(properties, "entry_desc", entry_desc);
    cJSON_AddStringToObject(properties, "alert_desc", alert_desc);

    // 组装JSON结构
    cJSON_AddItemToObject(service, "properties", properties);
    cJSON_AddItemToArray(services, service);
    cJSON_AddItemToObject(root, "services", services);

    // 转换为JSON字符串
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);

    return json_string;
}
// 解析厕位命令JSON
ToiletCommandMsg *parse_toilet_command_json(char *json_string)
{
    if (json_string == NULL) {
        return NULL;
    }

    // 解析JSON字符串
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        printf("Error parsing JSON\n");
        return NULL;
    }

    ToiletCommandMsg *cmd_msg = osal_kmalloc(sizeof(ToiletCommandMsg), 0);
    if (cmd_msg == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    // 初始化命令消息
    memset(cmd_msg, 0, sizeof(ToiletCommandMsg));

    // 解析命令参数
    cJSON *paras = cJSON_GetObjectItem(root, "paras");
    if (paras) {
        // 解析厕位ID
        cJSON *toilet_id = cJSON_GetObjectItem(paras, "toilet_id");
        if (toilet_id && cJSON_IsNumber(toilet_id)) {
            cmd_msg->toilet_id = (uint8_t)toilet_id->valueint;
        }

        // 解析命令类型
        cJSON *cmd_type = cJSON_GetObjectItem(paras, "cmd_type");
        if (cmd_type && cJSON_IsNumber(cmd_type)) {
            cmd_msg->cmd_type = (uint8_t)cmd_type->valueint;
        }

        // 解析命令参数
        cJSON *param = cJSON_GetObjectItem(paras, "param");
        if (param && cJSON_IsNumber(param)) {
            cmd_msg->param = (uint8_t)param->valueint;
        }

        // 兼容字符串格式的命令
        cJSON *cmd_str = cJSON_GetObjectItem(paras, "command");
        if (cmd_str && cJSON_IsString(cmd_str)) {
            if (strcmp(cmd_str->valuestring, "force_open") == 0) {
                cmd_msg->cmd_type = CMD_FORCE_OPEN;
            } else if (strcmp(cmd_str->valuestring, "send_tissue") == 0) {
                cmd_msg->cmd_type = CMD_SEND_TISSUE;
            } else if (strcmp(cmd_str->valuestring, "set_type") == 0) {
                cmd_msg->cmd_type = CMD_SET_TYPE;
            }
        }

        // 解析厕位类型设置
        cJSON *toilet_type = cJSON_GetObjectItem(paras, "toilet_type");
        if (toilet_type && cJSON_IsNumber(toilet_type)) {
            cmd_msg->param = (uint8_t)toilet_type->valueint;
        }
    }

    // 释放JSON内存
    cJSON_Delete(root);

    // 检查是否成功解析到有效命令
    if (cmd_msg->toilet_id == 0 || cmd_msg->cmd_type == 0) {
        printf("Invalid command parameters\n");
        osal_kfree(cmd_msg);
        return NULL;
    }

    return cmd_msg;
}

// 创建环境监测JSON（兼容原有功能）
char *make_json(char *service_id, char *temperature, char *humidity)
{
    if (service_id == NULL || temperature == NULL || humidity == NULL) {
        return NULL;
    }

    // 创建JSON对象
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    // 创建services数组
    cJSON *services = cJSON_CreateArray();
    if (services == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    // 创建service对象
    cJSON *service = cJSON_CreateObject();
    if (service == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddStringToObject(service, "service_id", service_id);

    // 创建properties对象
    cJSON *properties = cJSON_CreateObject();
    if (properties == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddStringToObject(properties, "temperature", temperature);
    cJSON_AddStringToObject(properties, "humidity", humidity);

    // 组装JSON结构
    cJSON_AddItemToObject(service, "properties", properties);
    cJSON_AddItemToArray(services, service);
    cJSON_AddItemToObject(root, "services", services);

    // 转换为JSON字符串
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);

    return json_string;
}

// 解析环境命令JSON（兼容原有功能）
char *parse_json(char *json_string)
{
    if (json_string == NULL) {
        return NULL;
    }

    char *string = NULL;
    // 解析JSON字符串
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        printf("Error parsing JSON\n");
        return NULL;
    }

    // 获取paras对象中的beep项
    cJSON *paras = cJSON_GetObjectItem(root, "paras");
    if (paras) {
        cJSON *beep = cJSON_GetObjectItem(paras, "beep");
        // 检查并输出beep的值
        if (beep && cJSON_IsString(beep)) {
            printf("beep: %s\n", beep->valuestring);
            string = beep->valuestring;
        } else {
            printf("beep not found or is not a string\n");
        }
    }

    // 释放内存
    cJSON_Delete(root);
    return string;
}