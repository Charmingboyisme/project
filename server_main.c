#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "i2c.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "sle_server.h"
#include "mqtt_demo.h"
#include "wifi_connect.h"
#include "watchdog.h"
#include <time.h>

// 硬件配置
#define TASK_PRIO                   25
#define TASK_STACK_SIZE             0x1000
#define CONFIG_I2C_SCL_PIN          15
#define CONFIG_I2C_SDA_PIN          16
#define I2C_SET_BANDRATE            400000
#define MAX_TOILETS                 4

// WiFi配置
#define CONFIG_WIFI_SSID "test"       // 要连接的WiFi 热点账号
#define CONFIG_WIFI_PWD "12345678" // 要连接的WiFi 热点密码

// 厕位状态结构（扩展版本）
typedef struct {
    uint8_t id;                    // 厕位ID
    uint8_t state;                 // 0=free, 1=in_use
    uint8_t smoke;                 // 0=no, 1=yes
    uint8_t paper;                 // 0=sufficient, 1=insufficient
    uint8_t overtime;              // 0=normal, 1=overtime
    uint8_t toilet_type;           // 0=unisex, 1=male, 2=female
    time_t use_start_time;         // 使用开始时间
    time_t last_update;            // 最后更新时间
} ToiletStatus;

// 命令结构
typedef struct {
    uint8_t toilet_id;
    uint8_t cmd_type;              // 1=设置类型, 2=强制开门, 3=厕纸发送
    uint8_t param;                 // 命令参数
} ToiletCommand;

static ToiletStatus toilet_status[MAX_TOILETS] = {0};
static uint8_t active_toilets = 0;
static unsigned long g_toilet_msg_queue = 0;
static unsigned long g_cmd_msg_queue = 0;

// 使用时间阈值（秒）
#define USAGE_TIME_THRESHOLD 300  // 5分钟
// // 厕位状态更新函数声明
void update_toilet_status(uint8_t id, uint8_t state);
void update_toilet_status_extended(uint8_t id, uint8_t state, uint8_t smoke, uint8_t paper);
void update_toilet_status_with_extended_info(uint8_t id, uint8_t state, uint8_t smoke, 
                                            uint8_t paper, uint8_t entry_side, uint8_t alert_type);
// OLED显示函数
static void oled_show_status(void) {
    char status_str[32] = {0};
    char type_str[8] = {0};
    ssd1306_Fill(Black);
    
    for (int i = 0; i < active_toilets && i < 4; i++) {
        // 厕位类型字符串
        switch (toilet_status[i].toilet_type) {
            case 1: strcpy(type_str, "M"); break;
            case 2: strcpy(type_str, "F"); break;
            default: strcpy(type_str, "U"); break;
        }
        
        // 状态字符串
        snprintf(status_str, sizeof(status_str), "%s%d:%s%s%s%s", 
                type_str,
                toilet_status[i].id,
                toilet_status[i].state ? "USE" : "FREE",
                toilet_status[i].smoke ? " SMK" : "",
                toilet_status[i].paper ? "" : " NP",
                toilet_status[i].overtime ? " OT" : "");
        
        ssd1306_SetCursor(0, i * 12);
        ssd1306_DrawString(status_str, Font_6x8, White);
    }
    
    ssd1306_UpdateScreen();
}

// 初始化I2C和OLED
static void init_oled(void) {
    // 配置I2C引脚
    uapi_pin_set_mode(CONFIG_I2C_SCL_PIN, 2); // PIN_MODE_I2C
    uapi_pin_set_mode(CONFIG_I2C_SDA_PIN, 2); // PIN_MODE_I2C
    
    // 初始化I2C
    if (uapi_i2c_master_init(1, I2C_SET_BANDRATE, 0x0) != 0) {
        osal_printk("I2C init failed!\r\n");
        return;
    }
    
    // 初始化OLED
    ssd1306_Init();
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_DrawString("Smart Toilet", Font_11x18, White);
    ssd1306_SetCursor(0, 20);
    ssd1306_DrawString("Server Ready", Font_7x10, White);
    ssd1306_UpdateScreen();
    osal_printk("OLED initialized\r\n");
}

