/**
 ****************************************************************************************************
* @file        main.c
* @author      正点原子团队(ALIENTEK)
* @version     V1.0
* @date        2023-08-26
* @brief       WIFI Aliyun实验
* @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
****************************************************************************************************
* @attention
*
* 实验平台:正点原子 ESP32-S3 开发板
* 在线视频:www.yuanzige.com
* 技术论坛:www.openedv.com
* 公司网址:www.alientek.com
* 购买地址:openedv.taobao.com
*
****************************************************************************************************
*/
#include "esp_system.h"
#include "nvs_flash.h"
#include "lcd.h"
#include "wifi_config.h"
#include "lwip_mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "firmware_storage.h"
#include "tud_flash.h"
#include "huawei_ota.h"
#include "esp_heap_caps.h"
#include "key_scan.h"
#include "mqtt_client.h"
#include "file_worker.h"
#include "web_server.h"          // 添加这一行
#include "web_server_handlers.h" // 添加这一行

static const char *TAG = "MAIN";
i2c_obj_t i2c0_master;
QueueHandle_t lvgl_queue = NULL;
SemaphoreHandle_t global_spi2_mux = NULL;
void print_mem_info(const char *tag)
{
    ESP_LOGI(tag,
        "free heap: %d | min heap: %d | internal: %d | dma: %d | largest internal: %d",
        heap_caps_get_free_size(MALLOC_CAP_8BIT),
        heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_free_size(MALLOC_CAP_DMA),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void mem_monitor_task(void *arg)
{
    while (1)
    {
        print_mem_info("MEM_MON");
        ESP_LOGI("MEM_MON",
            "PSRAM free: %d",
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

#include "lv_port_lcd.h"
#include "lvgl.h"
void setup_ui(void) {
    // 获取当前活动屏幕
    lv_obj_t * screen = lv_scr_act();

    // 1. 创建一个标签 (Label)
   // lv_obj_t * label = lv_label_create(screen);
   // lv_label_set_text(label, "SL-1000 SYSTEM READY");
    //lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

    // 2. 创建一个简单的按钮
    lv_obj_t * btn = lv_btn_create(screen);
    lv_obj_set_size(btn, 120, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    
    // 给按钮加文字
    lv_obj_t * btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "START UPGRADE");
    lv_obj_center(btn_label);
}
#include "lvgl_manager.h"
void lcd_gpio_init(void)
{
    gpio_config_t io_conf = {};

    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

    // 输出IO统一配置
    io_conf.pin_bit_mask =
        (1ULL << LCD_NUM_WR) |
        (1ULL << LCD_NUM_CS) |
        (1ULL << LCD_NUM_PWR);

    gpio_config(&io_conf);

    // ===== 默认电平 =====
    //gpio_set_level(LCD_NUM_PWR, 1);   // 背光/电源打开
}
void app_main(void)
{
    esp_err_t ret;
    char logo_str[64]= {0};
    lvgl_queue = xQueueCreate(10, sizeof(lvgl_msg_t));
    if (global_spi2_mux == NULL) {
        global_spi2_mux = xSemaphoreCreateRecursiveMutex();
    }
    ret = nvs_flash_init();
     //   ESP_ERROR_CHECK(nvs_flash_erase());

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // 在 Web 服务器和任何 NVS 读写操作之前初始化 NVS 句柄
    ESP_ERROR_CHECK(init_web_config_nvs()); // 【重要】添加这一行

    /* ================= OTA ================= */    
    ota_read_success_flag();
    ota_check_and_confirm();

    /* ================= 打印系统内存 ================= */
   // led_init();
   // i2c0_master = iic_init(I2C_NUM_0);
    spi2_init();
#ifdef LCD_1_47INCHL

#else   
    led_init();
    i2c0_master = iic_init(I2C_NUM_0);
    xl9555_init(i2c0_master);
#endif
  //  lcd_init();
#ifdef LCD_1_47INCHL
    lcd_gpio_init();
    spi3_init();
#endif
    lv_disp_t * disp = lv_port_lcd_init();

    if (disp == NULL) {
        ESP_LOGE("MAIN", "LVGL 驱动初始化失败！");
        return;
    }
    
    ESP_LOGI("MAIN", "soft version: %s",FW_VERSION);
    tud_usb_flash();
    firmware_storage_check(NULL);
    ui_create();
    /* ================= 本地任务 ================= */
    //xTaskCreate(mem_monitor_task, "mem_mon", 4096, NULL, 5, NULL);
    // xTaskCreatePinnedToCore(
    //     file_task_worker,
    //     "file_task_worker",
    //     6* 1024,
    //     NULL,
    //     5,
    //     NULL,
    //     1
    // );
    xTaskCreatePinnedToCore(
        usb_copy_task,
        "usb_copy",
        4096,
        NULL,
        4,
        NULL,
        1
    );

    //xTaskCreate(key_scan_task, "key_scan_task", 4* 1024, NULL, 5, NULL);
    // 将 key_scan_task 也固定到核心 1
    xTaskCreatePinnedToCore(
        key_scan_task, 
        "key_scan_task", 
        4096,           // 4KB 栈空间
        NULL, 
        5,              // 优先级保持为 5（高于下载任务的 4）
        NULL, 
        0               // 固定到 Core 1
    );
    /* ================= WiFi后台任务 ================= */
    xTaskCreate(
        wifi_background_task,
        "wifi_bg",
        4096,
        NULL,
        5,
        NULL
    );
    ota_queue = xQueueCreate(1, sizeof(ota_msg_t));

    // 2. 创建守护任务（常驻内存）
    xTaskCreate(ota_daemon_task, "ota_daemon", 8192, NULL, 5, NULL);

    xTaskCreatePinnedToCore(
        lvgl_task,
        "lvgl_task",
        4096,
        NULL,
        4,
        NULL,
        1
    );

}
