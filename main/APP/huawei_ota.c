#include "huawei_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "mqtt_client.h"
#include "nvs_flash.h"

static const char *TAG = "HUAWEI_OTA";

#define NVS_OTA_NS     "ota"
#define NVS_OTA_KEY    "success"

#define OTA_HTTP_BUF_SIZE  (2048)

static char s_ota_url[OTA_HTTP_BUF_SIZE];
static char s_ota_token[256];
/* 华为云 Root CA（你已经有） */
extern const uint8_t _binary_huaweicloud_iot_root_ca_list_pem_start[];
extern esp_mqtt_client_handle_t client;

bool ota_upgrade_flag_from_nvs = false;

void ota_clear_success_flag(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_OTA_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, NVS_OTA_KEY);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI("OTA", "OTA success flag cleared");
    }
}

void ota_set_success_flag(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_OTA_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        uint8_t flag = 1;
        nvs_set_u8(nvs, NVS_OTA_KEY, flag);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI("OTA", "OTA success flag written to NVS");
    }
}
void ota_read_success_flag(void)
{
    nvs_handle_t nvs;
    uint8_t flag = 0;

    if (nvs_open(NVS_OTA_NS, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, NVS_OTA_KEY, &flag);
        nvs_close(nvs);
    }

    ota_upgrade_flag_from_nvs = (flag == 1);
}

void huawei_ota_set_token(const char *token)
{
    if (!token) return;
    memset(s_ota_token, 0, sizeof(s_ota_token));
    strncpy(s_ota_token, token, sizeof(s_ota_token) - 1);
}

static int ota_mqtt_publish(const char *topic, const char *payload)
{
    if (!client) {
        ESP_LOGE("MQTT", "mqtt client not ready");
        return -1;
    }

    int msg_id = esp_mqtt_client_publish(
        client,
        topic,
        payload,
        0,      // len = strlen(payload)
        1,      // qos = 1（华为云 OTA 推荐）
        0       // retain = 0
    );

    if (msg_id < 0) {
        ESP_LOGE("MQTT", "publish failed");
    }

    return msg_id;
}

void huawei_ota_report_success(void)
{
    char payload[256];

    snprintf(payload, sizeof(payload),
        "{"
          "\"object_device_id\":\"%s\","
          "\"services\":[{"
            "\"service_id\":\"$ota\","
            "\"event_type\":\"upgrade_progress_report\","
            "\"event_time\":\"\","
            "\"paras\":{"
              "\"result_code\":0,"
              "\"progress\":100,"
              "\"version\":\"%s\","
              "\"description\":\"OTA success\""
            "}"
          "}]"
        "}",
        "696af323c9429d337f28b80f_0000",  // object_device_id
        FW_VERSION
    );

    ota_mqtt_publish(
        "$oc/devices/696af323c9429d337f28b80f_0000/sys/events/up",
        payload
    );
}

static void huawei_ota_report_progress(int progress)
{
    char payload[256];

    snprintf(payload, sizeof(payload),
        "{"
          "\"object_device_id\":\"%s\","
          "\"services\":[{"
            "\"service_id\":\"$ota\","
            "\"event_type\":\"upgrade_progress_report\","
            "\"event_time\":\"\","
            "\"paras\":{"
              "\"result_code\":0,"
              "\"progress\":%d,"
              "\"version\":\"%s\","
              "\"description\":\"OTA in progress\""
            "}"
          "}]"
        "}",
        "696af323c9429d337f28b80f_0000",  // object_device_id
        progress,
        FW_VERSION
    );

    ota_mqtt_publish(
        "$oc/devices/696af323c9429d337f28b80f_0000/sys/events/up",
        payload
    );
}
static void huawei_ota_report_fail(const char *reason)
{
    char payload[256];

    snprintf(payload, sizeof(payload),
        "{"
          "\"object_device_id\":\"%s\","
          "\"services\":[{"
            "\"service_id\":\"$ota\","
            "\"event_type\":\"upgrade_progress_report\","
            "\"event_time\":\"\","
            "\"paras\":{"
              "\"result_code\":-1,"
              "\"progress\":0,"
              "\"version\":\"%s\","
              "\"description\":\"OTA failed: %s\""
            "}"
          "}]"
        "}",
        "696af323c9429d337f28b80f_0000",  // object_device_id
        FW_VERSION,
        reason ? reason : "unknown"
    );

    ota_mqtt_publish(
        "$oc/devices/696af323c9429d337f28b80f_0000/sys/events/up",
        payload
    );
}

