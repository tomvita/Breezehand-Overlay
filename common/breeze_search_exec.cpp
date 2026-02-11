#include "breeze_search_exec.hpp"

#include <algorithm>
#include <limits>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <ultra.hpp>

#include "breeze_search_compat.hpp"
#include "search_exec_template.hpp"

namespace breeze {
namespace {

constexpr size_t kFixedScanBuffer = 2 * 1024 * 1024;
constexpr size_t kOutputBuffer = 512 * 1024;
constexpr size_t kContinueInputBuffer = kFixedScanBuffer / 2;
constexpr size_t kContinueMemoryBuffer = kFixedScanBuffer / 2;

struct CandidateRecord {
  u64 address;
  u64 value;
};

size_t TypeByteWidth(searchType_t type) {
  switch (type) {
  case SEARCH_TYPE_UNSIGNED_8BIT:
  case SEARCH_TYPE_SIGNED_8BIT:
    return 1;
  case SEARCH_TYPE_UNSIGNED_16BIT:
  case SEARCH_TYPE_SIGNED_16BIT:
    return 2;
  case SEARCH_TYPE_UNSIGNED_32BIT:
  case SEARCH_TYPE_SIGNED_32BIT:
  case SEARCH_TYPE_FLOAT_32BIT:
    return 4;
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_SIGNED_64BIT:
  case SEARCH_TYPE_FLOAT_64BIT:
  case SEARCH_TYPE_POINTER:
  case SEARCH_TYPE_UNSIGNED_40BIT:
    return 8;
  default:
    return 4;
  }
}

size_t SearchStepIncrement(searchType_t type) {
  switch (type) {
  case SEARCH_TYPE_UNSIGNED_8BIT:
  case SEARCH_TYPE_SIGNED_8BIT:
  case SEARCH_TYPE_UNSIGNED_16BIT:
  case SEARCH_TYPE_SIGNED_16BIT:
    return 1;
  case SEARCH_TYPE_UNSIGNED_32BIT:
  case SEARCH_TYPE_SIGNED_32BIT:
  case SEARCH_TYPE_FLOAT_32BIT:
    return 4;
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_SIGNED_64BIT:
  case SEARCH_TYPE_FLOAT_64BIT:
  case SEARCH_TYPE_POINTER:
  case SEARCH_TYPE_UNSIGNED_40BIT:
    return 8;
  default:
    return 4;
  }
}

bool IsModeSupported(searchMode_t mode, bool secondaryPass) {
  switch (mode) {
  case SM_EQ:
  case SM_NE:
  case SM_GT:
  case SM_LT:
  case SM_GE:
  case SM_LE:
  case SM_RANGE_EQ:
  case SM_RANGE_LT:
  case SM_BMEQ:
  case SM_EQ_plus:
  case SM_EQ_plus_plus:
  case SM_PTR:
  case SM_NPTR:
  case SM_NoDecimal:
    return true;
  case SM_MORE:
  case SM_LESS:
  case SM_DIFF:
  case SM_SAME:
  case SM_INC_BY:
  case SM_DEC_BY:
    return secondaryPass;
  default:
    return false;
  }
}

size_t SelectScanBufferBytes() {
  return kFixedScanBuffer;
}

bool EnsureBreezeDir() {
  ult::createDirectory("sdmc:/switch/Breeze/");
  return true;
}

std::string SanitizeStem(std::string stem) {
  if (stem.size() > 4 && stem.substr(stem.size() - 4) == ".dat") {
    stem.erase(stem.size() - 4);
  }
  if (stem.empty()) {
    stem = "1";
  }
  return stem;
}

std::string BuildCandidatePath(const std::string &stem) {
  return std::string("sdmc:/switch/Breeze/") + SanitizeStem(stem) + ".dat";
}

std::string StemFromPath(const std::string &path) {
  size_t slash = path.find_last_of("/\\");
  std::string file =
      (slash == std::string::npos) ? path : path.substr(slash + 1);
  if (file.size() > 4 && file.substr(file.size() - 4) == ".dat") {
    file.erase(file.size() - 4);
  }
  return file;
}

bool OpenCandidateForWrite(const std::string &path,
                           const BreezeFileHeader_t &header, FILE **outFile,
                           std::string *errorOut) {
  if (!EnsureBreezeDir()) {
    if (errorOut)
      *errorOut = "failed to create sdmc:/switch/Breeze/";
    return false;
  }
  FILE *fp = std::fopen(path.c_str(), "w+b");
  if (!fp) {
    if (errorOut)
      *errorOut = "failed to open output candidate file";
    return false;
  }
  if (std::fwrite(&header, sizeof(header), 1, fp) != 1) {
    std::fclose(fp);
    if (errorOut)
      *errorOut = "failed to write candidate header";
    return false;
  }
  *outFile = fp;
  return true;
}

bool RewriteHeader(FILE *fp, const BreezeFileHeader_t &header,
                   std::string *errorOut) {
  if (std::fseek(fp, 0, SEEK_SET) != 0) {
    if (errorOut)
      *errorOut = "failed to seek output header";
    return false;
  }
  if (std::fwrite(&header, sizeof(header), 1, fp) != 1) {
    if (errorOut)
      *errorOut = "failed to rewrite output header";
    return false;
  }
  std::fflush(fp);
  return true;
}

bool FlushRecordBuffer(FILE *fp, std::vector<CandidateRecord> &records,
                       SearchRunStats &stats, std::string *errorOut) {
  if (records.empty()) {
    return true;
  }
  const size_t written =
      std::fwrite(records.data(), sizeof(CandidateRecord), records.size(), fp);
  if (written != records.size()) {
    if (errorOut)
      *errorOut = "failed to write candidate records";
    return false;
  }
  stats.entriesWritten += static_cast<u64>(records.size());
  stats.bytesWritten += static_cast<u64>(records.size() * sizeof(CandidateRecord));
  records.clear();
  return true;
}

bool FlushRecordArray(FILE *fp, CandidateRecord *records, size_t &count,
                      SearchRunStats &stats, std::string *errorOut) {
  if (count == 0) {
    return true;
  }
  const size_t written = std::fwrite(records, sizeof(CandidateRecord), count, fp);
  if (written != count) {
    if (errorOut)
      *errorOut = "failed to write candidate records";
    return false;
  }
  stats.entriesWritten += static_cast<u64>(count);
  stats.bytesWritten += static_cast<u64>(count * sizeof(CandidateRecord));
  count = 0;
  return true;
}

template <typename T>
inline T ReadUnalignedFast(const u8 *ptr) {
  T out{};
  std::memcpy(&out, ptr, sizeof(T));
  return out;
}

template <typename T, searchMode_t Mode>
bool ScanPrimaryChunkTight(const Search_condition &condition, const u8 *scanBuffer,
                           size_t toRead, size_t step, u64 readAddr, u64 heapBase,
                           u64 heapEnd, u64 mainBase, u64 mainEnd,
                           CandidateRecord *outRecords, const size_t outCapacity,
                           size_t &outCount, FILE *out, SearchRunStats &stats,
                           std::string *errorOut) {
  const T a = search_exec::SearchValueAs<T>(condition.searchValue_1);
  const T b = search_exec::SearchValueAs<T>(condition.searchValue_2);
  const u32 aEq = search_exec::ConditionValue1AsU32(condition);

  for (size_t off = 0; off + sizeof(T) <= toRead; off += step) {
    if constexpr (Mode == SM_EQ_plus || Mode == SM_EQ_plus_plus) {
      if (off + sizeof(double) > toRead) {
        break;
      }
    }

    const u8 *ptr = scanBuffer + off;

    bool matched = false;
    if constexpr (Mode == SM_EQ_plus) {
      const u32 vU32 = ReadUnalignedFast<u32>(ptr);
      const float vF32 = ReadUnalignedFast<float>(ptr);
      const double vF64 = ReadUnalignedFast<double>(ptr);
      matched = (aEq == vU32) || (static_cast<float>(aEq) == vF32) ||
                (static_cast<double>(aEq) == vF64);
    } else if constexpr (Mode == SM_EQ_plus_plus) {
      const u32 vU32 = ReadUnalignedFast<u32>(ptr);
      const float vF32 = ReadUnalignedFast<float>(ptr);
      const double vF64 = ReadUnalignedFast<double>(ptr);
      const float aF32 = static_cast<float>(aEq);
      const double aF64 = static_cast<double>(aEq);
      matched = (aEq == vU32) ||
                ((vF32 > (aF32 - 1.0f)) && (vF32 < (aF32 + 1.0f))) ||
                ((vF64 > (aF64 - 1.0)) && (vF64 < (aF64 + 1.0)));
    } else if constexpr (Mode == SM_EQ) {
      const T current = ReadUnalignedFast<T>(ptr);
      matched = (current == a);
    } else if constexpr (Mode == SM_NE) {
      const T current = ReadUnalignedFast<T>(ptr);
      matched = (current != a);
    } else if constexpr (Mode == SM_GT) {
      const T current = ReadUnalignedFast<T>(ptr);
      matched = (current > a);
    } else if constexpr (Mode == SM_LT) {
      const T current = ReadUnalignedFast<T>(ptr);
      matched = (current < a);
    } else if constexpr (Mode == SM_GE) {
      const T current = ReadUnalignedFast<T>(ptr);
      matched = (current >= a);
    } else if constexpr (Mode == SM_LE) {
      const T current = ReadUnalignedFast<T>(ptr);
      matched = (current <= a);
    } else if constexpr (Mode == SM_RANGE_EQ) {
      const T current = ReadUnalignedFast<T>(ptr);
      matched = (current >= a) && (current <= b);
    } else if constexpr (Mode == SM_RANGE_LT) {
      const T current = ReadUnalignedFast<T>(ptr);
      matched = (current > a) && (current < b);
    } else if constexpr (Mode == SM_BMEQ) {
      if constexpr (std::is_integral<T>::value) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (static_cast<u64>(current) & static_cast<u64>(b)) ==
                  static_cast<u64>(a);
      } else {
        matched = false;
      }
    } else if constexpr (Mode == SM_PTR) {
      const T current = ReadUnalignedFast<T>(ptr);
      const u64 v = static_cast<u64>(current);
      matched = ((v >= heapBase) && (v < heapEnd)) ||
                ((v >= mainBase) && (v < mainEnd));
    } else if constexpr (Mode == SM_NPTR) {
      const T current = ReadUnalignedFast<T>(ptr);
      const u64 v = static_cast<u64>(current);
      matched = !(((v >= heapBase) && (v < heapEnd)) ||
                  ((v >= mainBase) && (v < mainEnd)));
    } else if constexpr (Mode == SM_NoDecimal) {
      if constexpr (std::is_floating_point<T>::value) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current >= a) && (current <= b) &&
                  (std::trunc(current) == current);
      } else {
        matched = false;
      }
    } else {
      matched = false;
    }

