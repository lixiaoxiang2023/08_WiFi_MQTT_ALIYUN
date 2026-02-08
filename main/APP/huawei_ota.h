#pragma once

#include "esp_err.h"
#include "cJSON.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FW_VERSION "V1.0.0"
void huawei_ota_set_token(const char *token);
esp_err_t huawei_ota_start(const char *url);
//void huawei_ota_report_version(void);
void huawei_ota_report_success(void);
void ota_read_success_flag(void);
void ota_clear_success_flag(void);

extern bool ota_upgrade_flag_from_nvs;
#ifdef __cplusplus
}
#endif
