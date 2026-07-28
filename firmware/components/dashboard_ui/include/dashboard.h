#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t dashboard_init(void);

void dashboard_update_main(void);

#ifdef __cplusplus
}
#endif
