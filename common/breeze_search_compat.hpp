#pragma once

#include <switch.h>

#include <string>
#include <vector>

#include "dmntcht.h"
#include "search_types.hpp"

namespace breeze {

inline constexpr const char kFileMagic[] = "BREEZE00E";
inline constexpr const char kHeaderEnd[] = "HEADER@";
inline constexpr size_t kScreenshotBytes = 0x384000;

typedef enum {
    fulldump,
    address,
    address_data,
    from_to_32_main_to_heap,
    from_to_32_main_to_main,
    from_to_32_heap_to_heap,
    from_to_64,
    bookmark,
    search_mission,
    UNDEFINED,
    adv_search_list,
} breezefile_t;

typedef struct {
    u64 from, to;
} from_to;

typedef struct {
    u64 from, to;
} from_to32;

typedef struct {
    char MAGIC[10] = "BREEZE00E";
    breezefile_t filetype = UNDEFINED;
    char prefilename[100] = "";
    char bfilename[83] = "";
    u16 ptr_search_range = 0;
    u8 timetaken = 0;
    u8 bit_mask = 0;
    u8 current_level = 0;
    u32 new_targets = 0;
    u64 from_to_size = 0;
    Search_condition search_condition{};
    DmntCheatProcessMetadata Metadata = {0};
    bool compressed = false;
    bool has_screenshot = false;
    u64 dataSize = 0;
    char End[8] = "HEADER@";
} BreezeFileHeader_t;

bool ReadCandidateHeader(const std::string &path, BreezeFileHeader_t &outHeader,
                         std::string *errorOut = nullptr);

std::vector<std::string>
ListCandidateFiles(const std::vector<std::string> &roots = {
    "sdmc:/switch/Breeze/", "/switch/Breeze/"});

bool LoadLatestCandidateCondition(Search_condition &outCondition,
                                  std::string &outFilePath,
                                  std::string *errorOut = nullptr);

std::string SearchConditionSummary(const Search_condition &condition);

} // namespace breeze

