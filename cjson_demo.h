#ifndef CJSON_DEMO_H
#define CJSON_DEMO_H

#include "mqtt_demo.h"

// 字符串处理函数
int string_length(char *str);
char *combine_strings(int str_amount, char *str1, ...);

// 智能公厕JSON处理函数
char *make_toilet_status_json(char *service_id, ToiletMqttMsg *toilet_msg);
ToiletCommandMsg *parse_toilet_command_json(char *json_string);

// 兼容原有功能的函数
char *make_json(char *service_id, char *temperature, char *humidity);
char *parse_json(char *json_string);

#endif // CJSON_DEMO_H