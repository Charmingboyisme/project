#include "pinctrl.h"
#include "adc.h"
#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "i2c.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "sle_client.h"
#include "peri.h"
#include "timer.h"
#include "tcxo.h"
#include "chip_core_irq.h"
#include "watchdog.h"

// 函数声明
static void send_toilet_status(void);
static void oled_show_message(char *msg);
static void oled_show_status(void);
static void init_oled(void);
static void init_timer(void);
static void handle_left_button(void);
static void handle_right_button(void);
static void handle_internal_button(void);
static void handle_emergency_button(void);
static void handle_tissue_button(void);
static void handle_button_d(void);
static void check_paper_status(void);
static void handle_server_command(server_command_t *cmd);

static void usage_timeout_callback(uintptr_t data);
static void emergency_response_callback(uintptr_t data);
// 硬件配置
#define DELAY_100MS                 100
#define TASK_PRIO                   26
#define TASK_STACK_SIZE             0x1000
#define CONFIG_I2C_SCL_PIN          15
#define CONFIG_I2C_SDA_PIN          16
#define I2C_SET_BANDRATE            400000

// 定时器配置
#define TIMER_INDEX                 1
#define TIMER_PRIO                  1
#define FORCE_OPEN_TIMEOUT          5000  // 5秒强制开门超时
#define BUTTON_DEBOUNCE_TIME        1000  // 1秒防抖
#define STATUS_CHECK_INTERVAL       2000  // 2秒状态检查间隔
// 在现有定义后添加这些新定义
#define USAGE_TIMEOUT_FIRST         10000 // 10秒第一次超时
#define USAGE_TIMEOUT_SECOND        10000 // 10秒第二次超时  
#define EMERGENCY_RESPONSE_TIME     5000  // 5秒紧急响应时间

// 新增消息类型
typedef enum {
    MSG_NORMAL,         // 正常消息
    MSG_EMERGENCY,      // 紧急消息  
    MSG_TIMEOUT         // 超时消息
} MessageType;

// 新增超时状态
typedef enum {
    TIMEOUT_NONE,       // 无超时
    TIMEOUT_FIRST,      // 第一次超时警报
    TIMEOUT_EMERGENCY,  // 紧急响应期
    TIMEOUT_SECOND      // 第二次超时驱逐
} TimeoutState;
// 厕位状态定义
typedef enum {
    TOILET_FREE,        // 厕位空闲
    TOILET_IN_USE       // 厕位使用中
} ToiletState;

// 厕位类型定义
typedef enum {
    TOILET_UNISEX,      // 通用厕位
    TOILET_MALE,        // 男厕
    TOILET_FEMALE       // 女厕
} ToiletType;

// 开门方向定义
typedef enum {
    SIDE_NONE,          // 无方向
    SIDE_LEFT,          // 左侧开门
    SIDE_RIGHT          // 右侧开门
} DoorSide;

// 系统状态定义
typedef enum {
    SYS_NORMAL,         // 正常状态
    SYS_FORCE_OPEN,     // 强制开门状态
    SYS_EMERGENCY       // 紧急状态
} SystemState;

// 系统控制结构体
typedef struct {
    ToiletState toilet_state;    // 厕位使用状态
    ToiletType toilet_type;      // 厕位类型
    DoorSide entry_side;         // 进入方向
    SystemState sys_state;       // 系统状态
    uint8_t toilet_id;           // 厕位ID
    uint8_t light_level;         // 光照强度级别 (1-5)
    bool paper_sufficient;       // 厕纸是否充足
    bool force_open_pending;     // 强制开门等待中
    timer_handle_t force_timer;  // 强制开门定时器
    uint32_t last_button_time;   // 上次按钮按下时间
    
    // 新增字段
    TimeoutState timeout_state;     // 超时状态
    timer_handle_t usage_timer;     // 使用时长定时器
    timer_handle_t emergency_timer; // 紧急响应定时器
    uint32_t usage_start_time;      // 开始使用时间
    MessageType current_msg_type;   // 当前消息类型
} SystemControl;
// 全局系统状态
static SystemControl g_sys_ctrl = {
    .toilet_state = TOILET_FREE,
    .toilet_type = TOILET_UNISEX,
    .entry_side = SIDE_NONE,
    .sys_state = SYS_NORMAL,
    .toilet_id = 1,              
    .light_level = LIGHT_LEVEL_DEFAULT,
    .paper_sufficient = true,
    .force_open_pending = false,
    .force_timer = 0,
    .last_button_time = 0,
    .timeout_state = TIMEOUT_NONE,
    .usage_timer = 0,
    .emergency_timer = 0,
    .usage_start_time = 0,
    .current_msg_type = MSG_NORMAL
};

