/**
 ****************************************************************************************************
 * @file        udp.c
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
#include <stdbool.h>
#include "huawei_ota.h"
#include "lwip_demo.h"
#include "cJSON.h"
#include "json_processor.h"
#include <esp_tls.h>
#include "xl9555.h"
#include "tud_flash.h"

int g_publish_flag = 0;/* 发布成功标志位 */
static const char *TAG = "MQTT_EXAMPLE";
char g_lcd_buff[100] = {0};

char g_strReadLocalFileName[64] = {0};//SPIFFS 读文件名
char g_strWriteLocalFileName[64] = {0};//SPIFFS 写文件名


/* ================= 华为云命令相关 ================= */
static esp_mqtt_client_handle_t g_hw_mqtt_client = NULL;
static char g_hw_request_id[64] = {0};

extern const uint8_t _binary_huaweicloud_iot_root_ca_list_pem_start[];
extern const uint8_t _binary_huaweicloud_iot_root_ca_list_pem_end[];


QueueHandle_t file_task_queue;
esp_mqtt_client_handle_t client = NULL;



/**
 * @brief       错误日记
 * @param       message     :错误消息
 * @param       error_code  :错误码
 * @retval      无
 */
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}
// 使用示例
void send_kv_json(esp_mqtt_client_handle_t client) {
    json_packet_t packet = {
        .device_id = "WirelessUsb",
        .timestamp = time(NULL),
        .pairs = NULL,
        .pair_count = 0
    };
    
    // 动态创建pairs数组
    kv_pair_t pairs[1] = {0};
    
    // 添加温度
    pairs[0].key = "A1CfgFile";
    pairs[0].is_string = true;
    pairs[0].value_str = "HI";

       // 创建根JSON对象
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create root JSON object");
        return;
    }
    
    // 创建services数组
    cJSON *services_array = cJSON_CreateArray();
    if (services_array == NULL) {
        ESP_LOGE(TAG, "Failed to create services array");
        cJSON_Delete(root);
        return;
    }
    
    // 将services数组添加到根对象
    cJSON_AddItemToObject(root, "services", services_array);
   


    cJSON *config = cJSON_CreateObject();
        // 将service对象添加到数组
    cJSON_AddItemToArray(services_array, config);

    cJSON_AddStringToObject(config, "service_id", "WirelessUsb");
    pairs[0].value_obj = config;
    
    packet.pairs = pairs;
    packet.pair_count = 1;
    
    cJSON *json = NULL;
    if (create_json_packet(&packet, &json) == ESP_OK) {
        char *json_str = cJSON_PrintUnformatted(json);
            // 打印生成的JSON（用于调试）
        ESP_LOGI(TAG, "Generated JSON: %s", json_str);
        if (json_str) {
            esp_mqtt_client_publish(client, DEVICE_PUBLISH_2, json_str, 0, 1, 0);
            free(json_str);
        }
        cJSON_Delete(json);
    }
}

