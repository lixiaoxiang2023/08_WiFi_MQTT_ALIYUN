
/*
 * obs_http_single.c
 * 量产级 ESP32 + 华为 OBS HTTP 客户端
 * 模式：MQTT下发临时URL，不在设备侧计算签名
 * 支持：下载 / 上传 / 桶文件列表查询
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define OBS_TAG "OBS_HTTP"
#define OBS_MAX_RETRY 3
#define OBS_RX_BUF   (4* 1024)
#define OBS_TX_BUF  (4 * 1024)

/* ===================== CA证书 ===================== */

// 将CA证书内容以字符串形式嵌入
// const char ca_cert[] = 
// "-----BEGIN CERTIFICATE-----\n"
// "MIIDXzCCAkegAwIBAgILBAAAAAABIVhTCKIwDQYJKoZIhvcNAQELBQAwTDEgMB4G\n"
// "A1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNp\n"
// "Z24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDkwMzE4MTAwMDAwWhcNMjkwMzE4\n"
// "MTAwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEG\n"
// "A1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI\n"
// "hvcNAQEBBQADggEPADCCAQoCggEBAMwldpB5BngiFvXAg7aEyiie/QV2EcWtiHL8\n"
// "RgJDx7KKnQRfJMsuS+FggkbhUqsMgUdwbN1k0ev1LKMPgj0MK66X17YUhhB5uzsT\n"
// "gHeMCOFJ0mpiLx9e+pZo34knlTifBtc+ycsmWQ1z3rDI6SYOgxXG71uL0gRgykmm\n"
// "KPZpO/bLyCiR5Z2KYVc3rHQU3HTgOu5yLy6c+9C7v/U9AOEGM+iCK65TpjoWc4zd\n"
// "QQ4gOsC0p6Hpsk+QLjJg6VfLuQSSaGjlOCZgdbKfd/+RFO+uIEn8rUAVSNECMWEZ\n"
// "XriX7613t2Saer9fwRPvm2L7DWzgVGkWqQPabumDk3F2xmmFghcCAwEAAaNCMEAw\n"
// "DgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFI/wS3+o\n"
// "LkUkrk1Q+mOai97i3Ru8MA0GCSqGSIb3DQEBCwUAA4IBAQBLQNvAUKr+yAzv95ZU\n"
// "RUm7lgAJQayzE4aGKAczymvmdLm6AC2upArT9fHxD4q/c2dKg8dEe3jgr25sbwMp\n"
// "jjM5RcOO5LlXbKr8EpbsU8Yt5CRsuZRj+9xTaGdWPoO4zzUhw8lo/s7awlOqzJCK\n"
// "6fBdRoyV3XpYKBovHd7NADdBj+1EbddTKJd+82cEHhXXipa0095MJ6RMG3NzdvQX\n"
// "mcIfeg7jLQitChws/zyrVQ4PkX4268NXSb7hLi18YIvDQVETI53O9zJrlAGomecs\n"
// "Mx86OyXShkDOOyyGeMlhLxS67ttVb9+E7gUJTb0o2HLO02JQZR7rkpeDMdmztcpH\n"
// "WD9f\n"
// "-----END CERTIFICATE-----\n"
// "-----BEGIN CERTIFICATE-----\n"
// "MIIFgzCCA2ugAwIBAgIORea7A4Mzw4VlSOb/RVEwDQYJKoZIhvcNAQEMBQAwTDEg\n"
// "MB4GA1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjYxEzARBgNVBAoTCkdsb2Jh\n"
// "bFNpZ24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMTQxMjEwMDAwMDAwWhcNMzQx\n"
// "MjEwMDAwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSNjET\n"
// "MBEGA1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCAiIwDQYJ\n"
// "KoZIhvcNAQEBBQADggIPADCCAgoCggIBAJUH6HPKZvnsFMp7PPcNCPG0RQssgrRI\n"
// "xutbPK6DuEGSMxSkb3/pKszGsIhrxbaJ0cay/xTOURQh7ErdG1rG1ofuTToVBu1k\n"
// "ZguSgMpE3nOUTvOniX9PeGMIyBJQbUJmL025eShNUhqKGoC3GYEOfsSKvGRMIRxD\n"
// "aNc9PIrFsmbVkJq3MQbFvuJtMgamHvm566qjuL++gmNQ0PAYid/kD3n16qIfKtJw\n"
// "LnvnvJO7bVPiSHyMEAc4/2ayd2F+4OqMPKq0pPbzlUoSB239jLKJz9CgYXfIWHSw\n"
// "1CM69106yqLbnQneXUQtkPGBzVeS+n68UARjNN9rkxi+azayOeSsJDa38O+2HBNX\n"
// "k7besvjihbdzorg1qkXy4J02oW9UivFyVm4uiMVRQkQVlO6jxTiWm05OWgtH8wY2\n"
// "SXcwvHE35absIQh1/OZhFj931dmRl4QKbNQCTXTAFO39OfuD8l4UoQSwC+n+7o/h\n"
// "bguyCLNhZglqsQY6ZZZZwPA1/cnaKI0aEYdwgQqomnUdnjqGBQCe24DWJfncBZ4n\n"
// "WUx2OVvq+aWh2IMP0f/fMBH5hc8zSPXKbWQULHpYT9NLCEnFlWQaYw55PfWzjMpY\n"
// "rZxCRXluDocZXFSxZba/jJvcE+kNb7gu3GduyYsRtYQUigAZcIN5kZeR1Bonvzce\n"
// "MgfYFGM8KEyvAgMBAAGjYzBhMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8EBTAD\n"
// "AQH/MB0GA1UdDgQWBBSubAWjkxPioufi1xzWx/B/yGdToDAfBgNVHSMEGDAWgBSu\n"
// "bAWjkxPioufi1xzWx/B/yGdToDANBgkqhkiG9w0BAQwFAAOCAgEAgyXt6NH9lVLN\n"
// "nsAEoJFp5lzQhN7craJP6Ed41mWYqVuoPId8AorRbrcWc+ZfwFSY1XS+wc3iEZGt\n"
// "Ixg93eFyRJa0lV7Ae46ZeBZDE1ZXs6KzO7V33EByrKPrmzU+sQghoefEQzd5Mr61\n"
// "55wsTLxDKZmOMNOsIeDjHfrYBzN2VAAiKrlNIC5waNrlU/yDXNOd8v9EDERm8tLj\n"
// "vUYAGm0CuiVdjaExUd1URhxN25mW7xocBFymFe944Hn+Xds+qkxV/ZoVqW/hpvvf\n"
// "cDDpw+5CRu3CkwWJ+n1jez/QcYF8AOiYrg54NMMl+68KnyBr3TsTjxKM4kEaSHpz\n"
// "oHdpx7Zcf4LIHv5YGygrqGytXm3ABdJ7t+uA/iU3/gKbaKxCXcPu9czc8FB10jZp\n"
// "nOZ7BN9uBmm23goJSFmH63sUYHpkqmlD75HHTOwY3WzvUy2MmeFe8nI+z1TIvWfs\n"
// "pA9MRf/TuTAjB0yPEL+GltmZWrSZVxykzLsViVO6LAUP5MSeGbEYNNVMnbrt9x+v\n"
// "JJUEeKgDu+6B5dpffItKoZB0JaezPkvILFa9x8jvOOJckvB595yEunQtYQEgfn7R\n"
// "8k8HWV+LLUNS60YMlOH1Zkd5d9VUWx+tJDfLRVpOoERIyNiwmcUVhAn21klJwGW4\n"
// "5hpxbqCo8YLoRT5s1gLXCmeDBVrJpBA=\n"
// "-----END CERTIFICATE-----\n"
// "-----BEGIN CERTIFICATE-----\n"
// "MIIFWjCCA0KgAwIBAgISEdK7udcjGJ5AXwqdLdDfJWfRMA0GCSqGSIb3DQEBDAUA\n"
// "MEYxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9iYWxTaWduIG52LXNhMRwwGgYD\n"
// "VQQDExNHbG9iYWxTaWduIFJvb3QgUjQ2MB4XDTE5MDMyMDAwMDAwMFoXDTQ2MDMy\n"
// "MDAwMDAwMFowRjELMAkGA1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYt\n"
// "c2ExHDAaBgNVBAMTE0dsb2JhbFNpZ24gUm9vdCBSNDYwggIiMA0GCSqGSIb3DQEB\n"
// "AQUAA4ICDwAwggIKAoICAQCsrHQy6LNl5brtQyYdpokNRbopiLKkHWPd08EsCVeJ\n"
// "OaFV6Wc0dwxu5FUdUiXSE2te4R2pt32JMl8Nnp8semNgQB+msLZ4j5lUlghYruQG\n"
// "vGIFAha/r6gjA7aUD7xubMLL1aa7DOn2wQL7Id5m3RerdELv8HQvJfTqa1VbkNud\n"
// "316HCkD7rRlr+/fKYIje2sGP1q7Vf9Q8g+7XFkyDRTNrJ9CG0Bwta/OrffGFqfUo\n"
// "0q3v84RLHIf8E6M6cqJaESvWJ3En7YEtbWaBkoe0G1h6zD8K+kZPTXhc+CtI4wSE\n"
// "y132tGqzZfxCnlEmIyDLPRT5ge1lFgBPGmSXZgjPjHvjK8Cd+RTyG/FWaha/LIWF\n"
// "zXg4mutCagI0GIMXTpRW+LaCtfOW3T3zvn8gdz57GSNrLNRyc0NXfeD412lPFzYE\n"
// "+cCQYDdF3uYM2HSNrpyibXRdQr4G9dlkbgIQrImwTDsHTUB+JMWKmIJ5jqSngiCN\n"
// "I/onccnfxkF0oE32kRbcRoxfKWMxWXEM2G/CtjJ9++ZdU6Z+Ffy7dXxd7Pj2Fxzs\n"
// "x2sZy/N78CsHpdlseVR2bJ0cpm4O6XkMqCNqo98bMDGfsVR7/mrLZqrcZdCinkqa\n"
// "ByFrgY/bxFn63iLABJzjqls2k+g9vXqhnQt2sQvHnf3PmKgGwvgqo6GDoLclcqUC\n"
// "4wIDAQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNV\n"
// "HQ4EFgQUA1yrc4GHqMywptWU4jaWSf8FmSwwDQYJKoZIhvcNAQEMBQADggIBAHx4\n"
// "7PYCLLtbfpIrXTncvtgdokIzTfnvpCo7RGkerNlFo048p9gkUbJUHJNOxO97k4Vg\n"
// "JuoJSOD1u8fpaNK7ajFxzHmuEajwmf3lH7wvqMxX63bEIaZHU1VNaL8FpO7XJqti\n"
// "2kM3S+LGteWygxk6x9PbTZ4IevPuzz5i+6zoYMzRx6Fcg0XERczzF2sUyQQCPtIk\n"
// "pnnpHs6i58FZFZ8d4kuaPp92CC1r2LpXFNqD6v6MVenQTqnMdzGxRBF6XLE+0xRF\n"
// "FRhiJBPSy03OXIPBNvIQtQ6IbbjhVp+J3pZmOUdkLG5NrmJ7v2B0GbhWrJKsFjLt\n"
// "rWhV/pi60zTe9Mlhww6G9kuEYO4Ne7UyWHmRVSyBQ7N0H3qqJZ4d16GLuc1CLgSk\n"
// "ZoNNiTW2bKg2SnkheCLQQrzRQDGQob4Ez8pn7fXwgNNgyYMqIgXQBztSvwyeqiv5\n"
// "u+YfjyW6hY0XHgL+XVAEV8/+LbzvXMAaq7afJMbfc2hIkCwU9D9SGuTSyxTDYWnP\n"
// "4vkYxboznxSjBF25cfe1lNj2M8FawTSLfJvdkzrnE6JwYZ+vj+vYxXX4M2bUdGc6\n"
// "N3ec592kD3ZDZopD8p/7DEJ4Y9HiD2971KE9dJeFt0g5QdYg/NA6s/rob8SKunE3\n"
// "vouXsXgxT7PntgMTzlSdriVZzH81Xwj3QEUxeCp6\n"
// "-----END CERTIFICATE-----\n"
// ;