// 紧急响应超时回调（放在usage_timeout_callback之前）
static void emergency_response_callback(uintptr_t data)
{
    unused(data);
    
    osal_printk("Emergency response callback triggered, timeout_state=%d\n", 
                g_sys_ctrl.timeout_state);
    
    if (g_sys_ctrl.timeout_state == TIMEOUT_FIRST && 
        g_sys_ctrl.toilet_state == TOILET_IN_USE) {
        // 5秒内未响应，强制开门
        osal_printk("Emergency response timeout - force opening door, entry_side=%d\n", 
                    g_sys_ctrl.entry_side);
        
        // 设置系统为紧急状态
        g_sys_ctrl.sys_state = SYS_EMERGENCY;
        
        // 执行强制开门
        if (g_sys_ctrl.entry_side == SIDE_LEFT) {
            osal_printk("Opening left door\n");
            peri_open_left_door();
            oled_show_message("EMERGENCY OPEN L");
        } else if (g_sys_ctrl.entry_side == SIDE_RIGHT) {
            osal_printk("Opening right door\n");
            peri_open_right_door();
            oled_show_message("EMERGENCY OPEN R");
        } else {
            // 如果entry_side无效，默认开左门
            osal_printk("Invalid entry_side, opening left door by default\n");
            peri_open_left_door();
            oled_show_message("EMERGENCY OPEN");
        }
        
        // 保持紧急状态和蜂鸣，等待管理员处理
        // 不停止蜂鸣器，不重置状态
        
        // 发送紧急状态
        send_toilet_status();
        
        osal_msleep(2000);
        oled_show_status();
    } else {
        osal_printk("Emergency callback conditions not met: timeout_state=%d, toilet_state=%d\n",
                    g_sys_ctrl.timeout_state, g_sys_ctrl.toilet_state);
    }
}

