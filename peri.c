#include "peri.h"
#include "pinctrl.h"
#include "pwm.h"
#include "adc.h"
#include "adc_porting.h"
#include "gpio.h"
#include "tcxo.h"
#include "soc_osal.h"
#include "common_def.h"

// 全局变量
static bool g_peri_initialized = false;
static uint8_t g_current_light_level = LIGHT_LEVEL_DEFAULT;

// PWM回调函数
static errcode_t buzzer_pwm_callback(uint8_t channel)
{
    unused(channel);
    return ERRCODE_SUCC;
}

static errcode_t led_pwm_callback(uint8_t channel)
{
    unused(channel);
    return ERRCODE_SUCC;
}

// 外设初始化
errcode_t peri_init(void)
{
    if (g_peri_initialized) {
        return ERRCODE_SUCC;
    }

    // 初始化ADC
    if (uapi_adc_init(ADC_CLOCK_NONE) != ERRCODE_SUCC) {
        return ERRCODE_FAIL;
    }

    // 初始化PWM
    if (uapi_pwm_init() != ERRCODE_SUCC) {
        uapi_adc_deinit();
        return ERRCODE_FAIL;
    }

    // 配置GPIO引脚
    uapi_pin_set_mode(MOTOR_PIN, 0);      // GPIO模式
    uapi_pin_set_mode(LEFT_DOOR_PIN, 0);  // GPIO模式
    uapi_pin_set_mode(RIGHT_DOOR_PIN, 0); // GPIO模式
    
    // 配置PWM引脚
    uapi_pin_set_mode(BUZZER_PIN, 1); // PWM模式
    uapi_pin_set_mode(LED_PIN, 1);    // PWM模式

    // 初始化GPIO为输出模式
    uapi_gpio_init(); // 修复：移除错误检查，因为此函数返回void
    
    gpio_select_core(MOTOR_PIN, CORES_APPS_CORE);
    gpio_select_core(LEFT_DOOR_PIN, CORES_APPS_CORE);
    gpio_select_core(RIGHT_DOOR_PIN, CORES_APPS_CORE);
    
    uapi_gpio_set_dir(MOTOR_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_dir(LEFT_DOOR_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_dir(RIGHT_DOOR_PIN, GPIO_DIRECTION_OUTPUT);

    // 设置初始状态为低电平
    uapi_gpio_set_val(MOTOR_PIN, GPIO_LEVEL_LOW);
    uapi_gpio_set_val(LEFT_DOOR_PIN, GPIO_LEVEL_LOW);
    uapi_gpio_set_val(RIGHT_DOOR_PIN, GPIO_LEVEL_LOW);

    g_peri_initialized = true;
    return ERRCODE_SUCC;
}

// 外设去初始化
errcode_t peri_deinit(void)
{
    if (!g_peri_initialized) {
        return ERRCODE_SUCC;
    }

    peri_buzzer_stop();
    peri_led_off();
    peri_gpio_set_low(MOTOR_PIN);
    peri_gpio_set_low(LEFT_DOOR_PIN);
    peri_gpio_set_low(RIGHT_DOOR_PIN);

    uapi_pwm_deinit();
    uapi_adc_deinit();
    uapi_gpio_deinit();

    g_peri_initialized = false;
    return ERRCODE_SUCC;
}

// 按钮检测
button_type_t peri_get_button_state(void)
{
    uint16_t voltage = 0;
    
    if (adc_port_read(BUTTON_ADC_CHANNEL, &voltage) != ERRCODE_SUCC) {
        return BUTTON_NONE;
    }

    if (voltage > BUTTON_A_THRESHOLD) {
        return BUTTON_A;
    } else if (voltage > BUTTON_B_THRESHOLD) {
        return BUTTON_B;
    } else if (voltage > BUTTON_C_THRESHOLD) {
        return BUTTON_C;
    } else if (voltage > BUTTON_D_THRESHOLD) {
        return BUTTON_D;
    } else if (voltage > BUTTON_L_THRESHOLD) {
        return BUTTON_L;
    } else if (voltage > BUTTON_R_THRESHOLD) {
        return BUTTON_R;
    }

    return BUTTON_NONE;
}

// 蜂鸣器控制
errcode_t peri_buzzer_start(uint32_t duration_ms)
{
    pwm_config_t cfg = {
        20000,      // freq - 频率
        10000,      // duty - 占空比50%
        0,          // offset_time - 相位偏移
        1,          // cycles - 周期数，1表示持续
        true        // is_loop - 是否循环
    };

    uapi_pwm_open(BUZZER_PWM_CHANNEL, &cfg);
    uapi_pwm_register_interrupt(BUZZER_PWM_GROUP, buzzer_pwm_callback);
    
    uint8_t channel_id = BUZZER_PWM_CHANNEL;
    uapi_pwm_set_group(BUZZER_PWM_GROUP, &channel_id, 1);
    uapi_pwm_start(BUZZER_PWM_GROUP);

    if (duration_ms > 0) {
        // 创建定时任务来停止蜂鸣器
        osal_msleep(duration_ms);
        peri_buzzer_stop();
    }

    return ERRCODE_SUCC;
}

errcode_t peri_buzzer_stop(void)
{
    uapi_pwm_close(BUZZER_PWM_CHANNEL);
    return ERRCODE_SUCC;
}

// LED灯控制
errcode_t peri_led_set_brightness(uint8_t level)
{
    if (level < LIGHT_LEVEL_MIN || level > LIGHT_LEVEL_MAX) {
        return ERRCODE_FAIL;
    }

    // 计算占空比：level 1-5 对应 20%-100% 占空比
    uint32_t duty = (level * 4000) + 4000; // 8000-20000 范围

    pwm_config_t cfg = {
        20000,      // freq - 频率
        duty,       // duty - 占空比
        0,          // offset_time - 相位偏移
        1,          // cycles - 周期数，1表示持续
        true        // is_loop - 是否循环
    };

    uapi_pwm_open(LED_PWM_CHANNEL, &cfg);
    uapi_pwm_register_interrupt(LED_PWM_GROUP, led_pwm_callback);
    
    uint8_t channel_id = LED_PWM_CHANNEL;
    uapi_pwm_set_group(LED_PWM_GROUP, &channel_id, 1);
    uapi_pwm_start(LED_PWM_GROUP);

    g_current_light_level = level;
    return ERRCODE_SUCC;
}

errcode_t peri_led_off(void)
{
    uapi_pwm_close(LED_PWM_CHANNEL);
    return ERRCODE_SUCC;
}

// GPIO控制
errcode_t peri_gpio_set_high(uint8_t pin, uint32_t duration_ms)
{
    uapi_gpio_set_val(pin, GPIO_LEVEL_HIGH);
    
    if (duration_ms > 0) {
        osal_msleep(duration_ms);
        uapi_gpio_set_val(pin, GPIO_LEVEL_LOW);
    }
    
    return ERRCODE_SUCC;
}

errcode_t peri_gpio_set_low(uint8_t pin)
{
    uapi_gpio_set_val(pin, GPIO_LEVEL_LOW);
    return ERRCODE_SUCC;
}

// 开门控制
errcode_t peri_open_left_door(void)
{
    return peri_gpio_set_high(LEFT_DOOR_PIN, 1000); // 1秒高电平
}

errcode_t peri_open_right_door(void)
{
    return peri_gpio_set_high(RIGHT_DOOR_PIN, 1000); // 1秒高电平
}

// 电机控制
errcode_t peri_motor_run(uint32_t duration_ms)
{
    return peri_gpio_set_high(MOTOR_PIN, duration_ms);
}

// 传感器读取
uint16_t peri_get_light_sensor(void)
{
    uint16_t voltage = 0;
    uint32_t total = 0;
    
    // 多次采样取平均值
    for (int i = 0; i < LIGHT_SENSOR_SAMPLES; i++) {
        if (adc_port_read(LIGHT_ADC_CHANNEL, &voltage) == ERRCODE_SUCC) {
            total += voltage;
        }
        osal_msleep(10);
    }
    
    return total / LIGHT_SENSOR_SAMPLES;
}

uint16_t peri_get_paper_sensor(void)
{
    uint16_t voltage = 0;
    adc_port_read(PAPER_ADC_CHANNEL, &voltage);
    return voltage;
}

bool peri_is_paper_sufficient(void)
{
    uint16_t voltage = peri_get_paper_sensor();
    return voltage < PAPER_LOW_THRESHOLD;
}

// 补光功能
errcode_t peri_auto_light_adjust(uint8_t light_level)
{
    if (light_level < LIGHT_LEVEL_MIN || light_level > LIGHT_LEVEL_MAX) {
        return ERRCODE_FAIL;
    }

    uint16_t light_sensor_value = peri_get_light_sensor();
    
    // 根据光照传感器值和设定级别调整LED亮度
    // 光照值越低，需要的补光越强
    // 这里使用简单的算法：基础亮度 + 补偿亮度
    
    uint8_t base_brightness = light_level;
    uint8_t compensation = 0;
    
    // 根据环境光照调整补偿
    if (light_sensor_value < 500) {        // 很暗
        compensation = 2;
    } else if (light_sensor_value < 1000) { // 较暗
        compensation = 1;
    } else if (light_sensor_value < 2000) { // 一般
        compensation = 0;
    } else {                               // 较亮，减少补光
        compensation = -1;
    }
    
    uint8_t final_brightness = base_brightness + compensation;
    
    // 限制在有效范围内
    if (final_brightness < LIGHT_LEVEL_MIN) {
        final_brightness = LIGHT_LEVEL_MIN;
    } else if (final_brightness > LIGHT_LEVEL_MAX) {
        final_brightness = LIGHT_LEVEL_MAX;
    }
    
    return peri_led_set_brightness(final_brightness);
}