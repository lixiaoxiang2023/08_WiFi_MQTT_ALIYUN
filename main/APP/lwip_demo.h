/**
 ****************************************************************************************************
 * @file        udp.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2023-08-26
 * @brief       LWIP实验
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

#ifndef __LWIP_DEMO_H
#define __LWIP_DEMO_H

#include <string.h>
#include <sys/socket.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_netif.h"
#include "led.h"
#include "lcd.h"
//#include "huawei_obs.h"
#include "obs_http.h"




// "device_id": "696af323c9429d337f28b80f_0000"
// "secret": "1b788c0f91c52fc3a0a6e843b1ddd7a3"
/* 用户需要根据设备信息完善以下宏定义中的三元组内容 */
//#define PRODUCT_KEY         "696af323c9429d337f28b80f_0000"                                                       /* ProductKey->阿里云颁发的产品唯一标识，11位长度的英文数字随机组合 */
//#define DEVICE_NAME         "696af323c9429d337f28b80f_0000"                                                       /* DeviceName->用户注册设备时生成的设备唯一编号，支持系统自动生成，也可支持用户添加自定义编号，产品维度内唯一  */
//#define DEVICE_SECRET       "1b788c0f91c52fc3a0a6e843b1ddd7a3"                                  /* DeviceSecret->设备密钥，与DeviceName成对出现，可用于一机一密的认证方案  */
/* MQTT地址与端口 */
#define HOST_NAME           "75b9ab3486.st1.iotda-device.cn-south-1.myhuaweicloud.com"
#define HOST_PORT           8883
#define CLIENT_ID           "696af323c9429d337f28b80f_0000_0_0_2026011809"
#define USER_NAME           "696af323c9429d337f28b80f_0000"
#define PASSWORD            "a5c59fbe63ea330c6f63ef3cf84e158850b172b831576af12e9dac914fd2fa5d"

// 主题（根据您的设备ID）
#define DEVICE_ID           "696af323c9429d337f28b80f_0000"
#define DEVICE_PUBLISH      "$oc/devices/"DEVICE_ID"/sys/messages/up"
#define DEVICE_PUBLISH_2    "$oc/devices/"DEVICE_ID"/sys/properties/report"
#define DEVICE_PUBLISH_EVENT    "$oc/devices/"DEVICE_ID"/sys/events/up"
#define DEVICE_SUBSCRIBE    "$oc/devices/"DEVICE_ID"/sys/messages/down"




#define SERVICE_ID     "$file_manager"
#define EVENT_DOWNLOAD  "get_download_url"
#define EVENT_UPLOAD     "get_upload_url"

#define SPIFFS_PATH   "/spiffs"//写入读取 SPIFFS 文件名称
#define USB_PATH    "/disk"//写入读取 SPIFFS 文件名称

#define SPIFFS_FILE_NAME    "/spiffs/123.txt"//写入读取 SPIFFS 文件名称
#define USB_FILE_NAME    "/disk/123.txt"//写入读取 SPIFFS 文件名称
#define OBS_LOAD_FILE_NAME    "23.txt"//OBS 上传文件名称
#define OBS_DOWN_FILE_NAME    "123.txt"//OBS 下载文件名称
//#define OBS_ENDPOINT      "obs.cn-south-1.myhuaweicloud.com"
//#define OBS_BUCKET_NAME   "lixx"
//#define OBS_ACCESS_KEY    "HPUAUYPWHPS6Y6FKQSOM"
//#define OBS_SECRET_KEY    "CIBQK6KYlXzYROnE9OzOTybzn1H7xFh9oE7p1RMs"
typedef enum {
    FILE_TASK_DOWNLOAD = 0,
    FILE_TASK_UPLOAD
} file_task_type_t;

typedef struct {
    file_task_type_t type;
    char url[512];
    char path[128];
} file_task_msg_t;
extern QueueHandle_t file_task_queue;
extern char g_strReadLocalFileName[64];
extern char g_strWriteLocalFileName[64];//SPIFFS 写文件名
/* ================= 华为云命令处理接口 ================= */
/* MQTT_EVENT_CONNECTED 后调用 */
void huawei_cmd_init(esp_mqtt_client_handle_t client);

/* MQTT_EVENT_DATA 中调用（仅喂 Topic） */
void huawei_cmd_feed_topic(const char *topic, int topic_len);

/* worker 线程中调用，返回 true 表示已处理 */
bool huawei_cmd_handle(const char *json);
void mqtt_cmd_handler(const char *json);
void lwip_demo(void);

#endif
