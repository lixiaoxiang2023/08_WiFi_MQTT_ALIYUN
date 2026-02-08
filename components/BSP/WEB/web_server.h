#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

void web_server_start(void);
void web_server_stop(void);

#ifdef __cplusplus
}
#endif