/* ===== CA PEM embedded symbols ===== */
extern const uint8_t _binary_huaweicloud_iot_root_ca_list_pem_start[];
extern const uint8_t _binary_huaweicloud_iot_root_ca_list_pem_end[];

/* ===================== 状态结构 ===================== */
typedef struct {
    FILE *fp;
    size_t content_length;
    size_t received;
    int retry_count;
    bool finished;
    bool success;
} obs_http_state_t;

static obs_http_state_t g_state;

/* ===================== 回调 ===================== */
static esp_err_t obs_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {

    case HTTP_EVENT_ERROR:
        ESP_LOGE(OBS_TAG, "HTTP_EVENT_ERROR");
        g_state.success = false;
        break;

    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(OBS_TAG, "Connected");
        break;

    case HTTP_EVENT_HEADERS_SENT:
        ESP_LOGI(OBS_TAG, "Headers sent");
        break;

    case HTTP_EVENT_ON_HEADER:
        // if (strcasecmp(evt->header_key, "Content-Length") == 0) {
        //     g_state.content_length = atoi(evt->header_value);
        //     ESP_LOGI(OBS_TAG, "Content-Length = %d", g_state.content_length);
        // }
        if (evt->header_key && evt->header_value) {
            ESP_LOGI(OBS_TAG, "HDR: %s = %s",
                    evt->header_key,
                    evt->header_value);

            if (strcasecmp(evt->header_key, "x-obs-request-id") == 0) {
                ESP_LOGI(OBS_TAG, "OBS CONFIRM ID: %s", evt->header_value);
            }
        }
        break;

    case HTTP_EVENT_ON_DATA:
        if (evt->data_len > 0 && g_state.fp) {
            fwrite(evt->data, 1, evt->data_len, g_state.fp);
            g_state.received += evt->data_len;
        }
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(OBS_TAG, "Transfer finished");
        g_state.finished = true;
        if (g_state.fp) {
            fclose(g_state.fp);
            g_state.fp = NULL;
        }
        if (g_state.content_length == 0 ||
            g_state.received == g_state.content_length) {
            g_state.success = true;
        } else {
            ESP_LOGE(OBS_TAG, "Size mismatch %d/%d",
                     g_state.received, g_state.content_length);
            g_state.success = false;
        }
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGW(OBS_TAG, "Disconnected");
        break;

    default:
        break;
    }

    return ESP_OK;
}