    if (!matched) {
      continue;
    }

    CandidateRecord &rec = outRecords[outCount++];
    rec.address = readAddr + static_cast<u64>(off);
    rec.value = 0;
    if constexpr (Mode == SM_EQ_plus || Mode == SM_EQ_plus_plus) {
      std::memcpy(&rec.value, ptr, sizeof(u64));
    } else {
      std::memcpy(&rec.value, ptr, sizeof(T));
    }

    if (outCount == outCapacity) {
      if (!FlushRecordArray(out, outRecords, outCount, stats, errorOut)) {
        return false;
      }
    }
  }

  return true;
}

using ScanPrimaryChunkFn = bool (*)(
    const Search_condition &condition, const u8 *scanBuffer, size_t toRead,
    size_t step, u64 readAddr, u64 heapBase, u64 heapEnd, u64 mainBase,
    u64 mainEnd, CandidateRecord *outRecords, size_t outCapacity, size_t &outCount,
    FILE *out, SearchRunStats &stats, std::string *errorOut);

template <typename T>
ScanPrimaryChunkFn ResolvePrimaryChunkScannerForType(searchMode_t mode) {
  switch (mode) {
  case SM_EQ:
    return &ScanPrimaryChunkTight<T, SM_EQ>;
  case SM_NE:
    return &ScanPrimaryChunkTight<T, SM_NE>;
  case SM_GT:
    return &ScanPrimaryChunkTight<T, SM_GT>;
  case SM_LT:
    return &ScanPrimaryChunkTight<T, SM_LT>;
  case SM_GE:
    return &ScanPrimaryChunkTight<T, SM_GE>;
  case SM_LE:
    return &ScanPrimaryChunkTight<T, SM_LE>;
  case SM_RANGE_EQ:
    return &ScanPrimaryChunkTight<T, SM_RANGE_EQ>;
  case SM_RANGE_LT:
    return &ScanPrimaryChunkTight<T, SM_RANGE_LT>;
  case SM_BMEQ:
    return &ScanPrimaryChunkTight<T, SM_BMEQ>;
  case SM_EQ_plus:
    return &ScanPrimaryChunkTight<T, SM_EQ_plus>;
  case SM_EQ_plus_plus:
    return &ScanPrimaryChunkTight<T, SM_EQ_plus_plus>;
  case SM_PTR:
    return &ScanPrimaryChunkTight<T, SM_PTR>;
  case SM_NPTR:
    return &ScanPrimaryChunkTight<T, SM_NPTR>;
  case SM_NoDecimal:
    return &ScanPrimaryChunkTight<T, SM_NoDecimal>;
  default:
    return nullptr;
  }
}

