#include "breeze_search_exec.hpp"

#include <algorithm>
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

bool MatchPrimaryValue(const Search_condition &condition, const u8 *bytes,
                       size_t available, u64 heapBase, u64 heapEnd, u64 mainBase,
                       u64 mainEnd) {
  const searchMode_t mode = condition.searchMode;
  if (mode == SM_EQ_plus) {
    if (available < sizeof(double)) {
      return false;
    }
    return search_exec::MatchEqPlus(condition, bytes);
  }
  if (mode == SM_EQ_plus_plus) {
    if (available < sizeof(double)) {
      return false;
    }
    return search_exec::MatchEqPlusPlus(condition, bytes);
  }

  return search_exec::DispatchBySearchType(
      condition.searchType, [&](auto tag) -> bool {
        using T = typename decltype(tag)::type;
        if (available < sizeof(T)) {
          return false;
        }
        const T current = search_exec::ReadUnaligned<T>(bytes);
        return search_exec::MatchModeTyped<T>(mode, current, condition, nullptr,
                                              nullptr, heapBase, heapEnd,
                                              mainBase, mainEnd);
      });
}

bool MatchSecondaryValue(const Search_condition &condition, u64 previousRaw,
                         const u8 *bytes, size_t available, u64 heapBase,
                         u64 heapEnd, u64 mainBase, u64 mainEnd) {
  const searchMode_t mode = condition.searchMode;
  if (mode == SM_EQ_plus) {
    if (available < sizeof(double)) {
      return false;
    }
    return search_exec::MatchEqPlus(condition, bytes);
  }
  if (mode == SM_EQ_plus_plus) {
    if (available < sizeof(double)) {
      return false;
    }
    return search_exec::MatchEqPlusPlus(condition, bytes);
  }

  return search_exec::DispatchBySearchType(
      condition.searchType, [&](auto tag) -> bool {
        using T = typename decltype(tag)::type;
        if (available < sizeof(T)) {
          return false;
        }
        searchValue_t previous{};
        previous._u64 = previousRaw;
        const T previousTyped = search_exec::SearchValueAs<T>(previous);
        const T current = search_exec::ReadUnaligned<T>(bytes);
        return search_exec::MatchModeTyped<T>(mode, current, condition,
                                              &previousTyped, nullptr, heapBase,
                                              heapEnd, mainBase, mainEnd);
      });
}

u64 ReadRaw64FromBytes(const u8 *bytes, size_t available) {
  u64 raw = 0;
  const size_t copySize = std::min<size_t>(sizeof(raw), available);
  std::memcpy(&raw, bytes, copySize);
  return raw;
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
  stats.scanBufferBytes = SelectScanBufferBytes();
  std::vector<u8> scanBuffer(stats.scanBufferBytes);
  std::vector<CandidateRecord> outRecords;
  outRecords.reserve(kOutputBuffer / sizeof(CandidateRecord));

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

        for (size_t off = 0; off + valueSize <= toRead; off += step) {
          const size_t remain = toRead - off;
          if (!MatchPrimaryValue(condition, scanBuffer.data() + off, remain,
                                 heapBase, heapEnd, mainBase, mainEnd)) {
            continue;
          }
          CandidateRecord rec{};
          rec.address = readAddr + static_cast<u64>(off);
          rec.value = ReadRaw64FromBytes(scanBuffer.data() + off, remain);
          outRecords.push_back(rec);
          if (outRecords.size() >= (kOutputBuffer / sizeof(CandidateRecord))) {
            if (!FlushRecordBuffer(out, outRecords, stats, errorOut)) {
              std::fclose(out);
              return false;
            }
          }
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

  if (!FlushRecordBuffer(out, outRecords, stats, errorOut)) {
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
  const u64 heapBase = metadata.heap_extents.base;
  const u64 heapEnd = metadata.heap_extents.base + metadata.heap_extents.size;
  const u64 mainBase = metadata.main_nso_extents.base;
  const u64 mainEnd = metadata.main_nso_extents.base + metadata.main_nso_extents.size;
  const u64 startTime = armGetSystemTick();

  std::vector<CandidateRecord> inRecords(kFixedScanBuffer / sizeof(CandidateRecord));
  stats.scanBufferBytes = kFixedScanBuffer;
  std::vector<CandidateRecord> outRecords;
  outRecords.reserve(kOutputBuffer / sizeof(CandidateRecord));

  while (true) {
    const size_t readCount =
        std::fread(inRecords.data(), sizeof(CandidateRecord), inRecords.size(), in);
    if (readCount == 0) {
      break;
    }
    for (size_t i = 0; i < readCount; ++i) {
      const CandidateRecord &src = inRecords[i];
      u8 currentBytes[8] = {};
      if (R_FAILED(dmntchtReadCheatProcessMemory(src.address, currentBytes,
                                                 sizeof(currentBytes)))) {
        continue;
      }
      stats.bytesScanned += valueSize;
      if (!MatchSecondaryValue(condition, src.value, currentBytes, sizeof(currentBytes),
                               heapBase, heapEnd, mainBase, mainEnd)) {
        continue;
      }

      CandidateRecord rec{};
      rec.address = src.address;
      rec.value = ReadRaw64FromBytes(currentBytes, sizeof(currentBytes));
      outRecords.push_back(rec);
      if (outRecords.size() >= (kOutputBuffer / sizeof(CandidateRecord))) {
        if (!FlushRecordBuffer(out, outRecords, stats, errorOut)) {
          std::fclose(in);
          std::fclose(out);
          return false;
        }
      }
    }
    if (readCount < inRecords.size()) {
      break;
    }
  }

  if (!FlushRecordBuffer(out, outRecords, stats, errorOut)) {
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