/* ===================== 内部工具 ===================== */
static void obs_state_reset(void)
{
    memset(&g_state, 0, sizeof(g_state));
}

/* ===================== 下载 ===================== */
esp_err_t obs_http_download(const char *url, const char *local_path)
{
    if (!url || !local_path) return ESP_ERR_INVALID_ARG;

    obs_state_reset();

    g_state.fp = fopen(local_path, "wb");
    if (!g_state.fp) {
        ESP_LOGE(OBS_TAG, "Failed to open file: %s", local_path);
        return ESP_FAIL;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = obs_http_event_handler,
        .cert_pem = (const char *)_binary_huaweicloud_iot_root_ca_list_pem_start,
        .timeout_ms = 15000,
        .buffer_size = OBS_RX_BUF,
        .buffer_size_tx = OBS_TX_BUF
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    for (g_state.retry_count = 0;
         g_state.retry_count < OBS_MAX_RETRY;
         g_state.retry_count++) {

        ESP_LOGI(OBS_TAG, "Download try %d", g_state.retry_count + 1);
        /* =================== 这里加 Header =================== */

        // 常规头
        esp_http_client_set_header(client, "Content-Type", "text/plain");

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK && g_state.success) {
            ESP_LOGI(OBS_TAG, "Download OK");
            esp_http_client_cleanup(client);
            return ESP_OK;
        }

        ESP_LOGW(OBS_TAG, "Retrying... err %d",err);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    esp_http_client_cleanup(client);
    return ESP_FAIL;
}

/* ===================== 上传 ===================== */
esp_err_t obs_http_upload(const char *url, const char *local_path)
{
    if (!url || !local_path) {
        ESP_LOGE(OBS_TAG, "Invalid args");
        return ESP_ERR_INVALID_ARG;
    }

    FILE *fp = fopen(local_path, "rb");
    if (!fp) {
        ESP_LOGE(OBS_TAG, "Open upload file failed: %s", local_path);
        return ESP_FAIL;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    ESP_LOGI(OBS_TAG, "Uploading %ld bytes to OBS", file_size);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,

        // 关键点：OBS + HTTPS + 长URL 必须大buffer
        .buffer_size = 4096,
        .buffer_size_tx = 4096,

        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = obs_http_event_handler,
        .cert_pem = (const char *)_binary_huaweicloud_iot_root_ca_list_pem_start,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(OBS_TAG, "HTTP client init failed");
        fclose(fp);
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_PUT);
        // 常规头
    esp_http_client_set_header(client, "Content-Type", "text/plain");

    esp_err_t err = esp_http_client_open(client, file_size);
    if (err != ESP_OK) {
        ESP_LOGE(OBS_TAG, "Client open failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    uint8_t buf[1024];
    int total_sent = 0;

    while (1) {
        int r = fread(buf, 1, sizeof(buf), fp);
        if (r <= 0) break;

        int w = esp_http_client_write(client, (const char *)buf, r);
        if (w <= 0) {
            ESP_LOGE(OBS_TAG, "Write failed");
            goto cleanup;
        }

        total_sent += w;
        ESP_LOGI(OBS_TAG, "Progress: %d / %ld", total_sent, file_size);
    }

    ESP_LOGI(OBS_TAG, "Upload finished %d/%ld", total_sent, file_size);

    esp_http_client_fetch_headers(client);

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(OBS_TAG, "HTTP STATUS = %d", status);

    if (status >= 400) {
        ESP_LOGE(OBS_TAG, "OBS ERROR BODY:");
        char errbuf[256];
        int r;
        while ((r = esp_http_client_read(client, errbuf, sizeof(errbuf) - 1)) > 0) {
            errbuf[r] = 0;
            ESP_LOGE(OBS_TAG, "%s", errbuf);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    fclose(fp);

    if (status != 200 && status != 201) {
        ESP_LOGE(OBS_TAG, "OBS rejected upload");
        return ESP_FAIL;
    }

    if (total_sent != file_size) {
        ESP_LOGE(OBS_TAG, "Size mismatch %d/%ld", total_sent, file_size);
        return ESP_FAIL;
    }

    ESP_LOGI(OBS_TAG, "OBS upload SUCCESS");
    return ESP_OK;

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    fclose(fp);
    return ESP_FAIL;
}


/* ===================== 桶文件查询 ===================== */
esp_err_t obs_http_list_bucket(const char *url, const char *save_path)
{
    return obs_http_download(url, save_path);
}

/* ===================== 示例 ===================== */
/*
MQTT回调中获取URL后调用：

obs_http_download(temp_url, "/spiffs/fw.bin");

上传：
obs_http_upload(upload_url, "/spiffs/log.txt");

查询桶：
obs_http_list_bucket(list_url, "/spiffs/list.xml");

*/