ScanPrimaryChunkFn ResolvePrimaryChunkScanner(const Search_condition &condition) {
  switch (condition.searchType) {
  case SEARCH_TYPE_UNSIGNED_8BIT:
    return ResolvePrimaryChunkScannerForType<u8>(condition.searchMode);
  case SEARCH_TYPE_SIGNED_8BIT:
    return ResolvePrimaryChunkScannerForType<s8>(condition.searchMode);
  case SEARCH_TYPE_UNSIGNED_16BIT:
    return ResolvePrimaryChunkScannerForType<u16>(condition.searchMode);
  case SEARCH_TYPE_SIGNED_16BIT:
    return ResolvePrimaryChunkScannerForType<s16>(condition.searchMode);
  case SEARCH_TYPE_UNSIGNED_32BIT:
    return ResolvePrimaryChunkScannerForType<u32>(condition.searchMode);
  case SEARCH_TYPE_SIGNED_32BIT:
    return ResolvePrimaryChunkScannerForType<s32>(condition.searchMode);
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_POINTER:
  case SEARCH_TYPE_UNSIGNED_40BIT:
    return ResolvePrimaryChunkScannerForType<u64>(condition.searchMode);
  case SEARCH_TYPE_SIGNED_64BIT:
    return ResolvePrimaryChunkScannerForType<s64>(condition.searchMode);
  case SEARCH_TYPE_FLOAT_32BIT:
    return ResolvePrimaryChunkScannerForType<float>(condition.searchMode);
  case SEARCH_TYPE_FLOAT_64BIT:
    return ResolvePrimaryChunkScannerForType<double>(condition.searchMode);
  default:
    return ResolvePrimaryChunkScannerForType<u32>(condition.searchMode);
  }
}