// 使用超时回调函数（放在emergency_response_callback之后）
static void usage_timeout_callback(uintptr_t data)
{
    unused(data);
    
    if (g_sys_ctrl.toilet_state != TOILET_IN_USE) {
        return;
    }
    
    if (g_sys_ctrl.timeout_state == TIMEOUT_NONE) {
        // 第一次超时警报
        osal_printk("First usage timeout - emergency response\n");
        g_sys_ctrl.timeout_state = TIMEOUT_FIRST;
        g_sys_ctrl.current_msg_type = MSG_EMERGENCY;
        
        // 启动蜂鸣器长鸣
        peri_buzzer_start(0);
        
        // 显示警告信息
        oled_show_message("TIMEOUT WARNING!");
        
        // 停止当前的usage_timer（避免定时器冲突）
        if (g_sys_ctrl.usage_timer != 0) {
            uapi_timer_stop(g_sys_ctrl.usage_timer);
            uapi_timer_delete(g_sys_ctrl.usage_timer);
            g_sys_ctrl.usage_timer = 0;
        }
        
        // 创建并启动5秒紧急响应定时器
        uapi_timer_create(TIMER_INDEX, &g_sys_ctrl.emergency_timer);
        uapi_timer_start(g_sys_ctrl.emergency_timer, EMERGENCY_RESPONSE_TIME * 1000, 
                        emergency_response_callback, 0);
        
        // 发送紧急消息
        send_toilet_status();
        
    } else if (g_sys_ctrl.timeout_state == TIMEOUT_EMERGENCY) {
        // 第二次超时驱逐
        osal_printk("Second usage timeout - force eviction\n");
        g_sys_ctrl.timeout_state = TIMEOUT_SECOND;
        g_sys_ctrl.current_msg_type = MSG_TIMEOUT;
        
        // 持续蜂鸣驱逐
        peri_buzzer_start(0);
        
        // 显示驱逐信息
        oled_show_message("EVICTION MODE!");
        
        // 发送超时消息
        send_toilet_status();
    }
}
// 发送厕位状态消息
static void send_toilet_status(void) {
    uint8_t paper_status = g_sys_ctrl.paper_sufficient ? 0 : 1;
    uint8_t entry_side = (uint8_t)g_sys_ctrl.entry_side;
    uint8_t msg_type = (uint8_t)g_sys_ctrl.current_msg_type;
    
    // 发送扩展状态：ID + State + Entry_Side + Paper + Msg_Type
    sle_client_send_extended_status(
        g_sys_ctrl.toilet_id, 
        (uint8_t)g_sys_ctrl.toilet_state,
        entry_side,
        paper_status,
        msg_type
    );
    
    const char* side_str = (g_sys_ctrl.entry_side == SIDE_LEFT) ? "LEFT" : 
                          (g_sys_ctrl.entry_side == SIDE_RIGHT) ? "RIGHT" : "NONE";
    const char* msg_str = (g_sys_ctrl.current_msg_type == MSG_EMERGENCY) ? "EMERGENCY" :
                          (g_sys_ctrl.current_msg_type == MSG_TIMEOUT) ? "TIMEOUT" : "NORMAL";
    
    osal_printk("Status sent: ID=%d, State=%s, Side=%s, Paper=%s, Type=%s\n", 
                g_sys_ctrl.toilet_id,
                g_sys_ctrl.toilet_state ? "USED" : "FREE",
                side_str,
                g_sys_ctrl.paper_sufficient ? "OK" : "LOW",
                msg_str);
}

// OLED显示函数
static void oled_show_message(char *msg) {
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 20);
    ssd1306_DrawString(msg, Font_11x18, White);
    ssd1306_UpdateScreen();
}

