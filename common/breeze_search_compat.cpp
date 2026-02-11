#include "breeze_search_compat.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <set>
#include <sys/stat.h>

namespace breeze {
namespace {

constexpr searchMode_t kMaxKnownMode = SM_GETBZ;
constexpr searchType_t kMaxKnownType = SEARCH_TYPE_UNSIGNED_40BIT;

const char *const kModeNames[] = {
    "==A",     "!=A",          ">A",           "<A",       ">=A",
    "<=A",     "[A..B]",       "&B=A",         "<A..B>",   "++",
    "--",      "DIFF",         "SAME",         "[A,B]",    "[A,,B]",
    "STRING",  "++Val",        "--Val",        "==*A",     "==**A",
    "NONE",    "DIFFB",        "SAMEB",        "B++",      "B--",
    "NotAB",   "[A.B.C]",      "[A bflip B]",  "Advance",  "GAP",
    "{GAP}",   "PTR",          "~PTR",         "[A..B]f.0","Gen2 data",
    "Gen2 code","GETB",        "REBASE",       "Target",   "ptr and offset",
    "skip",    "Aborted Target Search", "Branch code", "LDRx code",
    "ADRP code","EOR code",    "GETB==A"};

bool EndsWithDat(const char *name) {
    if (name == nullptr) {
        return false;
    }
    const size_t len = std::strlen(name);
    return len > 4 && std::strcmp(name + len - 4, ".dat") == 0;
}

bool ValidateHeader(const BreezeFileHeader_t &header, size_t fileSize,
                    std::string *errorOut) {
    if (std::memcmp(header.MAGIC, kFileMagic, sizeof(header.MAGIC)) != 0) {
        if (errorOut)
            *errorOut = "header magic mismatch";
        return false;
    }
    if (std::memcmp(header.End, kHeaderEnd, sizeof(header.End)) != 0) {
        if (errorOut)
            *errorOut = "header terminator mismatch";
        return false;
    }
    if (header.search_condition.searchMode < SM_EQ ||
        header.search_condition.searchMode > kMaxKnownMode) {
        if (errorOut)
            *errorOut = "unsupported search mode in file";
        return false;
    }
    if (header.search_condition.searchType < SEARCH_TYPE_UNSIGNED_8BIT ||
        header.search_condition.searchType > kMaxKnownType) {
        if (errorOut)
            *errorOut = "unsupported search type in file";
        return false;
    }
    if (header.search_condition.searchStringLen >=
        sizeof(header.search_condition.searchString)) {
        if (errorOut)
            *errorOut = "search string length out of range";
        return false;
    }

    const size_t expectedPayload = static_cast<size_t>(header.dataSize);
    const size_t minSize = sizeof(BreezeFileHeader_t) + expectedPayload;
    const size_t screenshotSize = header.has_screenshot ? kScreenshotBytes : 0;
    const size_t expectedSize = minSize + screenshotSize;

    if (fileSize != expectedSize) {
        if (errorOut)
            *errorOut = "file size does not match header";
        return false;
    }
    return true;
}

} // namespace

bool ReadCandidateHeader(const std::string &path, BreezeFileHeader_t &outHeader,
                         std::string *errorOut) {
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        if (errorOut)
            *errorOut = "failed to open file";
        return false;
    }

    if (std::fseek(fp, 0, SEEK_END) != 0) {
        std::fclose(fp);
        if (errorOut)
            *errorOut = "failed to seek file";
        return false;
    }
    long fileSizeSigned = std::ftell(fp);
    if (fileSizeSigned < 0) {
        std::fclose(fp);
        if (errorOut)
            *errorOut = "failed to get file size";
        return false;
    }
    const size_t fileSize = static_cast<size_t>(fileSizeSigned);
    if (fileSize < sizeof(BreezeFileHeader_t)) {
        std::fclose(fp);
        if (errorOut)
            *errorOut = "file too small for Breeze header";
        return false;
    }

    if (std::fseek(fp, 0, SEEK_SET) != 0) {
        std::fclose(fp);
        if (errorOut)
            *errorOut = "failed to seek file start";
        return false;
    }

    BreezeFileHeader_t header{};
    const size_t readCount =
        std::fread(&header, sizeof(BreezeFileHeader_t), 1, fp);
    std::fclose(fp);
    if (readCount != 1) {
        if (errorOut)
            *errorOut = "failed to read Breeze header";
        return false;
    }

    if (!ValidateHeader(header, fileSize, errorOut)) {
        return false;
    }
    outHeader = header;
    return true;
}

std::vector<std::string> ListCandidateFiles(const std::vector<std::string> &roots) {
    std::vector<std::string> found;
    for (const auto &root : roots) {
        DIR *dir = opendir(root.c_str());
        if (!dir) {
            continue;
        }
        dirent *entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            if (!EndsWithDat(entry->d_name)) {
                continue;
            }
            found.push_back(root + entry->d_name);
        }
        closedir(dir);
    }

    // Deduplicate by filename stem so sdmc:/switch/Breeze and /switch/Breeze
    // aliases don't produce duplicates.
    std::sort(found.begin(), found.end());
    std::vector<std::string> result;
    std::set<std::string> stems;
    for (const auto &path : found) {
        size_t slash = path.find_last_of("/\\");
        std::string file = (slash == std::string::npos) ? path : path.substr(slash + 1);
        if (file.size() > 4 && file.substr(file.size() - 4) == ".dat") {
            file = file.substr(0, file.size() - 4);
        }
        if (stems.insert(file).second) {
            result.push_back(path);
        }
    }
    return result;
}

bool LoadLatestCandidateCondition(Search_condition &outCondition,
                                  std::string &outFilePath,
                                  std::string *errorOut) {
    const std::vector<std::string> files = ListCandidateFiles();
    if (files.empty()) {
        if (errorOut)
            *errorOut = "no .dat candidate files found in /switch/Breeze";
        return false;
    }

    std::string latestPath;
    time_t latestTime = 0;
    for (const auto &file : files) {
        struct stat st {};
        if (stat(file.c_str(), &st) != 0) {
            continue;
        }
        if (latestPath.empty() || st.st_mtime > latestTime) {
            latestPath = file;
            latestTime = st.st_mtime;
        }
    }
    if (latestPath.empty()) {
        if (errorOut)
            *errorOut = "unable to stat any candidate file";
        return false;
    }

    BreezeFileHeader_t header{};
    std::string parseError;
    if (!ReadCandidateHeader(latestPath, header, &parseError)) {
        if (errorOut)
            *errorOut = "latest file is not a valid Breeze candidate: " +
                        parseError;
        return false;
    }

    outCondition = header.search_condition;
    outFilePath = latestPath;
    return true;
}

std::string SearchConditionSummary(const Search_condition &condition) {
    const int modeValue = static_cast<int>(condition.searchMode);
    const char *modeText = (modeValue >= 0 &&
                            modeValue < static_cast<int>(sizeof(kModeNames) /
                                                         sizeof(kModeNames[0])))
                               ? kModeNames[modeValue]
                               : "unknown";
    return "type=" + std::to_string(static_cast<int>(condition.searchType)) +
           " mode=" + modeText +
           " step=" + std::to_string(static_cast<int>(condition.search_step));
}

} // namespace breeze