template <typename T, searchMode_t Mode>
bool ScanSecondaryWindowTight(
    const Search_condition &condition, const CandidateRecord *inRecords,
    size_t beginIndex, size_t endIndex, u64 windowBase, const u8 *windowBytes,
    size_t scanBytesPerRecord, u64 heapBase, u64 heapEnd, u64 mainBase, u64 mainEnd,
    CandidateRecord *outRecords, const size_t outCapacity, size_t &outCount,
    FILE *out, SearchRunStats &stats, std::string *errorOut) {
  const T a = search_exec::SearchValueAs<T>(condition.searchValue_1);
  const T b = search_exec::SearchValueAs<T>(condition.searchValue_2);
  const u32 aEq = search_exec::ConditionValue1AsU32(condition);

  for (size_t i = beginIndex; i < endIndex; ++i) {
    const CandidateRecord &src = inRecords[i];
    const size_t offset = static_cast<size_t>(src.address - windowBase);
    const u8 *ptr = windowBytes + offset;

    bool matched = false;
    if constexpr (Mode == SM_EQ_plus) {
      const u32 vU32 = ReadUnalignedFast<u32>(ptr);
      const float vF32 = ReadUnalignedFast<float>(ptr);
      const double vF64 = ReadUnalignedFast<double>(ptr);
      matched = (aEq == vU32) || (static_cast<float>(aEq) == vF32) ||
                (static_cast<double>(aEq) == vF64);
    } else if constexpr (Mode == SM_EQ_plus_plus) {
      const u32 vU32 = ReadUnalignedFast<u32>(ptr);
      const float vF32 = ReadUnalignedFast<float>(ptr);
      const double vF64 = ReadUnalignedFast<double>(ptr);
      const float aF32 = static_cast<float>(aEq);
      const double aF64 = static_cast<double>(aEq);
      matched = (aEq == vU32) ||
                ((vF32 > (aF32 - 1.0f)) && (vF32 < (aF32 + 1.0f))) ||
                ((vF64 > (aF64 - 1.0)) && (vF64 < (aF64 + 1.0)));
    } else {
      T previous{};
      std::memcpy(&previous, &src.value, sizeof(T));

      if constexpr (Mode == SM_EQ) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current == a);
      } else if constexpr (Mode == SM_NE) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current != a);
      } else if constexpr (Mode == SM_GT) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current > a);
      } else if constexpr (Mode == SM_LT) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current < a);
      } else if constexpr (Mode == SM_GE) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current >= a);
      } else if constexpr (Mode == SM_LE) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current <= a);
      } else if constexpr (Mode == SM_RANGE_EQ) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current >= a) && (current <= b);
      } else if constexpr (Mode == SM_RANGE_LT) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current > a) && (current < b);
      } else if constexpr (Mode == SM_BMEQ) {
        if constexpr (std::is_integral<T>::value) {
          const T current = ReadUnalignedFast<T>(ptr);
          matched = (static_cast<u64>(current) & static_cast<u64>(b)) ==
                    static_cast<u64>(a);
        } else {
          matched = false;
        }
      } else if constexpr (Mode == SM_MORE) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current > previous);
      } else if constexpr (Mode == SM_LESS) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current < previous);
      } else if constexpr (Mode == SM_DIFF) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current != previous);
      } else if constexpr (Mode == SM_SAME) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current == previous);
      } else if constexpr (Mode == SM_INC_BY) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current > (previous + a - static_cast<T>(1))) &&
                  (current < (previous + a + static_cast<T>(1)));
      } else if constexpr (Mode == SM_DEC_BY) {
        const T current = ReadUnalignedFast<T>(ptr);
        matched = (current > (previous - a - static_cast<T>(1))) &&
                  (current < (previous - a + static_cast<T>(1)));
      } else if constexpr (Mode == SM_PTR) {
        const T current = ReadUnalignedFast<T>(ptr);
        const u64 v = static_cast<u64>(current);
        matched = ((v >= heapBase) && (v < heapEnd)) ||
                  ((v >= mainBase) && (v < mainEnd));
      } else if constexpr (Mode == SM_NPTR) {
        const T current = ReadUnalignedFast<T>(ptr);
        const u64 v = static_cast<u64>(current);
        matched = !(((v >= heapBase) && (v < heapEnd)) ||
                    ((v >= mainBase) && (v < mainEnd)));
      } else if constexpr (Mode == SM_NoDecimal) {
        if constexpr (std::is_floating_point<T>::value) {
          const T current = ReadUnalignedFast<T>(ptr);
          matched = (current >= a) && (current <= b) &&
                    (std::trunc(current) == current);
        } else {
          matched = false;
        }
      } else {
        matched = false;
      }
    }

    stats.bytesScanned += scanBytesPerRecord;
    if (!matched) {
      continue;
    }

    CandidateRecord &rec = outRecords[outCount++];
    rec.address = src.address;
    rec.value = 0;
    if constexpr (Mode == SM_EQ_plus || Mode == SM_EQ_plus_plus) {
      std::memcpy(&rec.value, ptr, sizeof(u64));
    } else {
      std::memcpy(&rec.value, ptr, sizeof(T));
    }

    if (outCount == outCapacity) {
      if (!FlushRecordArray(out, outRecords, outCount, stats, errorOut)) {
        return false;
      }
    }
  }

  return true;
}