static void oled_show_status(void) {
    char status_str[32] = {0};
    char type_str[8] = {0};
    
    // 厕位类型字符串
    switch (g_sys_ctrl.toilet_type) {
        case TOILET_MALE: strcpy(type_str, "M"); break;
        case TOILET_FEMALE: strcpy(type_str, "F"); break;
        default: strcpy(type_str, "U"); break;
    }
    
    ssd1306_Fill(Black);
    
    // 第一行：厕位信息
    snprintf(status_str, sizeof(status_str), "%s%d:%s", 
            type_str, g_sys_ctrl.toilet_id,
            g_sys_ctrl.toilet_state ? "USED" : "FREE");
    ssd1306_SetCursor(0, 0);
    ssd1306_DrawString(status_str, Font_7x10, White);
    
    // 第二行：厕纸状态
    snprintf(status_str, sizeof(status_str), "Paper:%s", 
            g_sys_ctrl.paper_sufficient ? "OK" : "LOW");
    ssd1306_SetCursor(0, 12);
    ssd1306_DrawString(status_str, Font_7x10, White);
    
    // 第三行：光照级别
    snprintf(status_str, sizeof(status_str), "Light:%d", g_sys_ctrl.light_level);
    ssd1306_SetCursor(0, 24);
    ssd1306_DrawString(status_str, Font_7x10, White);
    
    // 第四行：系统状态
    switch (g_sys_ctrl.sys_state) {
        case SYS_FORCE_OPEN:
            ssd1306_SetCursor(0, 36);
            ssd1306_DrawString("FORCE OPEN!", Font_7x10, White);
            break;
        case SYS_EMERGENCY:
            ssd1306_SetCursor(0, 36);
            ssd1306_DrawString("EMERGENCY", Font_7x10, White);
            break;
        default:
            if (g_sys_ctrl.entry_side != SIDE_NONE) {
                snprintf(status_str, sizeof(status_str), "Entry:%s", 
                        g_sys_ctrl.entry_side == SIDE_LEFT ? "LEFT" : "RIGHT");
                ssd1306_SetCursor(0, 36);
                ssd1306_DrawString(status_str, Font_7x10, White);
            }
            break;
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
    ssd1306_DrawString("Client Ready", Font_7x10, White);
    ssd1306_UpdateScreen();
    osal_msleep(2000);
    oled_show_status();
    osal_printk("OLED initialized\r\n");
}

// 初始化定时器
static void init_timer(void) {
    uapi_timer_init();
    uapi_timer_adapter(TIMER_INDEX, TIMER_1_IRQN, TIMER_PRIO);
}
// 处理左侧开门按钮
static void handle_left_button(void) {
    osal_printk("Left button pressed\r\n");
    
    if (g_sys_ctrl.toilet_type == TOILET_FEMALE) {
        oled_show_message("Access Denied");
        osal_msleep(1000);
        oled_show_status();
        return;
    }
    
    if (g_sys_ctrl.toilet_state == TOILET_IN_USE) {
        oled_show_message("Occupied");
        osal_msleep(1000);
        oled_show_status();
        return;
    }
    
    // 执行开门操作
    oled_show_message("Left Door Open");
    peri_open_left_door();
    
    g_sys_ctrl.toilet_state = TOILET_IN_USE;
    g_sys_ctrl.entry_side = SIDE_LEFT;
    g_sys_ctrl.usage_start_time = uapi_tcxo_get_ms();
    g_sys_ctrl.timeout_state = TIMEOUT_NONE;
    g_sys_ctrl.current_msg_type = MSG_NORMAL;
    
    // 启动使用时长定时器
    uapi_timer_create(TIMER_INDEX, &g_sys_ctrl.usage_timer);
    uapi_timer_start(g_sys_ctrl.usage_timer, USAGE_TIMEOUT_FIRST * 1000, 
                    usage_timeout_callback, 0);
    
    // 启动补光功能
    peri_auto_light_adjust(g_sys_ctrl.light_level);
    
    // 发送状态更新
    send_toilet_status();
    
    osal_msleep(2000);
    oled_show_status();
}

// 处理右侧开门按钮
static void handle_right_button(void) {
    osal_printk("Right button pressed\r\n");
    
    if (g_sys_ctrl.toilet_type == TOILET_MALE) {
        oled_show_message("Access Denied");
        osal_msleep(1000);
        oled_show_status();
        return;
    }
    
    if (g_sys_ctrl.toilet_state == TOILET_IN_USE) {
        oled_show_message("Occupied");
        osal_msleep(1000);
        oled_show_status();
        return;
    }
    
    // 执行开门操作
    oled_show_message("Right Door Open");
    peri_open_right_door();
    
    g_sys_ctrl.toilet_state = TOILET_IN_USE;
    g_sys_ctrl.entry_side = SIDE_RIGHT;
    g_sys_ctrl.usage_start_time = uapi_tcxo_get_ms();
    g_sys_ctrl.timeout_state = TIMEOUT_NONE;
    g_sys_ctrl.current_msg_type = MSG_NORMAL;
    
    // 启动使用时长定时器
    uapi_timer_create(TIMER_INDEX, &g_sys_ctrl.usage_timer);
    uapi_timer_start(g_sys_ctrl.usage_timer, USAGE_TIMEOUT_FIRST * 1000, 
                    usage_timeout_callback, 0);
    
    // 启动补光功能
    peri_auto_light_adjust(g_sys_ctrl.light_level);
    
    // 发送状态更新
    send_toilet_status();
    
    osal_msleep(2000);
    oled_show_status();
}
// 处理内部开门按钮
static void handle_internal_button(void) {
    osal_printk("Internal button pressed\r\n");
    
    if (g_sys_ctrl.toilet_state != TOILET_IN_USE) {
        oled_show_message("Not in use");
        osal_msleep(1000);
        oled_show_status();
        return;
    }
    
    // 停止所有定时器
    if (g_sys_ctrl.usage_timer != 0) {
        uapi_timer_stop(g_sys_ctrl.usage_timer);
        uapi_timer_delete(g_sys_ctrl.usage_timer);
        g_sys_ctrl.usage_timer = 0;
    }
    if (g_sys_ctrl.emergency_timer != 0) {
        uapi_timer_stop(g_sys_ctrl.emergency_timer);
        uapi_timer_delete(g_sys_ctrl.emergency_timer);
        g_sys_ctrl.emergency_timer = 0;
    }
    
    // 停止蜂鸣器
    peri_buzzer_stop();
    
    // 根据进入方向开门
    if (g_sys_ctrl.entry_side == SIDE_LEFT) {
        oled_show_message("Left Door Open");
        peri_open_left_door();
    } else if (g_sys_ctrl.entry_side == SIDE_RIGHT) {
        oled_show_message("Right Door Open");
        peri_open_right_door();
    }
    
    // 重置状态
    g_sys_ctrl.toilet_state = TOILET_FREE;
    g_sys_ctrl.entry_side = SIDE_NONE;
    g_sys_ctrl.timeout_state = TIMEOUT_NONE;
    g_sys_ctrl.current_msg_type = MSG_NORMAL;
    
    // 关闭补光
    peri_led_off();
    
    // 发送状态更新
    send_toilet_status();
    
    osal_msleep(2000);
    oled_show_status();
}
// 处理警报响应按钮
static void handle_emergency_button(void) {
    osal_printk("Emergency button pressed\r\n");
    
    if (g_sys_ctrl.force_open_pending) {
        // 取消强制开门
        osal_printk("Force open cancelled by emergency button\n");
        
        // 停止定时器和蜂鸣器
        if (g_sys_ctrl.force_timer != 0) {
            uapi_timer_stop(g_sys_ctrl.force_timer);
            uapi_timer_delete(g_sys_ctrl.force_timer);
            g_sys_ctrl.force_timer = 0;
        }
        peri_buzzer_stop();
        
        g_sys_ctrl.force_open_pending = false;
        g_sys_ctrl.sys_state = SYS_NORMAL;
        
        oled_show_message("Force Cancelled");
        osal_msleep(2000);
        oled_show_status();
    } else if (g_sys_ctrl.timeout_state == TIMEOUT_FIRST) {
        // 第一次超时期间按下按钮B，进入第二阶段
        osal_printk("Emergency button pressed during first timeout\n");
        
        // 停止紧急响应定时器
        if (g_sys_ctrl.emergency_timer != 0) {
            uapi_timer_stop(g_sys_ctrl.emergency_timer);
            uapi_timer_delete(g_sys_ctrl.emergency_timer);
            g_sys_ctrl.emergency_timer = 0;
        }
        
        // 停止蜂鸣器
        peri_buzzer_stop();
        
        g_sys_ctrl.timeout_state = TIMEOUT_EMERGENCY;
        g_sys_ctrl.current_msg_type = MSG_NORMAL;
        
        // **修复：重新创建并启动第二次超时定时器**
        if (g_sys_ctrl.usage_timer == 0) {
            uapi_timer_create(TIMER_INDEX, &g_sys_ctrl.usage_timer);
        }
        uapi_timer_start(g_sys_ctrl.usage_timer, USAGE_TIMEOUT_SECOND * 1000, 
                        usage_timeout_callback, 0);
        
        oled_show_message("Response Received");
        send_toilet_status();
        
        osal_msleep(2000);
        oled_show_status();
    } else {
        oled_show_message("Emergency");
        osal_msleep(1000);
        oled_show_status();
    }
}

// 处理厕纸下发按钮
static void handle_tissue_button(void) {
    osal_printk("Tissue button pressed\r\n");
    
    oled_show_message("Tissue Delivery");
    
    // 启动电机0.5秒
    peri_motor_run(500);
    
    osal_msleep(2000);
    oled_show_status();
}

// 处理按钮D（停止紧急状态）
static void handle_button_d(void) {
    osal_printk("Button D pressed\r\n");
    
    if (g_sys_ctrl.timeout_state != TIMEOUT_NONE || 
        g_sys_ctrl.current_msg_type != MSG_NORMAL) {
        
        // 停止所有定时器
        if (g_sys_ctrl.usage_timer != 0) {
            uapi_timer_stop(g_sys_ctrl.usage_timer);
            uapi_timer_delete(g_sys_ctrl.usage_timer);
            g_sys_ctrl.usage_timer = 0;
        }
        if (g_sys_ctrl.emergency_timer != 0) {
            uapi_timer_stop(g_sys_ctrl.emergency_timer);
            uapi_timer_delete(g_sys_ctrl.emergency_timer);
            g_sys_ctrl.emergency_timer = 0;
        }
        
        // 停止蜂鸣器
        peri_buzzer_stop();
        
        // 恢复正常状态
        g_sys_ctrl.timeout_state = TIMEOUT_NONE;
        g_sys_ctrl.current_msg_type = MSG_NORMAL;
        g_sys_ctrl.sys_state = SYS_NORMAL;
        
        oled_show_message("Normal Restored");
        send_toilet_status();
        
        osal_msleep(2000);
        oled_show_status();
    } else {
        oled_show_message("Button D");
        osal_msleep(1000);
        oled_show_status();
    }
}

// 检查厕纸状态
static void check_paper_status(void) {
    bool previous_status = g_sys_ctrl.paper_sufficient;
    g_sys_ctrl.paper_sufficient = peri_is_paper_sufficient();
    
    // 如果状态改变，发送更新
    if (previous_status != g_sys_ctrl.paper_sufficient) {
        osal_printk("Paper status changed: %s\n", 
                    g_sys_ctrl.paper_sufficient ? "Sufficient" : "Low");
        send_toilet_status();
    }
}
// 服务端命令处理函数
static void handle_server_command(server_command_t *cmd) {
    if (cmd->toilet_id != g_sys_ctrl.toilet_id) {
        return;
    }
    
    osal_printk("Processing server command: type=%d, param=%d\n", cmd->cmd_type, cmd->param);
    
    switch (cmd->cmd_type) {
        case CMD_SET_TYPE:
            if (cmd->param <= TOILET_FEMALE) {
                g_sys_ctrl.toilet_type = (ToiletType)cmd->param;
                osal_printk("Toilet type set to %d\n", g_sys_ctrl.toilet_type);
                oled_show_status();
            }
            break;
            
        case CMD_FORCE_OPEN:
            // 直接强制开门，不等待响应
            if (g_sys_ctrl.toilet_state == TOILET_IN_USE) {
                osal_printk("Executing immediate force open\n");
                
                // 停止现有定时器
                if (g_sys_ctrl.usage_timer != 0) {
                    uapi_timer_stop(g_sys_ctrl.usage_timer);
                    uapi_timer_delete(g_sys_ctrl.usage_timer);
                    g_sys_ctrl.usage_timer = 0;
                }
                if (g_sys_ctrl.emergency_timer != 0) {
                    uapi_timer_stop(g_sys_ctrl.emergency_timer);
                    uapi_timer_delete(g_sys_ctrl.emergency_timer);
                    g_sys_ctrl.emergency_timer = 0;
                }
                
                // 停止蜂鸣器
                peri_buzzer_stop();
                
                // 直接开门
                if (g_sys_ctrl.entry_side == SIDE_LEFT) {
                    peri_open_left_door();
                    oled_show_message("Force Left Open");
                } else if (g_sys_ctrl.entry_side == SIDE_RIGHT) {
                    peri_open_right_door();
                    oled_show_message("Force Right Open");
                } else {
                    peri_open_left_door();
                    oled_show_message("Force Open");
                }
                
                // 重置状态
                g_sys_ctrl.toilet_state = TOILET_FREE;
                g_sys_ctrl.entry_side = SIDE_NONE;
                g_sys_ctrl.timeout_state = TIMEOUT_NONE;
                g_sys_ctrl.current_msg_type = MSG_NORMAL;
                g_sys_ctrl.sys_state = SYS_NORMAL;
                
                peri_led_off();
                send_toilet_status();
                
                osal_msleep(2000);
                oled_show_status();
            }
            break;
            
        case CMD_TISSUE_DELIVERY:
            osal_printk("Received tissue delivery command\n");
            handle_tissue_button();
            break;
            
        default:
            osal_printk("Unknown command type: %d\n", cmd->cmd_type);
            break;
    }
}

// 状态监控任务
static void *status_monitor_task(const char *arg) {
    unused(arg);
    
    while (1) {
        // 检查厕纸状态
        check_paper_status();
        
        // 如果厕位在使用中，进行补光调节
        if (g_sys_ctrl.toilet_state == TOILET_IN_USE) {
            peri_auto_light_adjust(g_sys_ctrl.light_level);
        }
        
        osal_msleep(STATUS_CHECK_INTERVAL);
    }
    
    return NULL;
}

// 主任务函数
static void *main_task(const char *arg) {
    unused(arg);
    osal_printk("\r\nSmart Toilet Client Started!\r\n");
    osal_printk("Toilet ID: %d\r\n\r\n", g_sys_ctrl.toilet_id);
    
    // 禁用看门狗
    uapi_watchdog_disable();
    
    // 初始化硬件
    init_timer();
    init_oled();
    
    if (peri_init() != ERRCODE_SUCC) {
        osal_printk("Peripheral init failed!\r\n");
        return NULL;
    }
    
    // 初始化星闪客户端
    sle_client_init();
    
    // 注册命令处理回调
    sle_client_register_command_handler(handle_server_command);
    
    // 创建状态监控任务
    osal_task *monitor_task_handle = NULL;
    osal_kthread_lock();
    monitor_task_handle = osal_kthread_create((osal_kthread_handler)status_monitor_task, 
                                             0, "StatusMonitor", TASK_STACK_SIZE);
    if (monitor_task_handle != NULL) {
        osal_kthread_set_priority(monitor_task_handle, TASK_PRIO + 1);
    }
    osal_kthread_unlock();

    while (1) {
        // 读取按钮状态
        button_type_t button = peri_get_button_state();
        uint32_t current_time = uapi_tcxo_get_ms();
        
        // 防抖处理
        if (button != BUTTON_NONE && 
            (current_time - g_sys_ctrl.last_button_time) > BUTTON_DEBOUNCE_TIME) {
            
            g_sys_ctrl.last_button_time = current_time;
            
            switch (button) {
                case BUTTON_A:
                    handle_internal_button();
                    break;
                case BUTTON_B:
                    handle_emergency_button();
                    break;
                case BUTTON_C:
                    handle_tissue_button();
                    break;
                case BUTTON_D:
                    handle_button_d();
                    break;
                case BUTTON_L:
                    handle_left_button();
                    break;
                case BUTTON_R:
                    handle_right_button();
                    break;
                default:
                    break;
            }
        }
        
        osal_msleep(DELAY_100MS);
    }
    
    peri_deinit();
    return NULL;
}

// 任务入口函数
static void system_entry(void) {
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)main_task, 
                                     0, "ClientMainTask", TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, TASK_PRIO);
    }
    osal_kthread_unlock();
}

// 启动系统
app_run(system_entry);