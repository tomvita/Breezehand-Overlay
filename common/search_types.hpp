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
    // Breeze compatibility aliases. Values match SEARCH_TYPE_FLOAT/DOUBLE.
    SEARCH_TYPE_FLOAT_32BIT = SEARCH_TYPE_FLOAT,
    SEARCH_TYPE_FLOAT_64BIT = SEARCH_TYPE_DOUBLE,
    SEARCH_TYPE_HEX,
    SEARCH_TYPE_TEXT,
    SEARCH_TYPE_NONE
} searchType_t;

// Breeze search mode definitions (kept in Breeze order for binary compatibility).
typedef enum {
    SM_EQ,
    SM_NE,
    SM_GT,
    SM_LT,
    SM_GE,
    SM_LE,
    SM_RANGE_EQ,
    SM_BMEQ,
    SM_RANGE_LT,
    SM_MORE,
    SM_LESS,
    SM_DIFF,
    SM_SAME,
    SM_TWO_VALUE,
    SM_TWO_VALUE_PLUS,
    SM_STRING,
    SM_INC_BY,
    SM_DEC_BY,
    SM_EQ_plus,
    SM_EQ_plus_plus,
    SM_NONE,
    SM_DIFFB,
    SM_SAMEB,
    SM_MOREB,
    SM_LESSB,
    SM_NOTAB,
    SM_THREE_VALUE,
    SM_BIT_FLIP,
    SM_ADV,
    SM_GAP,
    SM_GAP_ALLOWANCE,
    SM_PTR,
    SM_NPTR,
    SM_NoDecimal,
    SM_Gen2_data,
    SM_Gen2_code,
    SM_GETB,
    SM_REBASE,
    SM_Target,
    SM_Pointer_and_OFFSET,
    SM_SKIP,
    SM_Aborted_Target,
    SM_Branch,
    SM_LDRx,
    SM_ADRP,
    SM_EOR,
    SM_GETBZ,
} searchMode_t;

typedef enum {
    search_step_primary,
    search_step_secondary,
    search_step_dump,
    search_step_dump_compare,
    search_step_none,
    search_target,
    search_step_dump_segment,
    search_step_save_memory_edit,
} search_step_t;

typedef struct {
    search_step_t search_step = search_step_primary;
    searchType_t searchType = SEARCH_TYPE_UNSIGNED_32BIT;
    searchValue_t searchValue_1 = {9};
    searchValue_t searchValue_2 = {0};
    searchMode_t searchMode = SM_EQ;
    char searchString[24] = "";
    searchValue_t searchValue_3 = {0};
    u8 searchStringLen = 0;
    bool searchStringHexmode = false;
} Search_condition;