// 生成并发送JSON数据
void send_json_data_services(esp_mqtt_client_handle_t client,const char *topic,char ctype, char* aData) {
    // 创建根JSON对象
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create root JSON object");
        return;
    }
    
    // 创建services数组
    cJSON *services_array = cJSON_CreateArray();
    if (services_array == NULL) {
        ESP_LOGE(TAG, "Failed to create services array");
        cJSON_Delete(root);
        return;
    }
    
    // 将services数组添加到根对象
    cJSON_AddItemToObject(root, "services", services_array);
    
    // 创建第一个service对象
    cJSON *service_obj = cJSON_CreateObject();
    if (service_obj == NULL) {
        ESP_LOGE(TAG, "Failed to create service object");
        cJSON_Delete(root);
        return;
    }
    
    // 将service对象添加到数组
    cJSON_AddItemToArray(services_array, service_obj);
    
    // 添加service_id
    cJSON_AddStringToObject(service_obj, "service_id", "WirelessUsb");
    
    // 创建properties对象
    cJSON *properties_obj = cJSON_CreateObject();
    if (properties_obj == NULL) {
        ESP_LOGE(TAG, "Failed to create properties object");
        cJSON_Delete(root);
        return;
    }
    
    // 添加properties到service对象
    cJSON_AddItemToObject(service_obj, "properties", properties_obj);
    
    if(ctype == 1)
    {
        // 添加LedSt属性（示例值设为0）
        cJSON_AddStringToObject(properties_obj, "A1CfgFile", aData);
    }
    else
    {
        // 添加LedSt属性（示例值设为0）
        cJSON_AddNumberToObject(properties_obj, "LedSt", aData[0]);
    }



    
    // 获取当前时间（可选）
    char event_time[32] = "";
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    struct tm *tm_info = localtime(&tv_now.tv_sec);
    if (tm_info != NULL) {
      //   格式化为ISO8601格式：YYYY-MM-DDTHH:MM:SSZ
        strftime(event_time, sizeof(event_time), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        cJSON_AddStringToObject(service_obj, "event_time", event_time);
   } else {
        // 如果获取时间失败，留空字符串
        cJSON_AddStringToObject(service_obj, "event_time", "");
    }
    
    // 转换为JSON字符串（紧凑格式）
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON");
        cJSON_Delete(root);
        return;
    }
    
    // 打印生成的JSON（用于调试）
    ESP_LOGI(TAG, "Generated JSON: %s", json_str);
    
    // 发布到MQTT
    if (client != NULL) {
        int msg_id = esp_mqtt_client_publish(client, 
                                            DEVICE_PUBLISH_2,
                                            json_str,
                                            0,   // 自动计算长度
                                            1,   // QoS 1
                                            0);  // 不保留
        
        ESP_LOGI(TAG, "Published JSON, msg_id=%d", msg_id);
    }
    
    // 清理内存
    free(json_str);
    cJSON_Delete(root);
}


char *build_file_manager_json(const char *object_device_id,
                              const char *even_type,
                              const char *event_time,
                              const char *file_name,
                              const char *hash_code,
                              int size)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    // "object_device_id"
    cJSON_AddStringToObject(root, "object_device_id", object_device_id);

    // "services": [ ... ]
    cJSON *services = cJSON_AddArrayToObject(root, "services");
    cJSON *svc = cJSON_CreateObject();
    cJSON_AddItemToArray(services, svc);

    cJSON_AddStringToObject(svc, "service_id", "$file_manager");
    cJSON_AddStringToObject(svc, "event_type", even_type);
    cJSON_AddStringToObject(svc, "event_time", event_time);

    // "paras": { ... }
    cJSON *paras = cJSON_AddObjectToObject(svc, "paras");
    cJSON_AddStringToObject(paras, "file_name", file_name);

    // "file_attributes": { ... }
    cJSON *file_attr = cJSON_AddObjectToObject(paras, "file_attributes");
    cJSON_AddStringToObject(file_attr, "hash_code", hash_code);
    cJSON_AddNumberToObject(file_attr, "size", size);

    // 输出字符串（需要 free）
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

// 生成并发送 events JSON数据
void send_json_data_events(esp_mqtt_client_handle_t client,const char *topic,const char *even_type, char* aData) {
    // 转换为JSON字符串（紧凑格式）
    char *json_str;

    json_str =  build_file_manager_json("",even_type,"",aData,"",1024);
    
    // 打印生成的JSON（用于调试）
    ESP_LOGI(TAG, "Generated JSON: %s", json_str);
    // 发布到MQTT
    if (client != NULL) {
        int msg_id = esp_mqtt_client_publish(client, 
                                            topic,
                                            json_str,
                                            0,   // 自动计算长度
                                            1,   // QoS 1
                                            0);  // 不保留
        
        ESP_LOGI(TAG, "Published JSON, msg_id=%d", msg_id);
    }
    
    // 清理内存
    free(json_str);
}


/* ================= 版本上报 ================= */
void huawei_ota_report_version(void)
{
    char topic[128];
    char payload[384];
    char event_time[32];

    /* 生成 UTC 时间：YYYYMMDDThhmmssZ */
    time_t now = time(NULL);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    strftime(event_time, sizeof(event_time),
             "%Y%m%dT%H%M%SZ", &tm_utc);

    /* MQTT topic */
    snprintf(topic, sizeof(topic),
             "$oc/devices/%s/sys/events/up",
             DEVICE_ID);

    /* JSON payload */
    snprintf(payload, sizeof(payload),
        "{"
          "\"object_device_id\":\"%s\","
          "\"services\":[{"
            "\"service_id\":\"$ota\","
            "\"event_type\":\"version_report\","
            "\"event_time\":\"%s\","
            "\"paras\":{"
              "\"sw_version\":\"%s\","
              "\"fw_version\":\"%s\""
            "}"
          "}]"
        "}",
        DEVICE_ID,
        event_time,
        FW_VERSION,   // 或 SW_VERSION
        FW_VERSION
    );

    int msg_id = esp_mqtt_client_publish(
        client,
        topic,
        payload,
        0,      // 自动计算长度
        1,      // qos
        0       // retain
    );

    ESP_LOGI("HUAWEI_OTA",
             "version_report sent, msg_id=%d, version=%s",
             msg_id, FW_VERSION);
}



