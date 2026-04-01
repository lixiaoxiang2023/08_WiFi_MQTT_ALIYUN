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

void app_main(void)
{
    esp_err_t ret;
    char logo_str[64]= {0};

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
    led_init();
    i2c0_master = iic_init(I2C_NUM_0);
    spi2_init();
    xl9555_init(i2c0_master);
    lcd_init();

    snprintf(logo_str, sizeof(logo_str), "Version: %s", FW_VERSION);
    lcd_show_string(30, 50, 200, 16, 16, logo_str, RED);
    lcd_show_string(30, 110, 200, 16, 16, "STA      ", RED);
    lcd_show_string(30, 130, 200, 16, 16, "                          ", RED);

    ESP_LOGI("MAIN", "soft version: %s",FW_VERSION);
    tud_usb_flash();
    firmware_storage_check(NULL);

    // 移除对 g_data_config 的直接操作，现在由 web_server_handlers.c 管理
    /*
    if (load_data_config(&g_data_config) == ESP_FAIL) {
        strcpy(g_data_config.local_file,USB_FILE_NAME);
        strcpy(g_data_config.upload_server,OBS_DOWN_FILE_NAME);
    }
    */

    /* ================= 本地任务 ================= */
    //xTaskCreate(mem_monitor_task, "mem_mon", 4096, NULL, 5, NULL);
    xTaskCreatePinnedToCore(
        file_task_worker,
        "file_task_worker",
        6* 1024,
        NULL,
        5,
        NULL,
        1
    );

    xTaskCreatePinnedToCore(
        usb_copy_task,
        "usb_copy",
        4096,
        NULL,
        4,
        NULL,
        0
    );
    xTaskCreate(key_scan_task, "key_scan_task", 4* 1024, NULL, 5, NULL);

    /* ================= WiFi后台任务 ================= */
    xTaskCreate(
        wifi_background_task,
        "wifi_bg",
        4096,
        NULL,
        5,
        NULL
    );
}

