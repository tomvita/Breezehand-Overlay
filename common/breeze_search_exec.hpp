#pragma once

#include <cstddef>
#include <string>

#include "search_types.hpp"

namespace breeze {

struct SearchRunStats {
  u64 entriesWritten = 0;
  u64 bytesWritten = 0;
  u64 bytesScanned = 0;
  u32 secondsTaken = 0;
  size_t scanBufferBytes = 0;
};

bool RunStartSearch(const Search_condition &condition,
                    const std::string &outputStem, SearchRunStats &stats,
                    std::string *errorOut = nullptr);

bool RunContinueSearch(const Search_condition &condition,
                       const std::string &sourceCandidatePath,
                       const std::string &outputStem, SearchRunStats &stats,
                       std::string *errorOut = nullptr);

} // namespace breeze