void mqtt_cmd_handler(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    cJSON *services = cJSON_GetObjectItem(root, "services");
    if (!cJSON_IsArray(services)) goto end;

    cJSON *srv = cJSON_GetArrayItem(services, 0);
    if (!srv) goto end;

    const char *service_id =
        cJSON_GetStringValue(cJSON_GetObjectItem(srv, "service_id"));

    if (!service_id || strcmp(service_id, "$ota") != 0) goto end;

    cJSON *event_type = cJSON_GetObjectItem(srv, "event_type");
    if (!cJSON_IsString(event_type)) goto end;

    /* ================= 1️⃣ 版本查询 ================= */
    if (strcmp(event_type->valuestring, "version_query") == 0) {
        ESP_LOGI(TAG, "Huawei OTA version_query received");
        huawei_ota_report_version();
        goto end;
    }

    /* ================= 2️⃣ 固件升级 ================= */
    if (strcmp(event_type->valuestring, "firmware_upgrade") == 0 ||
        strcmp(event_type->valuestring, "firmware_upgrade_v2") == 0) {

        ESP_LOGI(TAG, "Huawei OTA upgrade received");

        cJSON *paras = cJSON_GetObjectItem(srv, "paras");
        if (!paras) goto end;

        const char *url =
            cJSON_GetStringValue(cJSON_GetObjectItem(paras, "url"));
        const char *token =
            cJSON_GetStringValue(cJSON_GetObjectItem(paras, "access_token"));

        if (!url) {
            ESP_LOGE(TAG, "OTA url missing");
            goto end;
        }

        if (token) {
            huawei_ota_set_token(token);
        }

       // ESP_LOGI(TAG, "Start OTA: %s", url);
        huawei_ota_start(url);
    }

end:
    cJSON_Delete(root);
}