// 检查使用时间是否过长
static void check_usage_time(void) {
    time_t current_time = time(NULL);
    
    for (int i = 0; i < active_toilets; i++) {
        if (toilet_status[i].state == 1) { // 正在使用
            if (current_time - toilet_status[i].use_start_time > USAGE_TIME_THRESHOLD) {
                toilet_status[i].overtime = 1;
                osal_printk("Toilet %d usage overtime!\n", toilet_status[i].id);
            }
        }
    }
}
// 更新厕位状态（扩展版本 - 4字节消息：ID + State + Smoke + Paper）
void update_toilet_status_extended(uint8_t id, uint8_t state, uint8_t smoke, uint8_t paper) {
    int found = 0;
    time_t current_time = time(NULL);
    
    // 查找现有厕位
    for (int i = 0; i < active_toilets; i++) {
        if (toilet_status[i].id == id) {
            // 如果从空闲变为使用中，记录开始时间
            if (toilet_status[i].state == 0 && state == 1) {
                toilet_status[i].use_start_time = current_time;
                toilet_status[i].overtime = 0;
            }
            // 如果从使用中变为空闲，清除超时标记
            if (toilet_status[i].state == 1 && state == 0) {
                toilet_status[i].overtime = 0;
            }
            
            toilet_status[i].state = state;
            toilet_status[i].smoke = smoke;
            toilet_status[i].paper = paper;
            toilet_status[i].last_update = current_time;
            found = 1;
            break;
        }
    }
    
    // 添加新厕位
    if (!found && active_toilets < MAX_TOILETS) {
        toilet_status[active_toilets].id = id;
        toilet_status[active_toilets].state = state;
        toilet_status[active_toilets].smoke = smoke;
        toilet_status[active_toilets].paper = paper;
        toilet_status[active_toilets].overtime = 0;
        toilet_status[active_toilets].toilet_type = 0; // 默认通用
        toilet_status[active_toilets].use_start_time = state ? current_time : 0;
        toilet_status[active_toilets].last_update = current_time;
        active_toilets++;
    }
    
    // 更新显示
    oled_show_status();
    osal_printk("Toilet %d - State:%s Smoke:%s Paper:%s\n", 
                id, 
                state ? "USED" : "FREE",
                smoke ? "YES" : "NO",
                paper ? "OK" : "LOW");
    
    // 发送状态到MQTT队列
    ToiletMqttMsg mqtt_msg = {0};
    mqtt_msg.msg_type = EN_MSG_TOILET_REPORT;
    mqtt_msg.toilet_id = id;
    mqtt_msg.state = state;
    mqtt_msg.smoke = smoke;
    mqtt_msg.paper = paper;
    
    // 查找对应厕位的其他信息
    int toilet_index = -1;
    for (int i = 0; i < active_toilets; i++) {
        if (toilet_status[i].id == id) {
            toilet_index = i;
            break;
        }
    }
    
    if (toilet_index >= 0) {
        mqtt_msg.overtime = toilet_status[toilet_index].overtime;
        mqtt_msg.toilet_type = toilet_status[toilet_index].toilet_type;
    } else {
        mqtt_msg.overtime = 0;
        mqtt_msg.toilet_type = 0;
    }
    
    // 为4字节消息设置新增字段的默认值
    mqtt_msg.entry_side = 0;  // 默认无进入侧信息
    mqtt_msg.alert_type = 0;  // 默认正常状态
    
    unsigned long ret = osal_msg_queue_write_copy(g_toilet_msg_queue, &mqtt_msg, sizeof(ToiletMqttMsg), 0);
    if (ret != 0) {
        osal_printk("Failed to send toilet status to MQTT queue\n");
    }
}

// 兼容性函数（保持原有接口）
void update_toilet_status(uint8_t id, uint8_t state) {
    update_toilet_status_extended(id, state, 0, 1); // 默认无烟、纸张充足
}

