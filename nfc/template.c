#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "ohos_init.h"
#include "cmsis_os2.h"

#include "bsp_led.h"
#include "bsp_nfc.h"
#include "bsp_oled.h"
#include "hi_gpio.h"

// LED任务
osThreadId_t LED_Task_ID;

void LED_Task(void)
{
    led_init();

    while (1) 
    {
        LED(1); 
        usleep(200*1000);
        LED(0);
        usleep(200*1000);
    }
}

// LED任务创建
void led_task_create(void)
{
    osThreadAttr_t taskOptions;
    taskOptions.name = "LEDTask";
    taskOptions.attr_bits = 0;
    taskOptions.cb_mem = NULL;
    taskOptions.cb_size = 0;
    taskOptions.stack_mem = NULL;
    taskOptions.stack_size = 1024;
    taskOptions.priority = osPriorityNormal;

    LED_Task_ID = osThreadNew((osThreadFunc_t)LED_Task, NULL, &taskOptions);
    if (LED_Task_ID != NULL)
    {
        printf("ID = %d, Create LED_Task_ID is OK!\n", LED_Task_ID);
    }
}

// OLED任务
osThreadId_t OLED_Task_ID;

void OLED_Task(void)
{
    oled_init();
    
    oled_clear();
    oled_showstring(0, 0, "welcome to storehouse", 12);
    oled_showstring(0, 15, "Waiting for phone", 12);
    oled_refresh_gram();
    
    while (1) 
    {
        // 空循环，等待其他任务更新OLED显示
        usleep(100*1000); // 降低CPU占用
    }
}

// OLED任务创建
void oled_task_create(void)
{
    osThreadAttr_t taskOptions;
    taskOptions.name = "OLEDTask";
    taskOptions.attr_bits = 0;
    taskOptions.cb_mem = NULL;
    taskOptions.cb_size = 0;
    taskOptions.stack_mem = NULL;
    taskOptions.stack_size = 1024;
    taskOptions.priority = osPriorityNormal;

    OLED_Task_ID = osThreadNew((osThreadFunc_t)OLED_Task, NULL, &taskOptions);
    if (OLED_Task_ID != NULL)
    {
        printf("ID = %d, OLED_Task_ID Create OK!\n", OLED_Task_ID);
    }
}

// 控制任务
osThreadId_t NFC_Task_ID;

// 定义GPIO引脚号
#define OUTPUT_GPIO_PIN 1
#define OUTPUT_GPIO_PIN_0 0  // 新增GPIO0引脚定义

// 门禁状态变量
static bool access_granted = false;
// 记录上次检测到的状态
static bool last_card_detected = false;

// 初始化输出GPIO引脚
static void init_output_gpio(void)
{
    hi_gpio_init();
    
    // 初始化GPIO1
    hi_gpio_set_dir(OUTPUT_GPIO_PIN, HI_GPIO_DIR_OUT);
    hi_gpio_set_ouput_val(OUTPUT_GPIO_PIN, 0);
    
    // 初始化GPIO0
    hi_gpio_set_dir(OUTPUT_GPIO_PIN_0, HI_GPIO_DIR_OUT);
    hi_gpio_set_ouput_val(OUTPUT_GPIO_PIN_0, 0);
}

// 检查NDEF数据是否包含"123456"
static bool is_valid_card_data(uint8_t *ndefBuff, uint8_t len)
{
    const char *target_data = "123456";
    const uint8_t target_len = strlen(target_data);
    
    if (len < target_len) {
        printf("NDEF data too short, expected at least %d bytes\r\n", target_len);
        return false;
    }
    
    // 在整个NDEF数据中搜索目标数据
    for (uint8_t i = 0; i <= len - target_len; i++) {
        if (memcmp(&ndefBuff[i], target_data, target_len) == 0) {
            printf("Found valid data at offset %d: %.*s\r\n", i, target_len, &ndefBuff[i]);
            return true;
        }
    }
    
    printf("Valid data not found in NDEF buffer\r\n");
    return false;
}

// 更新门禁状态和GPIO输出
static void update_access_status(bool valid)
{
    if (valid && !access_granted) {
        // 密码正确且状态未更新
        access_granted = true;
        hi_gpio_set_ouput_val(OUTPUT_GPIO_PIN, 1);
        hi_gpio_set_ouput_val(OUTPUT_GPIO_PIN_0, 1);  // 同时设置GPIO0
        printf("Access granted! GPIO pins %d and %d set to HIGH\r\n", OUTPUT_GPIO_PIN, OUTPUT_GPIO_PIN_0);
    } else if (!valid && access_granted) {
        // 密码错误且状态为已授权
        access_granted = false;
        hi_gpio_set_ouput_val(OUTPUT_GPIO_PIN, 0);
        hi_gpio_set_ouput_val(OUTPUT_GPIO_PIN_0, 0);  // 同时设置GPIO0
        printf("Access denied! GPIO pins %d and %d set to LOW\r\n", OUTPUT_GPIO_PIN, OUTPUT_GPIO_PIN_0);
    }
}