using ScanSecondaryWindowFn = bool (*)(
    const Search_condition &condition, const CandidateRecord *inRecords,
    size_t beginIndex, size_t endIndex, u64 windowBase, const u8 *windowBytes,
    size_t scanBytesPerRecord, u64 heapBase, u64 heapEnd, u64 mainBase, u64 mainEnd,
    CandidateRecord *outRecords, size_t outCapacity, size_t &outCount, FILE *out,
    SearchRunStats &stats, std::string *errorOut);

template <typename T>
ScanSecondaryWindowFn ResolveSecondaryWindowScannerForType(searchMode_t mode) {
  switch (mode) {
  case SM_EQ:
    return &ScanSecondaryWindowTight<T, SM_EQ>;
  case SM_NE:
    return &ScanSecondaryWindowTight<T, SM_NE>;
  case SM_GT:
    return &ScanSecondaryWindowTight<T, SM_GT>;
  case SM_LT:
    return &ScanSecondaryWindowTight<T, SM_LT>;
  case SM_GE:
    return &ScanSecondaryWindowTight<T, SM_GE>;
  case SM_LE:
    return &ScanSecondaryWindowTight<T, SM_LE>;
  case SM_RANGE_EQ:
    return &ScanSecondaryWindowTight<T, SM_RANGE_EQ>;
  case SM_RANGE_LT:
    return &ScanSecondaryWindowTight<T, SM_RANGE_LT>;
  case SM_BMEQ:
    return &ScanSecondaryWindowTight<T, SM_BMEQ>;
  case SM_MORE:
    return &ScanSecondaryWindowTight<T, SM_MORE>;
  case SM_LESS:
    return &ScanSecondaryWindowTight<T, SM_LESS>;
  case SM_DIFF:
    return &ScanSecondaryWindowTight<T, SM_DIFF>;
  case SM_SAME:
    return &ScanSecondaryWindowTight<T, SM_SAME>;
  case SM_INC_BY:
    return &ScanSecondaryWindowTight<T, SM_INC_BY>;
  case SM_DEC_BY:
    return &ScanSecondaryWindowTight<T, SM_DEC_BY>;
  case SM_EQ_plus:
    return &ScanSecondaryWindowTight<T, SM_EQ_plus>;
  case SM_EQ_plus_plus:
    return &ScanSecondaryWindowTight<T, SM_EQ_plus_plus>;
  case SM_PTR:
    return &ScanSecondaryWindowTight<T, SM_PTR>;
  case SM_NPTR:
    return &ScanSecondaryWindowTight<T, SM_NPTR>;
  case SM_NoDecimal:
    return &ScanSecondaryWindowTight<T, SM_NoDecimal>;
  default:
    return nullptr;
  }
}

