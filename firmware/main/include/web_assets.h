#pragma once
#include <stddef.h>

typedef struct {
    const char *path;
    const char *mime;
    size_t       len;
    size_t       compressed_len;
    const unsigned char *data;
} web_asset_t;

static const web_asset_t web_assets[] = {};

static const size_t web_assets_count = 0;