static void huawei_ota_task(void *arg)
{
    esp_err_t err;
    int total = 0;
    int read_len = 0;
    int last_progress = -1;

    ESP_LOGI(TAG, "Final OTA URL: %s", s_ota_url);

    esp_http_client_config_t cfg = {
        .url = s_ota_url,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,

        .cert_pem = (const char *)_binary_huaweicloud_iot_root_ca_list_pem_start,
        .skip_cert_common_name_check = true,
        .use_global_ca_store = false,

        .timeout_ms = 30000,
        .buffer_size = OTA_HTTP_BUF_SIZE,
        .buffer_size_tx = OTA_HTTP_BUF_SIZE,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        huawei_ota_report_fail("http client init failed");
        goto exit;
    }

    esp_http_client_set_method(client, HTTP_METHOD_GET);

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        huawei_ota_report_fail("http open failed");
        goto cleanup;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    int content_length = esp_http_client_get_content_length(client);

    ESP_LOGI(TAG, "HTTP status=%d content_length=%d", status, content_length);

    if (status != 200 || content_length <= 0) {
        huawei_ota_report_fail("invalid http response");
        goto cleanup;
    }

    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        huawei_ota_report_fail("no update partition");
        goto cleanup;
    }

    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        huawei_ota_report_fail("esp_ota_begin failed");
        goto cleanup;
    }

    uint8_t *buf = malloc(OTA_HTTP_BUF_SIZE);
    if (!buf) {
        huawei_ota_report_fail("malloc failed");
        esp_ota_abort(ota_handle);
        goto cleanup;
    }
    while (1) {
        read_len = esp_http_client_read(client, (char *)buf, OTA_HTTP_BUF_SIZE);

        if (read_len < 0) {
            ESP_LOGE(TAG, "❌ HTTP read error at %d/%d bytes", total, content_length);
            huawei_ota_report_fail("http read error");
            esp_ota_abort(ota_handle);
            free(buf);
            goto cleanup;
        }

        if (read_len == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        err = esp_ota_write(ota_handle, buf, read_len);
        if (err != ESP_OK) {
            huawei_ota_report_fail("esp_ota_write failed");
            esp_ota_abort(ota_handle);
            free(buf);
            goto cleanup;
        }

        total += read_len;

        int progress = (total * 100) / content_length;
        if (progress != last_progress && progress % 5 == 0) {
            last_progress = progress;
            huawei_ota_report_progress(progress);
            
            /* ========== 📊 进度监控 ========== */
            ESP_LOGI(TAG, "📈 OTA Progress: %d%% (%d/%d bytes)", 
                    progress, total, content_length);
            
            if (progress % 20 == 0) {
                ESP_LOGI(TAG, "    Heap: %lu bytes (largest block: %lu)",
                        (unsigned long)esp_get_free_heap_size(),
                        (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            }
        }
    }

    free(buf);

    if (total < 4096) {
        ESP_LOGE(TAG, "❌ OTA image too small: %d bytes", total);
        huawei_ota_report_fail("image too small");
        esp_ota_abort(ota_handle);
        goto cleanup;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ esp_ota_end failed: %s", esp_err_to_name(err));
        huawei_ota_report_fail("esp_ota_end failed");
        goto cleanup;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ set_boot_partition failed: %s", esp_err_to_name(err));
        huawei_ota_report_fail("set boot partition failed");
        goto cleanup;
    }
    
    // 成功后确认回滚标志
    esp_ota_mark_app_valid_cancel_rollback();

    huawei_ota_report_success();
    
    /* ========== ✅ OTA 完成 ========== */
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "✅ OTA SUCCESS! Total: %d bytes", total);
    ESP_LOGI(TAG, "📌 Next boot: %s (slot %d)",
            update_partition->label, update_partition->subtype);
    ESP_LOGI(TAG, "📌 Rebooting in 1 second...");
    ESP_LOGI(TAG, "========================================");

    vTaskDelay(pdMS_TO_TICKS(1000));
    ota_set_success_flag();
    esp_restart();

cleanup:
    ESP_LOGW(TAG, "🔧 OTA cleanup: closing HTTP connection");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

exit:
    vTaskDelete(NULL);
}

esp_err_t huawei_ota_start(const char *url)
{
    if (!url) return ESP_ERR_INVALID_ARG;

    memset(s_ota_url, 0, sizeof(s_ota_url));
    strncpy(s_ota_url, url, sizeof(s_ota_url) - 1);

    /* ========== 📊 OTA 启动诊断 ========== */
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🚀 OTA START REQUEST");
    ESP_LOGI(TAG, "📌 URL: %.64s%s", s_ota_url, 
            strlen(s_ota_url) > 64 ? "..." : "");
    ESP_LOGI(TAG, "📌 Heap before task: %lu bytes (largest: %lu)",
            (unsigned long)esp_get_free_heap_size(),
            (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    // 优化：
    // 1. 栈从 8KB 降为 6KB（保留更多堆给文件操作）
    // 2. 优先级从 5 降为 3（避免抢占文件传输任务）
    TaskHandle_t ota_task = NULL;
    ota_task = xTaskCreate(huawei_ota_task, "huawei_ota", 6 * 1024, NULL, 3, NULL);
    
    if (ota_task == NULL) {
        ESP_LOGE(TAG, "❌ Failed to create OTA task!");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "✅ OTA task created");
    ESP_LOGI(TAG, "   Stack: 6 KB | Priority: 3");
    ESP_LOGI(TAG, "========================================");

    return ESP_OK;
}