ScanSecondaryWindowFn ResolveSecondaryWindowScanner(
    const Search_condition &condition) {
  switch (condition.searchType) {
  case SEARCH_TYPE_UNSIGNED_8BIT:
    return ResolveSecondaryWindowScannerForType<u8>(condition.searchMode);
  case SEARCH_TYPE_SIGNED_8BIT:
    return ResolveSecondaryWindowScannerForType<s8>(condition.searchMode);
  case SEARCH_TYPE_UNSIGNED_16BIT:
    return ResolveSecondaryWindowScannerForType<u16>(condition.searchMode);
  case SEARCH_TYPE_SIGNED_16BIT:
    return ResolveSecondaryWindowScannerForType<s16>(condition.searchMode);
  case SEARCH_TYPE_UNSIGNED_32BIT:
    return ResolveSecondaryWindowScannerForType<u32>(condition.searchMode);
  case SEARCH_TYPE_SIGNED_32BIT:
    return ResolveSecondaryWindowScannerForType<s32>(condition.searchMode);
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_POINTER:
  case SEARCH_TYPE_UNSIGNED_40BIT:
    return ResolveSecondaryWindowScannerForType<u64>(condition.searchMode);
  case SEARCH_TYPE_SIGNED_64BIT:
    return ResolveSecondaryWindowScannerForType<s64>(condition.searchMode);
  case SEARCH_TYPE_FLOAT_32BIT:
    return ResolveSecondaryWindowScannerForType<float>(condition.searchMode);
  case SEARCH_TYPE_FLOAT_64BIT:
    return ResolveSecondaryWindowScannerForType<double>(condition.searchMode);
  default:
    return ResolveSecondaryWindowScannerForType<u32>(condition.searchMode);
  }
}

void FillHeaderBase(BreezeFileHeader_t &header, const Search_condition &condition,
                    bool isSecondary, const DmntCheatProcessMetadata &metadata) {
  header = BreezeFileHeader_t{};
  header.filetype = search_mission;
  header.search_condition = condition;
  header.search_condition.search_step =
      isSecondary ? search_step_secondary : search_step_primary;
  header.Metadata = metadata;
  header.compressed = false;
  header.has_screenshot = false;
}

} // namespace