/**
 * @brief       注册接收MQTT事件的事件处理程序
 * @param       handler_args:注册到事件的用户数据
 * @param       base        :处理程序的事件库
 * @param       event_id    :接收到的事件的id
 * @param       event_data  :事件的数据
 * @retval      无
 */
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        g_publish_flag = 1;
        lcd_show_string(30, 90, 200, 16, 16, "MQTT Connected", RED);

        msg_id = esp_mqtt_client_subscribe(client, DEVICE_SUBSCRIBE, 0);
        huawei_cmd_init(client);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        if (ota_upgrade_flag_from_nvs) {
            huawei_ota_report_success();
            ota_clear_success_flag();
        }


        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        lcd_show_string(30, 90, 200, 16, 16, "MQTT Disconnected", RED);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
    {
        /* ---------- topic ---------- */
        char topic_buf[128] = {0};
        memcpy(topic_buf, event->topic, event->topic_len);
        topic_buf[event->topic_len] = '\0';

        ESP_LOGI(TAG, "TOPIC=%s", topic_buf);

        /* request_id 解析 */
        const char *p = strstr(topic_buf, "request_id=");
        if (p) {
            memset(g_hw_request_id, 0, sizeof(g_hw_request_id));
            strncpy(g_hw_request_id,
                    p + strlen("request_id="),
                    sizeof(g_hw_request_id) - 1);

            ESP_LOGI(TAG, "g_hw_request_id: %s", g_hw_request_id);
        }

        /* ---------- data ---------- */
        char *msg = malloc(event->data_len + 1);
        if (!msg) {
            ESP_LOGE(TAG, "malloc failed");
            break;
        }

        memcpy(msg, event->data, event->data_len);
        msg[event->data_len] = '\0';

        ESP_LOGI(TAG, "DATA=%s", msg);

        if (xQueueSend(file_task_queue, &msg, 0) != pdPASS) {
            ESP_LOGE(TAG, "Queue full, drop MQTT msg");
            free(msg);
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        lcd_show_string(30, 90, 200, 16, 16, "MQTT ERROR", RED);
        break;

    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

extern void file_task_worker(void *arg);
/**
 * @brief       lwip_demo进程
 * @param       无
 * @retval      无
 */

void lwip_demo(void)
{
    char mqtt_publish_data[100] = {0};
    char mqtt_uri[256];
    int iCx = 0;  
    uint8_t key;

    snprintf(mqtt_uri, sizeof(mqtt_uri), "mqtts://%s:%d", HOST_NAME, HOST_PORT);

    /* 设置客户端的信息量 */ 
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .hostname = HOST_NAME,
                .port = HOST_PORT,
                .transport = MQTT_TRANSPORT_OVER_SSL,
            },
            .verification = {
                .certificate = (const char *)_binary_huaweicloud_iot_root_ca_list_pem_start,
                .skip_cert_common_name_check = false,
                .use_global_ca_store = false,  // 明确不使用全局存储
            },
        },
        .credentials = {
            .client_id = CLIENT_ID,
            .username = USER_NAME,
            .authentication = {
                .password = PASSWORD,
            },
        },
        .session = {
            .keepalive = 60,  // 设置合理的心跳
            .disable_clean_session = false,
        },
        .buffer = {
            .size = 8192,      // 发送/接收缓冲区大小
        //  .send_timeout_ms = 10000,  // 发送超时时间
        },
        .network = {
            .reconnect_timeout_ms = 10000,  // 网络重连超时
            .timeout_ms = 30000,            // 网络操作超时
        },
        .task = {
            .stack_size = 6144,
        //  .prio = 5,
        },
    };


    client = esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
 
    ESP_LOGI("MEM", "Internal free: %d",
    heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI("MEM", "PSRAM free: %d",
    heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

 
    esp_mqtt_client_start(client);
 
    while(1)
    {
        if (g_publish_flag == 1)
        {
            key = xl9555_key_scan(0);
            switch (key)
            {
                case KEY0_PRES:
                {
                    printf("KEY0 has been pressed \n");

                    memset(g_strReadLocalFileName,0,sizeof(g_strReadLocalFileName));
                    strcpy(g_strReadLocalFileName,SPIFFS_FILE_NAME);

                    //xl9555_pin_write(BEEP_IO, 0);
                    send_json_data_events(client,DEVICE_PUBLISH_EVENT,EVENT_UPLOAD,OBS_LOAD_FILE_NAME);
                    break;
                }
                case KEY1_PRES:
                {
                    printf("KEY1 has been pressed \n");
                    send_json_data_events(client,DEVICE_PUBLISH_EVENT,EVENT_DOWNLOAD,OBS_DOWN_FILE_NAME);
                    //xl9555_pin_write(BEEP_IO, 1);
                    break;
                }
                case KEY2_PRES:
                {
                    printf("KEY2 has been pressed \n");
                    //LED(0);
                    break;
                }
                case KEY3_PRES:
                {
                    printf("KEY3 has been pressed \n");
                    //LED(1);
                    break;
                }
                default:
                {
                    break;
                }
            }

            //LED_TOGGLE();
        }
        
        vTaskDelay(10);
    }

}


static void huawei_cmd_send_response(const char *request_id,
                                     int result_code,
                                     const char *result_msg)
{
    char topic[128];
    char payload[256];

    snprintf(topic, sizeof(topic),
             "$oc/devices/%s/sys/commands/response/request_id=%s",
             DEVICE_ID,
             request_id);

    snprintf(payload, sizeof(payload),
             "{"
             "\"result_code\":%d,"
             "\"response_name\":\"COMMAND_RESPONSE\","
             "\"paras\":{"
             "\"result\":\"%s\""
             "}"
             "}",
             result_code, result_msg);

    esp_mqtt_client_publish(g_hw_mqtt_client,
                            topic,
                            payload,
                            0,
                            1,
                            0);

    ESP_LOGI(TAG, "HW CMD RESP: %s", payload);
}

void huawei_cmd_init(esp_mqtt_client_handle_t client)
{
    g_hw_mqtt_client = client;
}

bool huawei_cmd_handle(const char *json)
{
    char buf[64] = {0};

    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    cJSON *cmd     = cJSON_GetObjectItem(root, "command_name");
    cJSON *service = cJSON_GetObjectItem(root, "service_id");
    cJSON *paras   = cJSON_GetObjectItem(root, "paras");

    if (!cJSON_IsString(cmd) ||
        !cJSON_IsString(service) ||
        !cJSON_IsObject(paras)) {
        cJSON_Delete(root);
        return false;
    }

    ESP_LOGI(TAG, "HW CMD RX: %s.%s",
             service->valuestring,
             cmd->valuestring);

    /* 只处理 WirelessUsb.A1Cmd */
    if (strcmp(service->valuestring, "WirelessUsb") != 0 ||
        strcmp(cmd->valuestring, "A1Cmd") != 0) {

        huawei_cmd_send_response(
            g_hw_request_id,
            1,
            "unsupported command"
        );

        cJSON_Delete(root);
        return true;
    }

    bool handled = false;

    /* ---------- DownloadFile ---------- */
    cJSON *download = cJSON_GetObjectItem(paras, "DownloadFile");
    if (cJSON_IsString(download)) {
        ESP_LOGI(TAG, "DownloadFile: %s", download->valuestring);

        if (client) {
            send_json_data_events(
                client,
                DEVICE_PUBLISH_EVENT,
                EVENT_DOWNLOAD,
                download->valuestring
            );
        }
        handled = true;
    }

    /* ---------- UploadFile ---------- */
    cJSON *upload = cJSON_GetObjectItem(paras, "UploadFile");
    if (cJSON_IsString(upload)) {
        ESP_LOGI(TAG, "UploadFile: %s", upload->valuestring);

        memset(buf,0,sizeof(buf));
        snprintf(buf, sizeof(buf), "%s/%s", USB_PATH, upload->valuestring);
        memset(g_strReadLocalFileName,0,sizeof(g_strReadLocalFileName));

        strcpy(g_strReadLocalFileName, buf);

      //  strcpy(g_strReadLocalFileName,SPIFFS_FILE_NAME);
        if (client) {
            send_json_data_events(
                client,
                DEVICE_PUBLISH_EVENT,
                EVENT_UPLOAD,
                upload->valuestring
            );
        }
        handled = true;
    }

    /* ---------- 回复云端 ---------- */
    if (handled) {
        huawei_cmd_send_response(
            g_hw_request_id,
            0,
            "success"
        );
    } else {
        huawei_cmd_send_response(
            g_hw_request_id,
            1,
            "no valid paras"
        );
    }

    cJSON_Delete(root);
    return true;
}





void huawei_ota_event_handler(esp_mqtt_client_handle_t client,
                              const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    cJSON *services = cJSON_GetObjectItem(root, "services");
    if (!cJSON_IsArray(services)) goto exit;

    cJSON *svc = cJSON_GetArrayItem(services, 0);
    if (!svc) goto exit;

    cJSON *service_id = cJSON_GetObjectItem(svc, "service_id");
    cJSON *event_type = cJSON_GetObjectItem(svc, "event_type");

    if (!cJSON_IsString(service_id) || !cJSON_IsString(event_type)) goto exit;

    /* 只处理 OTA */
    if (strcmp(service_id->valuestring, "$ota") != 0) goto exit;

    /* ===== 版本查询 ===== */
    if (strcmp(event_type->valuestring, "version_query") == 0) {
        ESP_LOGI(TAG, "OTA version query");

        char reply[512];

        snprintf(reply, sizeof(reply),
            "{"
            "\"object_device_id\":\"\","
            "\"services\":[{"
                "\"service_id\":\"$ota\","
                "\"event_type\":\"version_report\","
                "\"event_time\":\"\","
                "\"paras\":{"
                "\"sw_version\":\"%s\","
                "\"fw_version\":\"%s\""
                "}"
            "}]"
            "}",
            "V1.0.0",
            FW_VERSION
        );

        esp_mqtt_client_publish(
            client,
            "$oc/devices/"DEVICE_ID"/sys/events/up",
            reply,
            0,
            1,
            0
        );

        ESP_LOGI(TAG, "OTA version reported: %s", FW_VERSION);
    }


exit:
    cJSON_Delete(root);
}