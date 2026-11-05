#pragma once
#include "app_config.h"
// NULL on success. Empty passwords retain the old value only for the same device/network.
const char *prov_form_parse(const char *body, const app_config_t *old,
                            app_config_t *out, bool *force);