bool RunStartSearch(const Search_condition &condition,
                    const std::string &outputStem, SearchRunStats &stats,
                    std::string *errorOut) {
  stats = SearchRunStats{};
  if (!IsModeSupported(condition.searchMode, false)) {
    if (errorOut)
      *errorOut = "search mode not supported for start search yet";
    return false;
  }

  DmntCheatProcessMetadata metadata{};
  if (R_FAILED(dmntchtGetCheatProcessMetadata(&metadata))) {
    if (errorOut)
      *errorOut = "failed to get cheat process metadata";
    return false;
  }

  BreezeFileHeader_t header{};
  FillHeaderBase(header, condition, false, metadata);
  const std::string outputPath = BuildCandidatePath(outputStem);

  FILE *out = nullptr;
  if (!OpenCandidateForWrite(outputPath, header, &out, errorOut)) {
    return false;
  }

  const size_t valueSize = TypeByteWidth(condition.searchType);
  const size_t step = SearchStepIncrement(condition.searchType);
  const ScanPrimaryChunkFn scanChunkFn = ResolvePrimaryChunkScanner(condition);
  if (!scanChunkFn) {
    std::fclose(out);
    if (errorOut)
      *errorOut = "search mode/type combo not supported for start search";
    return false;
  }
  stats.scanBufferBytes = SelectScanBufferBytes();
  std::vector<u8> scanBuffer(stats.scanBufferBytes);
  const size_t outCapacity = kOutputBuffer / sizeof(CandidateRecord);
  std::vector<CandidateRecord> outRecords(outCapacity);
  size_t outCount = 0;

  const u64 heapBase = metadata.heap_extents.base;
  const u64 heapEnd = metadata.heap_extents.base + metadata.heap_extents.size;
  const u64 mainBase = metadata.main_nso_extents.base;
  const u64 mainEnd = metadata.main_nso_extents.base + metadata.main_nso_extents.size;

  const u64 startTime = armGetSystemTick();
  MemoryInfo info{};
  u64 address = 0;
  while (true) {
    Result rc = dmntchtQueryCheatProcessMemory(&info, address);
    if (R_FAILED(rc) || info.addr < address || info.size == 0) {
      break;
    }

    const u64 segStart = info.addr;
    const u64 segEnd = info.addr + info.size;
    const bool readable = ((info.perm & Perm_R) == Perm_R);
    if (readable) {
      u64 readAddr = segStart;
      while (readAddr < segEnd) {
        const size_t toRead =
            static_cast<size_t>(std::min<u64>(scanBuffer.size(), segEnd - readAddr));
        if (toRead < valueSize) {
          break;
        }
        rc = dmntchtReadCheatProcessMemory(readAddr, scanBuffer.data(), toRead);
        if (R_FAILED(rc)) {
          break;
        }

        if (!scanChunkFn(condition, scanBuffer.data(), toRead, step, readAddr,
                         heapBase, heapEnd, mainBase, mainEnd, outRecords.data(),
                         outCapacity, outCount, out, stats, errorOut)) {
          std::fclose(out);
          return false;
        }
        stats.bytesScanned += toRead;
        readAddr += toRead;
      }
    }

    if (segEnd <= address) {
      break;
    }
    address = segEnd;
  }

  if (!FlushRecordArray(out, outRecords.data(), outCount, stats, errorOut)) {
    std::fclose(out);
    return false;
  }

  header.dataSize = stats.bytesWritten;
  const u64 elapsedNs = armTicksToNs(armGetSystemTick() - startTime);
  header.timetaken = static_cast<u8>(std::min<u64>(255, elapsedNs / 1000000000ULL));
  stats.secondsTaken = header.timetaken;

  const bool headerOk = RewriteHeader(out, header, errorOut);
  std::fclose(out);
  return headerOk;
}