// 处理来自云端的命令
void handle_toilet_command(uint8_t toilet_id, uint8_t cmd_type, uint8_t param) {
    osal_printk("Received command - Toilet:%d Type:%d Param:%d\n", toilet_id, cmd_type, param);
    
    switch (cmd_type) {
        case 1: // 设置厕位类型
            for (int i = 0; i < active_toilets; i++) {
                if (toilet_status[i].id == toilet_id) {
                    toilet_status[i].toilet_type = param;
                    osal_printk("Toilet %d type set to %d\n", toilet_id, param);
                    oled_show_status();
                    break;
                }
            }
            break;
            
        case 2: // 强制开门
            osal_printk("Force open toilet %d\n", toilet_id);
            // 发送命令给对应的客户端
            sle_server_send_command_to_client(toilet_id, cmd_type, param);
            break;
            
        case 3: // 厕纸发送
            osal_printk("Send tissue to toilet %d\n", toilet_id);
            // 发送命令给对应的客户端
            sle_server_send_command_to_client(toilet_id, cmd_type, param);
            break;
            
        default:
            osal_printk("Unknown command type: %d\n", cmd_type);
            break;
    }
}

// 状态监控任务
static void *status_monitor_task(const char *arg) {
    unused(arg);
    
    while (1) {
        check_usage_time();
        osal_msleep(10000); // 每10秒检查一次
    }
    
    return NULL;
}

// 命令处理任务
static void *command_task(const char *arg) {
    unused(arg);
    ToiletCommand cmd = {0};
    
    while (1) {
        unsigned int buffer_size = sizeof(ToiletCommand);
        unsigned long ret = osal_msg_queue_read_copy(g_cmd_msg_queue, &cmd, &buffer_size, OSAL_WAIT_FOREVER);
        if (ret == 0) {
            handle_toilet_command(cmd.toilet_id, cmd.cmd_type, cmd.param);
        }
        osal_msleep(100);
    }
    
    return NULL;
}

// 主任务函数
static void *main_task(const char *arg) {
    unused(arg);
    osal_printk("\r\nSmart Toilet Server Started!\r\n\r\n");
    
    // 禁用看门狗
    uapi_watchdog_disable();
    
    // 初始化硬件
    init_oled();
    
    // 初始化星闪服务端
    sle_server_init();
    
    // 连接WiFi
    wifi_connect(CONFIG_WIFI_SSID, CONFIG_WIFI_PWD);
    
    // 初始化MQTT
    mqtt_init();
    
    // 创建监控任务
    osal_task *monitor_task_handle = NULL;
    osal_task *cmd_task_handle = NULL;
    
    osal_kthread_lock();
    monitor_task_handle = osal_kthread_create((osal_kthread_handler)status_monitor_task, 
                                             0, "MonitorTask", TASK_STACK_SIZE);
    if (monitor_task_handle != NULL) {
        osal_kthread_set_priority(monitor_task_handle, TASK_PRIO + 1);
    }
    
    cmd_task_handle = osal_kthread_create((osal_kthread_handler)command_task, 
                                         0, "CommandTask", TASK_STACK_SIZE);
    if (cmd_task_handle != NULL) {
        osal_kthread_set_priority(cmd_task_handle, TASK_PRIO + 2);
    }
    osal_kthread_unlock();
    
    // 主循环
    while (1) {
        osal_msleep(5000);
        // 可以在这里添加其他周期性任务
    }
    
    return NULL;
}

// 向命令队列发送命令（供MQTT回调使用）
void send_command_to_queue(uint8_t toilet_id, uint8_t cmd_type, uint8_t param) {
    ToiletCommand cmd = {0};
    cmd.toilet_id = toilet_id;
    cmd.cmd_type = cmd_type;
    cmd.param = param;
    
    unsigned long ret = osal_msg_queue_write_copy(g_cmd_msg_queue, &cmd, sizeof(ToiletCommand), 0);
    if (ret != 0) {
        osal_printk("Failed to send command to queue\n");
    }
}

