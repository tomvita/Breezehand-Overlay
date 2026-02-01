#pragma once

#include <switch.h>

typedef union {
    u8 _u8;
    s8 _s8;
    u16 _u16;
    s16 _s16;
    u32 _u32;
    s32 _s32;
    u64 _u64;
    u64 _u40:40;
    s64 _s64;
    float _f32;
    double _f64;
} searchValue_t;

typedef enum {
    SEARCH_TYPE_UNSIGNED_8BIT,
    SEARCH_TYPE_SIGNED_8BIT,
    SEARCH_TYPE_UNSIGNED_16BIT,
    SEARCH_TYPE_SIGNED_16BIT,
    SEARCH_TYPE_UNSIGNED_32BIT,
    SEARCH_TYPE_SIGNED_32BIT,
    SEARCH_TYPE_UNSIGNED_64BIT,
    SEARCH_TYPE_SIGNED_64BIT,
    SEARCH_TYPE_FLOAT,
    SEARCH_TYPE_DOUBLE,
    SEARCH_TYPE_POINTER,
    SEARCH_TYPE_UNSIGNED_40BIT,
    SEARCH_TYPE_HEX,
    SEARCH_TYPE_TEXT,
    SEARCH_TYPE_NONE
} searchType_t;