bool RunContinueSearch(const Search_condition &condition,
                       const std::string &sourceCandidatePath,
                       const std::string &outputStem, SearchRunStats &stats,
                       std::string *errorOut) {
  stats = SearchRunStats{};
  if (!IsModeSupported(condition.searchMode, true)) {
    if (errorOut)
      *errorOut = "search mode not supported for continue search yet";
    return false;
  }

  BreezeFileHeader_t sourceHeader{};
  std::string readErr;
  if (!ReadCandidateHeader(sourceCandidatePath, sourceHeader, &readErr)) {
    if (errorOut)
      *errorOut = "invalid source candidate: " + readErr;
    return false;
  }

  DmntCheatProcessMetadata metadata{};
  if (R_FAILED(dmntchtGetCheatProcessMetadata(&metadata))) {
    if (errorOut)
      *errorOut = "failed to get cheat process metadata";
    return false;
  }

  BreezeFileHeader_t header{};
  FillHeaderBase(header, condition, true, metadata);
  header.from_to_size = sourceHeader.dataSize;
  const std::string sourceStem = StemFromPath(sourceCandidatePath);
  std::snprintf(header.prefilename, sizeof(header.prefilename), "%s",
                sourceStem.c_str());

  const std::string outputPath = BuildCandidatePath(outputStem);
  FILE *out = nullptr;
  if (!OpenCandidateForWrite(outputPath, header, &out, errorOut)) {
    return false;
  }

  FILE *in = std::fopen(sourceCandidatePath.c_str(), "rb");
  if (!in) {
    std::fclose(out);
    if (errorOut)
      *errorOut = "failed to open source candidate file";
    return false;
  }
  if (std::fseek(in, static_cast<long>(sizeof(BreezeFileHeader_t)), SEEK_SET) != 0) {
    std::fclose(in);
    std::fclose(out);
    if (errorOut)
      *errorOut = "failed to seek source candidate payload";
    return false;
  }

  const size_t valueSize = TypeByteWidth(condition.searchType);
  const ScanSecondaryWindowFn scanWindowFn =
      ResolveSecondaryWindowScanner(condition);
  if (!scanWindowFn) {
    std::fclose(in);
    std::fclose(out);
    if (errorOut)
      *errorOut = "search mode/type combo not supported for continue search";
    return false;
  }
  const u64 heapBase = metadata.heap_extents.base;
  const u64 heapEnd = metadata.heap_extents.base + metadata.heap_extents.size;
  const u64 mainBase = metadata.main_nso_extents.base;
  const u64 mainEnd = metadata.main_nso_extents.base + metadata.main_nso_extents.size;
  const u64 startTime = armGetSystemTick();

  std::vector<CandidateRecord> inRecords(kContinueInputBuffer / sizeof(CandidateRecord));
  std::vector<u8> memoryBuffer(kContinueMemoryBuffer);
  stats.scanBufferBytes = memoryBuffer.size();
  const size_t outCapacity = kOutputBuffer / sizeof(CandidateRecord);
  std::vector<CandidateRecord> outRecords(outCapacity);
  size_t outCount = 0;

  while (true) {
    const size_t readCount =
        std::fread(inRecords.data(), sizeof(CandidateRecord), inRecords.size(), in);
    if (readCount == 0) {
      break;
    }

    size_t i = 0;
    while (i < readCount) {
      const u64 windowBase = inRecords[i].address;
      const u64 maxWindowSpan =
          std::min<u64>(static_cast<u64>(memoryBuffer.size()),
                        std::numeric_limits<u64>::max() - windowBase);

      size_t end = i;
      while (end < readCount) {
        const u64 addr = inRecords[end].address;
        if (addr < windowBase) {
          break;
        }
        const u64 delta = addr - windowBase;
        if (delta + sizeof(u64) > maxWindowSpan) {
          break;
        }
        ++end;
      }
      if (end == i) {
        end = i + 1;
      }

      const u64 lastAddr = inRecords[end - 1].address;
      const size_t bytesToRead =
          static_cast<size_t>((lastAddr - windowBase) + sizeof(u64));

      if (R_FAILED(dmntchtReadCheatProcessMemory(windowBase, memoryBuffer.data(),
                                                 bytesToRead))) {
        if (R_FAILED(dmntchtReadCheatProcessMemory(windowBase, memoryBuffer.data(),
                                                   sizeof(u64)))) {
          ++i;
          continue;
        }

        if (!scanWindowFn(condition, inRecords.data(), i, i + 1, windowBase,
                          memoryBuffer.data(), valueSize, heapBase, heapEnd,
                          mainBase, mainEnd, outRecords.data(), outCapacity, outCount,
                          out, stats, errorOut)) {
          std::fclose(in);
          std::fclose(out);
          return false;
        }
        ++i;
        continue;
      }

      if (!scanWindowFn(condition, inRecords.data(), i, end, windowBase,
                        memoryBuffer.data(), valueSize, heapBase, heapEnd,
                        mainBase, mainEnd, outRecords.data(), outCapacity, outCount,
                        out, stats, errorOut)) {
        std::fclose(in);
        std::fclose(out);
        return false;
      }
      i = end;
    }

    if (readCount < inRecords.size()) {
      break;
    }
  }

  if (!FlushRecordArray(out, outRecords.data(), outCount, stats, errorOut)) {
    std::fclose(in);
    std::fclose(out);
    return false;
  }

  std::fclose(in);
  header.dataSize = stats.bytesWritten;
  const u64 elapsedNs = armTicksToNs(armGetSystemTick() - startTime);
  header.timetaken = static_cast<u8>(std::min<u64>(255, elapsedNs / 1000000000ULL));
  stats.secondsTaken = header.timetaken;
  const bool headerOk = RewriteHeader(out, header, errorOut);
  std::fclose(out);
  return headerOk;
}

} // namespace breeze