// 任务入口函数
static void system_entry(void) {
    unsigned long ret;
    
    // 创建消息队列
    ret = osal_msg_queue_create("toilet_msg", 16, &g_toilet_msg_queue, 0, sizeof(ToiletMqttMsg));
    if (ret != OSAL_SUCCESS) {
        osal_printk("Create toilet message queue failed: %lx\n", ret);
    }
    
    ret = osal_msg_queue_create("cmd_msg", 16, &g_cmd_msg_queue, 0, sizeof(ToiletCommand));
    if (ret != OSAL_SUCCESS) {
        osal_printk("Create command message queue failed: %lx\n", ret);
    }
    
    // 设置全局队列句柄供MQTT使用
    set_toilet_msg_queue(g_toilet_msg_queue);
    
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)main_task, 
                                     0, "ServerMainTask", TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, TASK_PRIO);
    }
    osal_kthread_unlock();
}
// 处理扩展厕位状态（包含进入侧和消息类型）
// 处理扩展厕位状态（5字节消息：ID + State + Entry_side + Paper + Alert_type）
void update_toilet_status_with_extended_info(uint8_t id, uint8_t state, uint8_t smoke, 
                                            uint8_t paper, uint8_t entry_side, uint8_t alert_type) {
    int found = 0;
    time_t current_time = time(NULL);
    
    // 查找现有厕位
    for (int i = 0; i < active_toilets; i++) {
        if (toilet_status[i].id == id) {
            // 如果从空闲变为使用中，记录开始时间
            if (toilet_status[i].state == 0 && state == 1) {
                toilet_status[i].use_start_time = current_time;
                toilet_status[i].overtime = 0;
            }
            // 如果从使用中变为空闲，清除超时标记
            if (toilet_status[i].state == 1 && state == 0) {
                toilet_status[i].overtime = 0;
            }
            
            // 根据alert_type设置超时状态
            if (alert_type == 2) { // 超时警报
                toilet_status[i].overtime = 1;
            }
            
            toilet_status[i].state = state;
            toilet_status[i].smoke = smoke;
            toilet_status[i].paper = paper;
            toilet_status[i].last_update = current_time;
            found = 1;
            break;
        }
    }
    
    // 添加新厕位
    if (!found && active_toilets < MAX_TOILETS) {
        toilet_status[active_toilets].id = id;
        toilet_status[active_toilets].state = state;
        toilet_status[active_toilets].smoke = smoke;
        toilet_status[active_toilets].paper = paper;
        toilet_status[active_toilets].overtime = (alert_type == 2) ? 1 : 0;
        toilet_status[active_toilets].toilet_type = 0; // 默认通用
        toilet_status[active_toilets].use_start_time = state ? current_time : 0;
        toilet_status[active_toilets].last_update = current_time;
        active_toilets++;
    }
    
    // 更新显示
    oled_show_status();
    
    // 处理特殊警报类型
    switch (alert_type) {
        case 1:
            osal_printk("EMERGENCY: Toilet %d emergency situation!\n", id);
            break;
        case 2:
            osal_printk("TIMEOUT: Toilet %d usage timeout!\n", id);
            break;
        default:
            break;
    }
    
    // 详细日志输出
    const char *entry_desc = (entry_side == 1) ? "LEFT" : 
                            (entry_side == 2) ? "RIGHT" : "NONE";
    const char *alert_desc = (alert_type == 1) ? "EMERGENCY" : 
                            (alert_type == 2) ? "TIMEOUT" : "NORMAL";
    
    osal_printk("Extended Toilet %d - State:%s Entry:%s Paper:%s Alert:%s\n", 
                id, 
                state ? "USED" : "FREE",
                entry_desc,
                paper ? "LOW" : "OK", 
                alert_desc);
    
    // 发送状态到MQTT队列
    ToiletMqttMsg mqtt_msg = {0};
    mqtt_msg.msg_type = EN_MSG_TOILET_REPORT;
    mqtt_msg.toilet_id = id;
    mqtt_msg.state = state;
    mqtt_msg.smoke = smoke;
    mqtt_msg.paper = paper;
    
    // 查找对应厕位的其他信息
    int toilet_index = -1;
    for (int i = 0; i < active_toilets; i++) {
        if (toilet_status[i].id == id) {
            toilet_index = i;
            break;
        }
    }
    
    if (toilet_index >= 0) {
        mqtt_msg.overtime = toilet_status[toilet_index].overtime;
        mqtt_msg.toilet_type = toilet_status[toilet_index].toilet_type;
    } else {
        mqtt_msg.overtime = (alert_type == 2) ? 1 : 0;
        mqtt_msg.toilet_type = 0;
    }
    
    // 设置扩展字段
    mqtt_msg.entry_side = entry_side;
    mqtt_msg.alert_type = alert_type;
    
    unsigned long ret = osal_msg_queue_write_copy(g_toilet_msg_queue, &mqtt_msg, sizeof(ToiletMqttMsg), 0);
    if (ret != 0) {
        osal_printk("Failed to send toilet status to MQTT queue\n");
    }
}
// 启动系统
app_run(system_entry);