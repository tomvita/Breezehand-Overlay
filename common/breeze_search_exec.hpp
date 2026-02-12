#pragma once

#include <atomic>
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
  size_t primaryBufferBytes = 0;
  size_t secondaryBufferBytes = 0;
  size_t outputBufferBytes = 0;
  u8 bufferCount = 0;
  bool aborted = false;
};

struct SearchRunControl {
  std::atomic<bool> *pauseRequested = nullptr;
  std::atomic<bool> *abortRequested = nullptr;
  std::atomic<u64> *progressCurrent = nullptr;
  std::atomic<u64> *progressTotal = nullptr;
  std::atomic<bool> *isPaused = nullptr;
};

bool RunStartSearch(const Search_condition &condition,
                    const std::string &outputStem, SearchRunStats &stats,
                    std::string *errorOut = nullptr,
                    const SearchRunControl *control = nullptr);

bool RunContinueSearch(const Search_condition &condition,
                       const std::string &sourceCandidatePath,
                       const std::string &outputStem, SearchRunStats &stats,
                       std::string *errorOut = nullptr,
                       const SearchRunControl *control = nullptr);

} // namespace breeze