// 显示卡片信息到OLED
static void display_card_info(uint8_t *ndefBuff, uint8_t len, bool is_valid)
{
    oled_clear();
    oled_showstring(0, 0, "Phone Data Detected", 12);
    
    if (is_valid) {
        oled_showstring(0, 15, "Valid Data: 123456", 12);
        oled_showstring(0, 30, "Access Granted", 12);
    } else {
        oled_showstring(0, 15, "Invalid Data", 12);
        oled_showstring(0, 30, "Access Denied", 12);
    }
    
    // 显示部分NDEF数据（仅作示例）
    char data_str[17] = {0};
    uint8_t display_len = (len > 16) ? 16 : len;
    memcpy(data_str, ndefBuff, display_len);
    oled_showstring(0, 45, "Data: ", 12);
    oled_showstring(32, 45, data_str, 12);
    
    oled_refresh_gram();
}

// 恢复初始显示
static void restore_initial_display(void)
{
    oled_clear();
    oled_showstring(0, 0, "welcome to storehouse", 12);
    oled_showstring(0, 15, "Waiting for phone", 12);
    oled_refresh_gram();
}

void NFC_Task(void)
{
    uint8_t ndefLen = 0;
    uint8_t ndef_Header = 0;
    uint32_t result_code = 0;
    uint8_t i = 0;
    bool is_valid = false;
    bool card_detected = false;
    
    nfc_init();
    init_output_gpio();

    while (1) 
    {
        ndefLen = 0;
        card_detected = false;
        
        // 读取NFC模块数据，检查是否有手机写入数据
        if ((result_code = NT3HReadHeaderNfc(&ndefLen, &ndef_Header)) != true) 
        {
            printf("NT3HReadHeaderNfc failed. result_code = %d\r\n", result_code);
            // 记录当前未检测到卡片
            card_detected = false;
            goto CHECK_DELAY;
        }

        ndefLen += NDEF_HEADER_SIZE;
        if (ndefLen <= NDEF_HEADER_SIZE) 
        {
            printf("ndefLen <= %d\r\n", NDEF_HEADER_SIZE);
            // 记录当前未检测到卡片
            card_detected = false;
            goto CHECK_DELAY;
        }
        
        uint8_t *ndefBuff = (uint8_t *)malloc(ndefLen + 1);
        if (ndefBuff == NULL) 
        {
            printf("ndefBuff malloc failed!\r\n");
            // 记录当前未检测到卡片
            card_detected = false;
            goto CHECK_DELAY;
        }

        if ((result_code = get_NDEFDataPackage(ndefBuff, ndefLen)) != HI_ERR_SUCCESS) 
        {
            printf("get_NDEFDataPackage failed. result_code = %d\r\n", result_code);
            free(ndefBuff);
            // 记录当前未检测到卡片
            card_detected = false;
            goto CHECK_DELAY;
        }

        card_detected = true;
        
        printf("Phone data detected! NDEF data (len=%d):\r\n", ndefLen);
        for (i = 0; i < ndefLen; i++) 
        {
            printf("0x%02X ", ndefBuff[i]);
            if ((i + 1) % 16 == 0) {
                printf("\r\n");
            }
        }
        printf("\r\n");
        
        is_valid = is_valid_card_data(ndefBuff, ndefLen);
        display_card_info(ndefBuff, ndefLen, is_valid);
        
        // 更新门禁状态
        update_access_status(is_valid);
        
        // 显示结果后恢复初始显示
        if (is_valid) {
            printf("Valid data detected from phone!\r\n");
            usleep(2000*1000);  // 成功时显示时间稍长
        } else {
            printf("Invalid data from phone!\r\n");
            usleep(1500*1000);  // 失败时显示时间稍短
        }

        free(ndefBuff);
        restore_initial_display();
        
CHECK_DELAY:
        // 检查是否有卡片状态变化
        if (last_card_detected && !card_detected) {
            // 卡片移开且之前是检测到卡片的状态
            printf("Phone removed, resetting access status\r\n");
            // 无论之前是否授权，都将门禁状态重置为未授权
            access_granted = false;
            hi_gpio_set_ouput_val(OUTPUT_GPIO_PIN, 0);
            hi_gpio_set_ouput_val(OUTPUT_GPIO_PIN_0, 0);  // 同时设置GPIO0
        }
        
        // 更新上次检测状态
        last_card_detected = card_detected;
        
        usleep(500*1000);  // 轮询间隔
    }
}

// 任务创建
void nfc_task_create(void)
{
    osThreadAttr_t taskOptions;
    taskOptions.name = "nfcTask";
    taskOptions.attr_bits = 0;
    taskOptions.cb_mem = NULL;
    taskOptions.cb_size = 0;
    taskOptions.stack_mem = NULL;
    taskOptions.stack_size = 1024*5;
    taskOptions.priority = osPriorityNormal;

    NFC_Task_ID = osThreadNew((osThreadFunc_t)NFC_Task, NULL, &taskOptions);
    if (NFC_Task_ID != NULL)
    {
        printf("ID = %d, NFC_Task_ID Create OK!\n", NFC_Task_ID);
    }
}

static void template_demo(void)
{
    led_task_create();
    oled_task_create();
    nfc_task_create();
}
SYS_RUN(template_demo);
