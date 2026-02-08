/**
 ****************************************************************************************************
 * @file        wifi_config.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2023-08-26
 * @brief       网络连接配置
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

#ifndef __WIFI_CONFIG_H
#define __WIFI_CONFIG_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* WiFi 状态位 */
#define WIFI_CFG_CONNECTED_BIT   BIT0
#define WIFI_CFG_SC_DONE_BIT     BIT1

/* 获取内部 EventGroup（高级用法） */
EventGroupHandle_t wifi_config_get_event_group(void);
/* 声明函数 */
esp_err_t wifi_smartconfig_sta(void);
/* 阻塞等待 WiFi 连接成功（拿到 IP） */
void wifi_config_wait_connected(void);
#endif
