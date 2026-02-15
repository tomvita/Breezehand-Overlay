/********************************************************************************
 * File: main.cpp
 * Author: ppkantorski
 * Description:
 *   This file contains the main program logic for the Ultrahand Overlay
 * project, an overlay executor designed for versatile crafting and management
 * of overlays. It defines various functions, menu structures, and interaction
 * logic to facilitate the seamless execution and customization of overlays
 * within the project.
 *
 *   Key Features:
 *   - Dynamic overlay loading and execution.
 *   - Integration with menu systems and submenus.
 *   - Configuration options through INI files.
 *   - Toggles for enabling/disabling specific commands.
 *
 *   For the latest updates and contributions, visit the project's GitHub
 * repository. (GitHub Repository:
 * https://github.com/ppkantorski/Ultrahand-Overlay)
 *
 *   Note: Please be aware that this notice cannot be altered or removed. It is
 * a part of the project's documentation and must remain intact.
 *
 *  Licensed under GPLv2
 *  Copyright (c) 2023-2026 ppkantorski
 ********************************************************************************/

#define NDEBUG
// #define STBTT_STATIC
#define TESLA_INIT_IMPL

#include "../common/breeze_search_compat.hpp"
#include "../common/breeze_search_exec.hpp"
#include "../common/search_types.hpp"
#include "keyboard.hpp"
#include <tesla.hpp>
#include <ultra.hpp>

#include "disasm.hpp"
#include <dmntcht.h>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <utils.hpp>

#ifndef USE_KEYSTONE_ASM
#define USE_KEYSTONE_ASM 0
#endif

#if USE_KEYSTONE_ASM && __has_include(<keystone/keystone.h>)
#include <keystone/keystone.h>
#define HAVE_KEYSTONE_ASM 1
#else
#define HAVE_KEYSTONE_ASM 0
#endif

using namespace ult;

// Memory ordering constants for cleaner syntax
constexpr auto acquire = std::memory_order_acquire;
constexpr auto acq_rel = std::memory_order_acq_rel;
constexpr auto release = std::memory_order_release;

static std::mutex transitionMutex;

// Forward declaration for helper to break dependency cycle
void TransitionToMainMenu(const std::string &arg1, const std::string &arg2);
void SwapToMainMenu();
static void setFooterBackLabel(const bool useRestart);

// Placeholder replacement
const std::string valuePlaceholder = "{value}";
const std::string indexPlaceholder = "{index}";
const size_t valuePlaceholderLength = valuePlaceholder.length();
const size_t indexPlaceholderLength = indexPlaceholder.length();

static std::string selectedPackage; // for package forwarders

static std::string nextToggleState;

// Overlay booleans
static bool returningToMain = false;
static bool returningToHiddenMain = false;
static bool returningToSettings = false;
static bool returningToPackage = false;
static bool returningToSubPackage = false;
static bool returningToSelectionMenu = false;
// static bool languageWasChanged = false; // moved to tsl_utils
static bool themeWasChanged = false;

// static bool skipJumpReset.store(false, release); // for overrridng the
// default main menu jump to implementation // moved to utils

// static bool inMainMenu = false; // moved to libtesla
static bool wasInHiddenMode = false;
// static bool inHiddenMode = false;
// static bool inSettingsMenu = false;
// static bool inSubSettingsMenu = false;
static bool inPackageMenu = false;
static bool inSubPackageMenu = false;
static bool inScriptMenu = false;
static bool inSelectionMenu = false;

// static bool currentMenuLoaded = true;
static bool freshSpawn = true;
// static bool refreshPage = false; //moved to libtesla
static bool reloadMenu = false;
static bool reloadMenu2 = false;
// static bool reloadMenu3 = false;
static bool triggerMenuReload = false;
static bool triggerMenuReload2 = false;
static bool inOverlay = false;
static bool toPackages = false;

static size_t nestedMenuCount = 0;

// Command mode globals
static const std::vector<std::string> commandSystems = {DEFAULT_STR, ERISTA_STR,
                                                        MARIKO_STR};

static std::vector<u32>
    g_cheatFolderIndexStack; // Indices of folder starts in the flat cheat list
static std::vector<std::string> g_cheatFolderNameStack;

static int g_cheatDownloadIndex = 0;
static int g_cheatFontSize = 17;
static bool g_searchConditionReady = false;
static Search_condition g_searchCondition{};
static std::string g_searchConditionSourcePath;
static std::string g_startSearchOutputName;
static std::string g_searchContinueSourcePath;
static std::string g_continueSearchOutputName;
static std::string g_continueSourceJumpStem;
static std::string g_pendingDeleteSeriesPath;
static bool g_pendingDeleteSeriesWaitRelease = false;
static std::string g_pendingDeleteFilePath;
static bool g_pendingDeleteFileWaitRelease = false;
static int g_continueSourceFilterMode = 1; // 1=series ends, 2=focused series
static std::string g_continueSourceFilterAnchorStem;
static bool g_lastSearchStatsValid = false;
static size_t g_lastSearchPrimaryBufferBytes = 0;
static size_t g_lastSearchSecondaryBufferBytes = 0;
static size_t g_lastSearchOutputBufferBytes = 0;
static u8 g_lastSearchBufferCount = 0;
static u32 g_lastSearchSeconds = 0;
static bool g_searchInProgress = false;
static size_t g_searchProgressPrimaryBufferBytes = 0;
static size_t g_searchProgressSecondaryBufferBytes = 0;
static size_t g_searchProgressOutputBufferBytes = 0;
static u8 g_searchProgressBufferCount = 0;
enum SearchQueuedAction : u8 { SEARCH_QUEUED_NONE = 0, SEARCH_QUEUED_START, SEARCH_QUEUED_CONTINUE };
static SearchQueuedAction g_searchQueuedAction = SEARCH_QUEUED_NONE;
static u8 g_searchQueuedDelayTicks = 0;
static SearchQueuedAction g_searchActiveAction = SEARCH_QUEUED_NONE;
static std::thread g_searchWorkerThread;
static std::atomic<bool> g_searchWorkerRunning{false};
static std::atomic<bool> g_searchWorkerDone{false};
static std::atomic<bool> g_searchPauseRequested{false};
static std::atomic<bool> g_searchAbortRequested{false};
static std::atomic<bool> g_searchWorkerPaused{false};
static std::atomic<u64> g_searchProgressCurrent{0};
static std::atomic<u64> g_searchProgressTotal{0};
static breeze::SearchRunStats g_searchWorkerStats{};
static bool g_searchWorkerSuccess = false;
static std::string g_searchWorkerError;
static std::string g_searchWorkerOutputStem;

#ifdef EDITCHEAT_OVL
u32 g_cheatIdToEdit = 0;
std::string g_cheatNameToEdit = "";
bool g_cheatEnabledToEdit = false;
std::string g_focusCheatName = ""; // For focus restoration
std::vector<u32> g_focusFolderIndices; // Folder indices for focus restoration
std::vector<std::string> g_focusFolderNames; // Folder names for focus restoration
#endif

static std::string ReplaceAll(std::string str, const std::string &from,
                              const std::string &to) {
  if (from.empty())
    return str;
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += (to.length() > 0 ? to.length() : 1);
  }
  return str;
}

static void LogDownload(const std::string &url) {
  FILE *logFile = fopen("sdmc:/config/breezehand/cheat_download.log", "a");
  if (logFile) {
    fprintf(logFile, "%s\n", url.c_str());
    fclose(logFile);
  }
}
static const std::vector<std::string> commandModes = {DEFAULT_STR,
                                                      HOLD_STR,
                                                      SLOT_STR,
                                                      TOGGLE_STR,
                                                      OPTION_STR,
                                                      FORWARDER_STR,
                                                      TEXT_STR,
                                                      TABLE_STR,
                                                      TRACKBAR_STR,
                                                      STEP_TRACKBAR_STR,
                                                      NAMED_STEP_TRACKBAR_STR};
static const std::vector<std::string> commandGroupings = {
    DEFAULT_STR, "split", "split2", "split3", "split4", "split5"};
static const std::string MODE_PATTERN = ";mode=";
static const std::string GROUPING_PATTERN = ";grouping=";
static const std::string FOOTER_PATTERN = ";footer=";
static const std::string FOOTER_HIGHLIGHT_PATTERN = ";footer_highlight=";
static const std::string HOLD_PATTERN = ";hold=";
static const std::string SYSTEM_PATTERN = ";system=";
static const std::string WIDGET_PATTERN = ";widget=";

static const std::string MINI_PATTERN = ";mini=";
static const std::string SELECTION_MINI_PATTERN = ";selection_mini=";

// Toggle option patterns
static const std::string PROGRESS_PATTERN = ";progress=";

// Table option patterns
static const std::string POLLING_PATTERN = ";polling=";
static const std::string SCROLLABLE_PATTERN = ";scrollable=";
static const std::string TOP_PIVOT_PATTERN = ";top_pivot=";
static const std::string BOTTOM_PIVOT_PATTERN = ";bottom_pivot=";
static const std::string BACKGROUND_PATTERN = ";background="; // true or false
static const std::string HEADER_INDENT_PATTERN =
    ";header_indent="; // true or false
// static const std::string HEADER_PATTERN = ";header=";
static const std::string ALIGNMENT_PATTERN = ";alignment=";
static const std::string WRAPPING_MODE_PATTERN =
    ";wrapping_mode="; // "none", "char", "word"
static const std::string WRAPPING_INDENT_PATTERN =
    ";wrapping_indent="; // true or false
static const std::string START_GAP_PATTERN = ";start_gap=";
static const std::string END_GAP_PATTERN = ";end_gap=";
static const std::string END_GAP_PATTERN_ALIAS = ";gap=";
static const std::string OFFSET_PATTERN = ";offset=";
static const std::string SPACING_PATTERN = ";spacing=";
static const std::string INFO_TEXT_COLOR_PATTERN = ";info_text_color=";
static const std::string SECTION_TEXT_COLOR_PATTERN = ";section_text_color=";

// Trackbar option patterns
static const std::string MIN_VALUE_PATTERN = ";min_value=";
static const std::string MAX_VALUE_PATTERN = ";max_value=";
static const std::string STEPS_PATTERN = ";steps=";
static const std::string UNITS_PATTERN = ";units=";
static const std::string UNLOCKED_PATTERN = ";unlocked=";
static const std::string ON_EVERY_TICK_PATTERN = ";on_every_tick=";

static const size_t SYSTEM_PATTERN_LEN = SYSTEM_PATTERN.length();
static const size_t MODE_PATTERN_LEN = MODE_PATTERN.length();
static const size_t GROUPING_PATTERN_LEN = GROUPING_PATTERN.length();
static const size_t FOOTER_PATTERN_LEN = FOOTER_PATTERN.length();
static const size_t FOOTER_HIGHLIGHT_PATTERN_LEN =
    FOOTER_HIGHLIGHT_PATTERN.length();
static const size_t HOLD_PATTERN_LEN = HOLD_PATTERN.length();
static const size_t MINI_PATTERN_LEN = MINI_PATTERN.length();
static const size_t SELECTION_MINI_PATTERN_LEN =
    SELECTION_MINI_PATTERN.length();
static const size_t PROGRESS_PATTERN_LEN = PROGRESS_PATTERN.length();
static const size_t POLLING_PATTERN_LEN = POLLING_PATTERN.length();
static const size_t SCROLLABLE_PATTERN_LEN = SCROLLABLE_PATTERN.length();
static const size_t TOP_PIVOT_PATTERN_LEN = TOP_PIVOT_PATTERN.length();
static const size_t BOTTOM_PIVOT_PATTERN_LEN = BOTTOM_PIVOT_PATTERN.length();
static const size_t BACKGROUND_PATTERN_LEN = BACKGROUND_PATTERN.length();
static const size_t HEADER_INDENT_PATTERN_LEN = HEADER_INDENT_PATTERN.length();
static const size_t START_GAP_PATTERN_LEN = START_GAP_PATTERN.length();
static const size_t END_GAP_PATTERN_LEN = END_GAP_PATTERN.length();
static const size_t END_GAP_PATTERN_ALIAS_LEN = END_GAP_PATTERN_ALIAS.length();
static const size_t OFFSET_PATTERN_LEN = OFFSET_PATTERN.length();
static const size_t SPACING_PATTERN_LEN = SPACING_PATTERN.length();
static const size_t SECTION_TEXT_COLOR_PATTERN_LEN =
    SECTION_TEXT_COLOR_PATTERN.length();
static const size_t INFO_TEXT_COLOR_PATTERN_LEN =
    INFO_TEXT_COLOR_PATTERN.length();
static const size_t ALIGNMENT_PATTERN_LEN = ALIGNMENT_PATTERN.length();
static const size_t WRAPPING_MODE_PATTERN_LEN = WRAPPING_MODE_PATTERN.length();
static const size_t WRAPPING_INDENT_PATTERN_LEN =
    WRAPPING_INDENT_PATTERN.length();
static const size_t MIN_VALUE_PATTERN_LEN = MIN_VALUE_PATTERN.length();
static const size_t MAX_VALUE_PATTERN_LEN = MAX_VALUE_PATTERN.length();
static const size_t UNITS_PATTERN_LEN = UNITS_PATTERN.length();
static const size_t STEPS_PATTERN_LEN = STEPS_PATTERN.length();
static const size_t UNLOCKED_PATTERN_LEN = UNLOCKED_PATTERN.length();
static const size_t ON_EVERY_TICK_PATTERN_LEN = ON_EVERY_TICK_PATTERN.length();
static const size_t TRUE_STR_LEN = TRUE_STR.length();
static const size_t FALSE_STR_LEN = FALSE_STR.length();

static std::string currentMenu = OVERLAYS_STR;
// static std::string lastPage = LEFT_STR;
// static std::string lastPackagePath;
// static std::string lastPackageName;
static std::string lastPackageMenu;
// static std::string lastPageHeader;

static std::string lastMenu = "";
static std::string lastMenuMode = "";
static std::string lastKeyName = "";
static bool hideUserGuide = false;
// static bool hideHidden = false; // moved to tesla.hpp
static bool hideDelete = false;
// static bool hideForceSupport = true;
static bool hideUnsupported = false;

static std::string lastCommandMode;
static bool lastCommandIsHold;
static bool lastFooterHighlight;
static bool lastFooterHighlightDefined;
static std::string g_defaultBackLabel = "Back";

static std::unordered_map<std::string, std::string> selectedFooterDict;

static tsl::elm::ListItem *selectedListItem;
static tsl::elm::ListItem *lastSelectedListItem;
// static tsl::elm::ListItem* forwarderListItem;
// static tsl::elm::ListItem* dropdownListItem;

static std::atomic<bool> lastRunningInterpreter{false};

template <typename Map,
          typename Func = std::function<std::string(const std::string &)>,
          typename... Args>
std::string getValueOrDefault(const Map &data, const std::string &key,
                              const std::string &defaultValue,
                              Func formatFunc = nullptr, Args... args) {
  auto it = data.find(key);
  if (it != data.end()) {
    return formatFunc ? formatFunc(it->second, args...) : it->second;
  }
  return defaultValue;
}

inline void clearMemory() {
  // hexSumCache = {};
  selectedFooterDict = {};
  clearIniMutexCache();
  clearHexSumCache();
  // hexSumCache.clear();
  // selectedFooterDict.clear(); // Clears all data from the map, making it
  // empty again selectedListItem = nullptr; lastSelectedListItem = nullptr;
  // forwarderListItem = nullptr;
}

// void tsl::shiftItemFocus(tsl::elm::Element* element) {
//     if (auto& currentGui = tsl::Overlay::get()->getCurrentGui()) {
//         currentGui->requestFocus(element, tsl::FocusDirection::None);
//     }
// }

/**
 * @brief Handles updates and checks when the interpreter is running.
 *
 * This function processes the progression of download, unzip, and copy
 * operations, updates the user interface accordingly, and handles the thread
 * failure and abort conditions.
 *
 * @param keysDown A bitset representing keys that are pressed down.
 * @param keysHeld A bitset representing keys that are held down.
 * @return `true` if the operation needs to abort, `false` otherwise.
 */
bool handleRunningInterpreter(uint64_t &keysDown, uint64_t &keysHeld) {
  static int lastPct = -1;
  static uint8_t lastOp = 255;
  static bool inProg = true;
  static uint8_t currentOpIndex = 0; // Track which operation to check first

  static bool wasHoldingR = false;

  const bool isHoldingR =
      keysHeld & KEY_R && !(keysHeld & ~KEY_R & ALL_KEYS_MASK);
  const bool releasedR = wasHoldingR && !(isHoldingR);
  wasHoldingR = isHoldingR;

  // FIX: More robust abort handling
  if ((releasedR && !(keysHeld & ~KEY_R & ALL_KEYS_MASK) &&
       !stillTouching.load(acquire)) ||
      externalAbortCommands.load(acquire)) {
    // Set all abort flags with proper ordering
    abortDownload.store(true, release);
    abortUnzip.store(true, release);
    abortFileOp.store(true, release);
    abortCommand.store(true, release);
    externalAbortCommands.store(false, release);
    // Reset UI state
    commandSuccess.store(false, release);
    lastPct = -1;
    lastOp = 255;
    inProg = true;
    currentOpIndex = 0; // Reset operation tracking

    return true;
  }

  // FIX: Check abort flags with acquire ordering
  if (abortDownload.load(acquire) || abortUnzip.load(acquire) ||
      abortFileOp.load(acquire) || abortCommand.load(acquire)) {
    return true;
  }

  // Other input handling
  if ((keysDown & KEY_B) && !(keysHeld & ~KEY_B & ALL_KEYS_MASK) &&
      !stillTouching.load(acquire)) {
    tsl::Overlay::get()->hide();
  }

  if (threadFailure.exchange(false, std::memory_order_acq_rel)) {
    commandSuccess.store(false, release);
  }

  // FIX: Ultra-optimized progress tracking - single operation check
  static std::atomic<int> *const pcts[] = {&downloadPercentage,
                                           &unzipPercentage, &copyPercentage};
  static const std::string *const syms[] = {&DOWNLOAD_SYMBOL, &UNZIP_SYMBOL,
                                            &COPY_SYMBOL};

  int currentPct = -1;
  uint8_t currentOp = 255;

  // If we know operations are sequential, check current index first
  int pct = pcts[currentOpIndex]->load(acquire);

  bool displayed100 = false;
  if (pct >= 0 && pct < 100) {
    // Current operation is active
    currentPct = pct;
    currentOp = currentOpIndex;
  } else if (pct == 100) {
    displayPercentage.store(100, release);
    if (lastSelectedListItem)
      lastSelectedListItem->setValue(*syms[currentOpIndex] + " 100%");
    displayed100 = true;

    // Current operation completed, mark and advance
    pcts[currentOpIndex]->store(-1, release);
    currentOpIndex = (currentOpIndex + 1) % 3;

    // Check if next operation is already active
    pct = pcts[currentOpIndex]->load(acquire);
    if (pct >= 0 && pct < 100) {
      currentPct = pct;
      currentOp = currentOpIndex;
    }
  } else {
    // Current operation not active, do a quick scan for any active operation
    for (uint8_t i = 0; i < 3; ++i) {
      pct = pcts[i]->load(acquire);
      if (pct >= 0 && pct < 100) {
        currentPct = pct;
        currentOp = i;
        currentOpIndex = i;
        break;
      }
    }
  }

  // Update UI only when necessary
  if (currentOp != 255 && (currentPct != lastPct || currentOp != lastOp)) {
    if (!displayed100) {
      displayPercentage.store(currentPct, release);
      if (lastSelectedListItem)
        lastSelectedListItem->setValue(*syms[currentOp] + " " +
                                       ult::to_string(currentPct) + "%");
    }
    lastPct = currentPct;
    lastOp = currentOp;
    inProg = true; // Reset inProg when we have active operations
  } else if (currentOp == 255 && inProg) { // Remove lastPct < 0 condition
    displayPercentage.store(-1, release);
    if (lastSelectedListItem && nextToggleState.empty())
      lastSelectedListItem->setValue(INPROGRESS_SYMBOL);
    inProg = false;
    lastPct = -1; // Reset lastPct for next cycle
  }

  return false;
}

static u64 holdStartTick = 0;
static std::string lastSelectedListItemFooter;
static std::vector<std::vector<std::string>> storedCommands;

bool processHold(uint64_t keysDown, uint64_t keysHeld, u64 &holdStartTick,
                 bool &isHolding, std::function<void()> onComplete,
                 std::function<void()> onRelease = nullptr,
                 bool resetStoredCommands = false) {
  if (!lastSelectedListItem) {
    isHolding = false;
    return false;
  }

  // Check if user is touch holding or button holding
  const bool isTouchHolding = lastSelectedListItem->isTouchHolding();
  const bool isButtonHolding = (keysHeld & KEY_A);

  if (!isTouchHolding && !isButtonHolding) {
    // Key/touch released — reset everything
    triggerExitFeedback();
    isHolding = false;
    displayPercentage.store(0, std::memory_order_release);
    runningInterpreter.store(false, std::memory_order_release);

    if (lastSelectedListItem) {
      // Reset touch hold state
      lastSelectedListItem->resetTouchHold();

      if (resetStoredCommands) {
        bool highlightParam = true;
        if (lastFooterHighlightDefined) {
          highlightParam = !lastFooterHighlight;
        } else {
          highlightParam =
              !(lastCommandMode == SLOT_STR || lastCommandMode == OPTION_STR) ||
              lastCommandMode.empty();
        }
        lastSelectedListItem->setValue(lastSelectedListItemFooter,
                                       highlightParam);
        lastSelectedListItemFooter.clear();
      } else {
        lastSelectedListItem->setValue("", true);
      }
      lastSelectedListItem = nullptr;
      lastFooterHighlight = lastFooterHighlightDefined = false;
    }

    if (resetStoredCommands) {
      storedCommands.clear();
      lastCommandMode.clear();
      lastCommandIsHold = false;
      lastKeyName.clear();
    }

    if (onRelease)
      onRelease();
    return true;
  }

  // Directional feedback (only for button input)
  if (keysDown & KEY_UP)
    lastSelectedListItem->shakeHighlight(tsl::FocusDirection::Up);
  else if (keysDown & KEY_DOWN)
    lastSelectedListItem->shakeHighlight(tsl::FocusDirection::Down);
  else if (keysDown & KEY_LEFT)
    lastSelectedListItem->shakeHighlight(tsl::FocusDirection::Left);
  else if (keysDown & KEY_RIGHT)
    lastSelectedListItem->shakeHighlight(tsl::FocusDirection::Right);

  // Update hold progress
  const u64 elapsedMs =
      armTicksToNs(armGetSystemTick() - holdStartTick) / 1000000;
  const int percentage =
      std::min(100, static_cast<int>((elapsedMs * 100) / 4000));
  displayPercentage.store(percentage, std::memory_order_release);

  if (percentage > 20 && (percentage % 30) == 0)
    triggerRumbleDoubleClick.store(true, std::memory_order_release);

  // Completed hold
  if (percentage >= 100) {
    isHolding = false;
    displayPercentage.store(-1, std::memory_order_release);

    if (lastSelectedListItem) {
      lastSelectedListItem->resetTouchHold();
      lastSelectedListItem->enableClickAnimation();
      lastSelectedListItem->triggerClickAnimation();
      lastSelectedListItem->disableClickAnimation();
    }

    if (onComplete)
      onComplete();
    return true;
  }

  return true; // Continue holding
}

static std::string returnJumpItemName;
static std::string returnJumpItemValue;

// Forward declarations
bool checkOverlayMemory(u32 requiredMB);

// Forward declaration of the MainMenu class.
class MainMenu;

class MainComboSetItem : public tsl::elm::ListItem {
private:
  u64 holdStartTick = 0;
  u64 capturedKeys = 0;
  bool capturing = false;

public:
  MainComboSetItem(const std::string &text, const std::string &value);

  virtual bool handleInput(u64 keysDown, u64 keysHeld,
                           const HidTouchState &touchState,
                           HidAnalogStickState leftJoyStick,
                           HidAnalogStickState rightJoyStick) override;
  virtual bool onClick(u64 keys) override;
};
class UltrahandSettingsMenu : public tsl::Gui {
private:
  std::string entryName, entryMode, overlayName, dropdownSelection,
      settingsIniPath;
  bool isInSection = false, inQuotes = false, isFromMainMenu = false;
  std::string languagesVersion = APP_VERSION;
  int MAX_PRIORITY = 20;
  std::string comboLabel;
  // std::string lastSelectedListItemFooter = "";
  // bool notifyRebootIsRequiredNow = false;

  bool exitOnBack = false;
  bool softwareHasUpdated = false;

  bool rightAlignmentState;

  u64 holdStartTick;
  bool isHolding = false;
  // tsl::elm::ListItem* deleteListItem = nullptr;
  bool exitItemFocused = false;

  void addListItem(tsl::elm::List *list, const std::string &title,
                   const std::string &value, const std::string &targetMenu) {
    auto *listItem = new tsl::elm::ListItem(title);
    listItem->setValue(value);
    listItem->setClickListener([listItem, targetMenu](uint64_t keys) {
      if (runningInterpreter.load(acquire))
        return false;

      if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {

        if (targetMenu == "softwareUpdateMenu") {
          deleteFileOrDirectory(SETTINGS_PATH + "RELEASE.ini");
          downloadFile(LATEST_RELEASE_INFO_URL, SETTINGS_PATH, false, true);
          // downloadPercentage.store(-1, release);
        } else if (targetMenu == "themeMenu") {
          if (!isFile(THEMES_PATH + "ultra.ini")) {
            downloadFile(INCLUDED_THEME_FOLDER_URL + "ultra.ini", THEMES_PATH,
                         false, true);
            // downloadPercentage.store(-1, release);
          }
          if (!isFile(THEMES_PATH + "ultra-blue.ini")) {
            downloadFile(INCLUDED_THEME_FOLDER_URL + "ultra-blue.ini",
                         THEMES_PATH, false, true);
            // downloadPercentage.store(-1, release);
          }
        }

        tsl::shiftItemFocus(listItem);
        tsl::changeTo<UltrahandSettingsMenu>(targetMenu);
        // selectedListItem = nullptr;
        selectedListItem = listItem;
        return true;
      }
      return false;
    });
    list->addItem(listItem);
  }

  // Remove all combos from other overlays / packages
  void removeKeyComboFromAllOthers(const std::string &keyCombo) {
    // Declare variables once for reuse across both scopes
    std::string existingCombo;
    std::string comboListStr;
    std::vector<std::string> comboList;
    bool modified;
    std::string newComboStr;

    // Process overlays first
    {
      auto overlaysIniData = getParsedDataFromIniFile(OVERLAYS_INI_FILEPATH);
      bool overlaysModified = false;

      // const auto overlayNames = getOverlayNames(); // Get all overlay names

      for (const auto &overlayName : getOverlayNames()) {
        auto overlayIt = overlaysIniData.find(overlayName);
        if (overlayIt == overlaysIniData.end())
          continue; // Skip if overlay not in INI

        auto &overlaySection = overlayIt->second;

        // 1. Remove from main key_combo field if it matches
        auto keyComboIt = overlaySection.find(KEY_COMBO_STR);
        if (keyComboIt != overlaySection.end()) {
          existingCombo = keyComboIt->second;
          if (!existingCombo.empty() &&
              tsl::hlp::comboStringToKeys(existingCombo) ==
                  tsl::hlp::comboStringToKeys(keyCombo)) {
            overlaySection[KEY_COMBO_STR] = "";
            overlaysModified = true;
          }
        }

        // 2. Remove from mode_combos list if any element matches
        auto modeCombosIt = overlaySection.find("mode_combos");
        if (modeCombosIt != overlaySection.end()) {
          comboListStr = modeCombosIt->second;
          if (!comboListStr.empty()) {
            comboList = splitIniList(comboListStr);
            modified = false;

            for (std::string &combo : comboList) {
              if (!combo.empty() && tsl::hlp::comboStringToKeys(combo) ==
                                        tsl::hlp::comboStringToKeys(keyCombo)) {
                combo.clear();
                modified = true;
              }
            }

            if (modified) {
              newComboStr = "(" + joinIniList(comboList) + ")";
              overlaySection["mode_combos"] = newComboStr;
              overlaysModified = true;
            }
          }
        }
      }

      // Write back if modified, then clear memory
      if (overlaysModified) {
        saveIniFileData(OVERLAYS_INI_FILEPATH, overlaysIniData);
      }
      // overlaysIniData automatically cleared when scope ends
    }

    // Process packages second (overlays INI data is already cleared)
    {
      auto packagesIniData = getParsedDataFromIniFile(PACKAGES_INI_FILEPATH);
      bool packagesModified = false;

      // const auto packageNames = getPackageNames(); // Get all package names

      for (const auto &packageName : getPackageNames()) {
        auto packageIt = packagesIniData.find(packageName);
        if (packageIt == packagesIniData.end())
          continue; // Skip if package not in INI

        auto &packageSection = packageIt->second;
        auto keyComboIt = packageSection.find(KEY_COMBO_STR);
        if (keyComboIt != packageSection.end()) {
          existingCombo = keyComboIt->second; // Reusing the same variable
          if (!existingCombo.empty() &&
              tsl::hlp::comboStringToKeys(existingCombo) ==
                  tsl::hlp::comboStringToKeys(keyCombo)) {
            packageSection[KEY_COMBO_STR] = "";
            packagesModified = true;
          }
        }
      }

      // Write back if modified, then clear memory
      if (packagesModified) {
        saveIniFileData(PACKAGES_INI_FILEPATH, packagesIniData);
      }
      // packagesIniData automatically cleared when scope ends
    }
  }

  void handleSelection(tsl::elm::List *list,
                       const std::vector<std::string> &items,
                       const std::string &defaultItem,
                       const std::string &iniKey,
                       const std::string &targetMenu) {
    // tsl::elm::ListItem* listItem;
    std::string mappedItem;
    for (const auto &item : items) {
      // auto mappedItem = convertComboToUnicode(item); // moved to ListItem
      // class in libTesla if (mappedItem.empty()) mappedItem = item;
      mappedItem = item;
      if (targetMenu == KEY_COMBO_STR)
        convertComboToUnicode(mappedItem);

      tsl::elm::ListItem *listItem = new tsl::elm::ListItem(mappedItem);
      if (item == defaultItem) {
        listItem->setValue(CHECKMARK_SYMBOL);
        // lastSelectedListItem = nullptr;
        lastSelectedListItem = listItem;
      }
      listItem->setClickListener([this, item, mappedItem, defaultItem, iniKey,
                                  targetMenu, listItem](uint64_t keys) {
        if (runningInterpreter.load(acquire))
          return false;

        if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
          setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
                          iniKey, item);

          if (targetMenu == KEY_COMBO_STR) {
            // Also set it in tesla config
            setIniFileValue(TESLA_CONFIG_INI_PATH, TESLA_STR, iniKey, item);

            // Remove this key combo from any overlays using it (both key_combo
            // and mode_combos)
            this->removeKeyComboFromAllOthers(item);

            // Reload the overlay key combos to reflect changes
            tsl::hlp::loadEntryKeyCombos();
          }

          reloadMenu = true;

          if (lastSelectedListItem)
            lastSelectedListItem->setValue("");
          if (selectedListItem)
            selectedListItem->setValue(mappedItem);

          listItem->setValue(CHECKMARK_SYMBOL);
          lastSelectedListItem = listItem;
          tsl::shiftItemFocus(listItem);
          if (lastSelectedListItem)
            lastSelectedListItem->triggerClickAnimation();

          return true;
        }
        return false;
      });
      list->addItem(listItem);
    }
  }

  void addUpdateButton(tsl::elm::List *list, const std::string &title,
                       const std::string &versionLabel) {
    auto *listItem = new tsl::elm::ListItem(title);
    listItem->setValue(versionLabel, true);
    if (isVersionGreaterOrEqual(versionLabel.c_str(), APP_VERSION) &&
        versionLabel != APP_VERSION)
      listItem->setValueColor(tsl::onTextColor);

    listItem->setClickListener([this, listItem, title](uint64_t keys) {
      static bool executingCommands = false;
      if (runningInterpreter.load(acquire)) {
        return false;
      } else {
        if (executingCommands && commandSuccess.load(acquire) &&
            title != UPDATE_LANGUAGES) {
          softwareHasUpdated = true;
          triggerMenuReload = true;
        }
        executingCommands = false;
      }

      if (!(keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
        return false;
      }

      // Check if we have sufficient memory for download operations (6MB required)
      if (!checkOverlayMemory(6)) {
        // User didn't increase memory, abort the operation
        return true;
      }

      executingCommands = true;
      isDownloadCommand.store(true, release);

      std::vector<std::vector<std::string>> interpreterCommands = {{"try:"}};

      // === UPDATE_ULTRAHAND case ===
      if (title == UPDATE_ULTRAHAND) {
        const std::string versionLabel = cleanVersionLabel(
            parseValueFromIniSection((SETTINGS_PATH + "RELEASE.ini"),
                                     "Release Info", "latest_version"));

        // Update via breezehand.zip
        interpreterCommands.push_back(
            {"delete", DOWNLOADS_PATH + "breezehand.zip"});
        interpreterCommands.push_back(
            {"download",
             ULTRAHAND_REPO_URL + "releases/latest/download/breezehand.zip",
             DOWNLOADS_PATH});
        interpreterCommands.push_back(
            {"unzip", DOWNLOADS_PATH + "breezehand.zip", ROOT_PATH});
        interpreterCommands.push_back(
            {"delete", DOWNLOADS_PATH + "breezehand.zip"});

        // Update version in appstore JSON if available
        if (!versionLabel.empty()) {
          interpreterCommands.push_back(
              {"set-json-val", HB_APPSTORE_JSON, "version", versionLabel});
        }
      }
      // === UPDATE_LANGUAGES case ===
      else if (title == UPDATE_LANGUAGES) {
        interpreterCommands.push_back({"delete", DOWNLOADS_PATH + "lang.zip"});
        interpreterCommands.push_back(
            {"download",
             ULTRAHAND_REPO_URL + "releases/latest/download/lang.zip",
             DOWNLOADS_PATH});
        interpreterCommands.push_back(
            {"unzip", DOWNLOADS_PATH + "lang.zip", LANG_PATH});
        interpreterCommands.push_back({"delete", DOWNLOADS_PATH + "lang.zip"});
      }

      runningInterpreter.store(true, release);
      executeInterpreterCommands(std::move(interpreterCommands), "", "");
      listItem->disableClickAnimation();
      listItem->setValue(INPROGRESS_SYMBOL);
      lastSelectedListItem = listItem;
      tsl::shiftItemFocus(listItem);
      lastRunningInterpreter.store(true, std::memory_order_release);
      if (lastSelectedListItem)
        lastSelectedListItem->triggerClickAnimation();
      return true;
    });
    list->addItem(listItem);
  }

  // Helper function to create toggle list items
  auto createToggleListItem(tsl::elm::List *list, const std::string &title,
                            bool &state, const std::string &iniKey,
                            const bool invertLogic = false,
                            const bool useReloadMenu = false,
                            const bool useReloadMenu2 = false,
                            const bool isMini = true) {

    auto *toggleListItem = new tsl::elm::ToggleListItem(
        title, invertLogic ? !state : state, ON, OFF, isMini);
    toggleListItem->setStateChangedListener(
        [this, &state, iniKey, invertLogic, useReloadMenu, useReloadMenu2,
         listItem = toggleListItem,
         firstState = std::make_shared<std::optional<bool>>()](bool newState) {
          tsl::Overlay::get()->getCurrentGui()->requestFocus(
              listItem, tsl::FocusDirection::None);

          // Calculate the actual logical state first
          const bool actualState = invertLogic ? !newState : newState;

          setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
                          iniKey, actualState ? TRUE_STR : FALSE_STR);

          // Store actualState on first click
          if (!firstState->has_value()) {
            *firstState = actualState;
          }

          if (iniKey == "page_swap") {
            triggerMenuReload = firstState->value() != state;
          } else if (iniKey == "right_alignment") {
            if (!state) {
              // const auto [horizontalUnderscanPixels, verticalUnderscanPixels]
              // = tsl::gfx::getUnderscanPixels();
              tsl::gfx::Renderer::get().setLayerPos(
                  1280 - 32 - tsl::gfx::getUnderscanPixels().first, 0);
              ult::layerEdge = (1280 - 448);
            } else {
              tsl::gfx::Renderer::get().setLayerPos(0, 0);
              ult::layerEdge = 0;
            }
          } else if (iniKey == "notifications") {
            if (!state) {
              if (!ult::isFile(NOTIFICATIONS_FLAG_FILEPATH)) {
                FILE *file =
                    std::fopen((NOTIFICATIONS_FLAG_FILEPATH).c_str(), "w");
                if (file) {
                  std::fclose(file);
                }
              }
            } else {
              deleteFileOrDirectory(NOTIFICATIONS_FLAG_FILEPATH);
            }
          } else if (iniKey == "sound_effects") {
            if (actualState) {
              Audio::initialize();
            } else {
              Audio::exit();
            }
          } else if (iniKey == "take_over_ovlmenu") {
            const std::string ovlmenuPath = OVERLAY_PATH + "ovlmenu.ovl";
            const std::string breezehandPath = OVERLAY_PATH + "breezehand.ovl";
            const std::string backupPath = OVERLAY_PATH + "ovlmenu.ovl.bak";

            if (actualState) {
              if (!isFile(breezehandPath)) {
                if (tsl::notification) {
                  tsl::notification->show(
                      "Missing breezehand.ovl\nCannot take over ovlmenu");
                }
                setIniFileValue(ULTRAHAND_CONFIG_INI_PATH,
                                ULTRAHAND_PROJECT_NAME, iniKey, FALSE_STR);
                listItem->setState(invertLogic ? true : false);
                state = false;
                return;
              }

              if (!isFile(backupPath) && isFile(ovlmenuPath)) {
                copyFileOrDirectory(ovlmenuPath, backupPath);
              }

              copyFileOrDirectory(breezehandPath, ovlmenuPath);
              if (tsl::notification) {
                tsl::notification->show("ovlmenu takeover enabled");
              }
            } else {
              if (!isFile(backupPath)) {
                if (tsl::notification) {
                  tsl::notification->show(
                      "Backup not found\nNothing to restore");
                }
              } else {
                copyFileOrDirectory(backupPath, ovlmenuPath);
                deleteFileOrDirectory(backupPath);
                if (tsl::notification) {
                  tsl::notification->show("ovlmenu restored from backup");
                }
              }
            }
          }

          state = actualState;

          if (useReloadMenu)
            reloadMenu = true;
          if (useReloadMenu2)
            reloadMenu2 = true;
        });
    list->addItem(toggleListItem);
  }

  std::vector<std::string> filesList;

public:
  UltrahandSettingsMenu(const std::string &selection = "")
      : dropdownSelection(selection) {
    lastSelectedListItemFooter = "";
  }

  ~UltrahandSettingsMenu() {
    lastSelectedListItemFooter = "";
    // tsl::elm::clearFrameCache();
  }

  virtual tsl::elm::Element *createUI() override {
    setFooterBackLabel(false);
    inSettingsMenu = dropdownSelection.empty();
    inSubSettingsMenu = !dropdownSelection.empty();

    const std::array<const std::string *, 14> defaultLanguagesRepresentation = {
        &ENGLISH,
        &SPANISH,
        &FRENCH,
        &GERMAN,
        &JAPANESE,
        &KOREAN,
        &ITALIAN,
        &DUTCH,
        &PORTUGUESE,
        &RUSSIAN,
        &UKRAINIAN,
        &POLISH,
        &SIMPLIFIED_CHINESE,
        &TRADITIONAL_CHINESE};
    const std::vector<std::string> defaultLanguages = {
        "en", "es", "fr", "de", "ja", "ko",    "it",
        "nl", "pt", "ru", "uk", "pl", "zh-cn", "zh-tw"};

    auto *list = new tsl::elm::List();

    if (dropdownSelection.empty()) {
      addHeader(list, MAIN_SETTINGS);

      // Load INI once and extract all values
      auto ultrahandIniData =
          getParsedDataFromIniFile(ULTRAHAND_CONFIG_INI_PATH);
      auto sectionIt = ultrahandIniData.find(ULTRAHAND_PROJECT_NAME);

      // Extract all values with defaults
      std::string defaultLang = "";
      std::string keyCombo = "";
      std::string currentTheme = "";
      std::string currentWallpaper = "";
      bool takeOverOvlmenuState = false;

      if (sectionIt != ultrahandIniData.end()) {
        auto langIt = sectionIt->second.find(DEFAULT_LANG_STR);
        if (langIt != sectionIt->second.end()) {
          defaultLang = langIt->second;
        }

        auto comboIt = sectionIt->second.find(KEY_COMBO_STR);
        if (comboIt != sectionIt->second.end()) {
          keyCombo = comboIt->second;
        }

        auto themeIt = sectionIt->second.find("current_theme");
        if (themeIt != sectionIt->second.end()) {
          currentTheme = themeIt->second;
        }

        if (expandedMemory) {
          auto wallpaperIt = sectionIt->second.find("current_wallpaper");
          if (wallpaperIt != sectionIt->second.end()) {
            currentWallpaper = wallpaperIt->second;
          }
        }

        auto takeOverIt = sectionIt->second.find("take_over_ovlmenu");
        if (takeOverIt != sectionIt->second.end()) {
          takeOverOvlmenuState = (takeOverIt->second == TRUE_STR);
        }
      }

      // Apply defaults and processing
      trim(keyCombo);
      defaultLang = defaultLang.empty() ? "en" : defaultLang;
      keyCombo = keyCombo.empty() ? defaultCombos[0] : keyCombo;
      convertComboToUnicode(keyCombo);
      currentTheme = (currentTheme.empty() || currentTheme == DEFAULT_STR)
                         ? DEFAULT
                         : currentTheme;
      if (expandedMemory) {
        currentWallpaper =
            (currentWallpaper.empty() || currentWallpaper == OPTION_SYMBOL)
                ? OPTION_SYMBOL
                : currentWallpaper;
      }

      auto getLanguageLabel = [&](const std::string &code) -> std::string {
        for (size_t i = 0; i < defaultLanguages.size(); ++i) {
          if (defaultLanguages[i] == code)
            return *defaultLanguagesRepresentation[i];
        }
        return code;
      };

      addListItem(list, KEY_COMBO, keyCombo, KEY_COMBO_STR);
      addListItem(list, LANGUAGE, getLanguageLabel(defaultLang),
                  "languageMenu");
      takeOverOvlmenu = takeOverOvlmenuState;
      createToggleListItem(list, "Take over ovlmenu", takeOverOvlmenu,
                           "take_over_ovlmenu", false, false, false, false);
      addListItem(list, SYSTEM, DROPDOWN_SYMBOL, "systemMenu");
      addListItem(list, SOFTWARE_UPDATE, DROPDOWN_SYMBOL, "softwareUpdateMenu");
      addHeader(list, UI_SETTINGS);
      addListItem(list, THEME, currentTheme, "themeMenu");
      if (expandedMemory) {
        addListItem(list, WALLPAPER, currentWallpaper, "wallpaperMenu");
      }
      addListItem(list, WIDGET, DROPDOWN_SYMBOL, "widgetMenu");
      addListItem(list, MISCELLANEOUS, DROPDOWN_SYMBOL, "miscMenu");

      addGap(list, 12);

    } else if (dropdownSelection == KEY_COMBO_STR) {
      addHeader(list, KEY_COMBO);
      std::string defaultCombo = parseValueFromIniSection(
          ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME, KEY_COMBO_STR);
      trim(defaultCombo);
      // handleSelection(list, defaultCombos, defaultCombo, KEY_COMBO_STR,
      // KEY_COMBO_STR);
      std::string mappedCombo =
          defaultCombo.empty() ? defaultCombos[0] : defaultCombo;
      convertComboToUnicode(mappedCombo);

      auto *setComboItem = new MainComboSetItem(KEY_COMBO, mappedCombo);
      list->addItem(setComboItem);
    } else if (dropdownSelection == "languageMenu") {
      addHeader(list, LANGUAGE);
      const std::string defaulLang = parseValueFromIniSection(
          ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME, DEFAULT_LANG_STR);
      size_t index = 0;
      std::string langFile;
      // tsl::elm::ListItem* listItem;

      for (const auto &defaultLangMode : defaultLanguages) {
        langFile = LANG_PATH + defaultLangMode + ".json";
        if (defaultLangMode != "en" && !isFile(langFile)) {
          index++;
          continue;
        }

        tsl::elm::ListItem *listItem =
            new tsl::elm::ListItem(*defaultLanguagesRepresentation[index]);

        listItem->setValue(defaultLangMode);
        if (defaultLangMode == defaulLang) {
          lastSelectedListItemFooter = defaultLangMode;
          listItem->setValue(defaultLangMode + " " + CHECKMARK_SYMBOL);
          // lastSelectedListItem = nullptr;
          lastSelectedListItem = listItem;
        }
        listItem->setClickListener([skipLang = !isFile(langFile),
                                    defaultLangMode, defaulLang, langFile,
                                    listItem](uint64_t keys) {
          // listItemPtr = std::shared_ptr<tsl::elm::ListItem>(listItem,
          // [](auto*){})](uint64_t keys) {
          if (runningInterpreter.load(acquire))
            return false;

          static bool hasNotTriggeredAnimation = false;

          if (hasNotTriggeredAnimation) { // needed because of swapTo
            listItem->triggerClickAnimation();
            hasNotTriggeredAnimation = false;
          }
          static bool triggerClick = false;

          if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
            triggerClick = true;
          }
          if (triggerClick && tsl::elm::s_currentScrollVelocity <= 1.0f &&
              tsl::elm::s_currentScrollVelocity >= -1.0f) {
            triggerClick = false;
            setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
                            DEFAULT_LANG_STR, defaultLangMode);
            reloadMenu = reloadMenu2 = true;
            parseLanguage(langFile);
            if (skipLang && defaultLangMode == "en")
              reinitializeLangVars();
            if (lastSelectedListItem)
              lastSelectedListItem->setValue(lastSelectedListItemFooter);
            if (selectedListItem)
              selectedListItem->setValue(defaultLangMode);
            listItem->setValue(defaultLangMode + " " + CHECKMARK_SYMBOL);

            // Reload font if CJK language is selected
            if (defaultLangMode == "zh-cn") {
              tsl::gfx::Renderer::get().loadLocalFont(
                  PlSharedFontType_ChineseSimplified);
            } else if (defaultLangMode == "zh-tw") {
              tsl::gfx::Renderer::get().loadLocalFont(
                  PlSharedFontType_ChineseTraditional);
            } else if (defaultLangMode == "ko") {
              tsl::gfx::Renderer::get().loadLocalFont(PlSharedFontType_KO);
            }
            // lastSelectedListItem = nullptr;
            lastSelectedListItem = listItem;
            tsl::shiftItemFocus(listItem);
            // if (lastSelectedListItem)
            //     lastSelectedListItem->triggerClickAnimation();
            lastSelectedListItemFooter = defaultLangMode;
            languageWasChanged.store(true, release);
            hasNotTriggeredAnimation = true;
            // executeCommands({
            //     {"refresh-to", "", CHECKMARK_SYMBOL, FALSE_STR}
            // });
            tsl::swapTo<UltrahandSettingsMenu>("languageMenu");

            return true;
          }
          return false;
        });
        list->addItem(listItem);
        index++;
      }
    } else if (dropdownSelection == "softwareUpdateMenu") {
      const std::string fullVersionLabel = cleanVersionLabel(
          parseValueFromIniSection((SETTINGS_PATH + "RELEASE.ini"),
                                   "Release Info", "latest_version"));

      if (isVersionGreaterOrEqual(fullVersionLabel.c_str(), APP_VERSION) &&
          fullVersionLabel != APP_VERSION && tsl::notification) {
        tsl::notification->showNow(NOTIFY_HEADER + NEW_UPDATE_IS_AVAILABLE);
      }

      addHeader(list, SOFTWARE_UPDATE);
      addUpdateButton(list, UPDATE_ULTRAHAND, fullVersionLabel);
      // addUpdateButton(list, UPDATE_LANGUAGES, fullVersionLabel);

      PackageHeader overlayHeader;
      overlayHeader.title = "Breezehand Overlay";
      overlayHeader.version = APP_VERSION;
      overlayHeader.creator = "tomvita";
      overlayHeader.about =
          "Breezehand is a powerful cheat management overlay for Nintendo "
          "Switch with enhanced UI, automatic cheat downloads with versioning, "
          "folder support, and advanced cheat organization features.";
      overlayHeader.credits =
          "Built on Ultrahand Overlay by ppkantorski. Special thanks to B3711, "
          "ComplexNarrative, ssky, MasaGratoR, meha, WerWolv, HookedBehemoth "
          "and many others. ♥";
      addPackageInfo(list, overlayHeader, OVERLAY_STR);
      overlayHeader.clear();

    } else if (dropdownSelection == "systemMenu") {

      // Version info formatting - stack allocated, optimal size
      char versionString[32];
      snprintf(versionString, sizeof(versionString), "HOS %sAMS %s",
               hosVersion, amsVersion);

      const std::string hekateVersion =
          extractVersionFromBinary("sdmc:/bootloader/update.bin");

      addHeader(list, DEVICE_INFO);

      SetSysProductModel model = SetSysProductModel_Invalid;
      setsysGetProductModel(&model);

      // Switch is optimal for enum - generates jump table on ARMv8-A
      const char *modelRev;
      switch (model) {
      case SetSysProductModel_Iowa:
        modelRev = "IowaTegra X1+ (Mariko)";
        break;
      case SetSysProductModel_Hoag:
        modelRev = "HoagTegra X1+ (Mariko)";
        break;
      case SetSysProductModel_Calcio:
        modelRev = "CalcioTegra X1+ (Mariko)";
        break;
      case SetSysProductModel_Aula:
        modelRev = "AulaTegra X1+ (Mariko)";
        break;
      case SetSysProductModel_Nx:
        modelRev = "IcosaTegra X1 (Erista)";
        break;
      case SetSysProductModel_Copper:
        modelRev = "CopperTegra X1 (Erista)";
        break;
      default:
        modelRev = UNAVAILABLE_SELECTION.c_str();
        break;
      }

      // Pre-allocate and reuse tableData - avoid repeated allocations
      std::vector<std::vector<std::string>> tableData;
      tableData.reserve(7); // Max size needed is 7 rows

      // Cache bootloader string to avoid redundant empty() check
      const std::string hekateStr =
          hekateVersion.empty() ? UNAVAILABLE_SELECTION : hekateVersion;

      tableData = {{FIRMWARE, "", versionString},
                   {"└ hekate", "", hekateStr},
                   {LOCAL_IP, "", getLocalIpAddress()}};
      addTable(list, tableData, "", 164, 20, 28, 4);

      // Hardware and storage info - clear() is cheap, reuses capacity
      tableData.clear();
      tableData = {{HARDWARE, "", modelRev},
                   {MEMORY, "", memorySize},
                   {"└ " + VENDOR, "", memoryVendor},
                   {"└ " + MODEL, "", memoryModel},
                   {STORAGE, "", usingEmunand ? "emuMMC" : "sysMMC"},
                   {"└ eMMC ", "", getStorageInfo("emmc")},
                   {"└ SD Card", "", getStorageInfo("sdmc")}};
      addTable(list, tableData, "", 164, 20, 28, 4);

      // CPU, GPU, and SOC info
      tableData.clear();
      tableData = {{"", "", "CPU      GPU      SOC"}};
      addTable(list, tableData, "", 163, 9, 3, 0, DEFAULT_STR, "section",
               "section", RIGHT_STR, true);

      tableData.clear();

      // Branchless validation check - single bitwise OR instead of 6
      // comparisons
      if ((cpuSpeedo0 | cpuSpeedo2 | socSpeedo0 | cpuIDDQ | gpuIDDQ |
           socIDDQ) != 0) {
        tableData = {{"Speedo", "",
                      customAlign(cpuSpeedo0) + " " + DIVIDER_SYMBOL + " " +
                          customAlign(cpuSpeedo2) + " " + DIVIDER_SYMBOL + " " +
                          customAlign(socSpeedo0)},
                     {"IDDQ", "",
                      customAlign(cpuIDDQ) + " " + DIVIDER_SYMBOL + " " +
                          customAlign(gpuIDDQ) + " " + DIVIDER_SYMBOL + " " +
                          customAlign(socIDDQ)}};
      } else {
        tableData = {{"Speedo", "",
                      "⋯    " + DIVIDER_SYMBOL + "    ⋯    " + DIVIDER_SYMBOL +
                          "    ⋯  "},
                     {"IDDQ", "",
                      "⋯    " + DIVIDER_SYMBOL + "    ⋯    " + DIVIDER_SYMBOL +
                          "    ⋯  "}};
      }
      addTable(list, tableData, "", 164, 20, -2, 4);

      // addHeader(list, COMMANDS);

      addGap(list, 33);

      // Get system memory info
      u64 RAM_Used_system_u, RAM_Total_system_u;
      svcGetSystemInfo(&RAM_Used_system_u, 1, INVALID_HANDLE, 2);
      svcGetSystemInfo(&RAM_Total_system_u, 0, INVALID_HANDLE, 2);

      // Stack buffer for RAM string - optimal size
      char ramString[24];
      const float freeRamMB =
          static_cast<float>(RAM_Total_system_u - RAM_Used_system_u) /
          (1024.0f * 1024.0f);
      snprintf(ramString, sizeof(ramString), "%.2f MB %s", freeRamMB,
               FREE.c_str());

      // Nested ternary compiles to conditional select (CSEL) on ARMv8-A - no
      // branches
      const char *ramColor =
          freeRamMB >= 9.0f ? "healthy_ram"
                            : (freeRamMB >= 3.0f ? "neutral_ram" : "bad_ram");

      tableData.clear();
      tableData = {{SYSTEM_RAM, "", ramString}};
      addTable(list, tableData, "", 165 + 2, 19 - 2, 19 - 2, 0, "header",
               ramColor, DEFAULT_STR, RIGHT_STR, true, true);

      // Read custom overlay memory from INI
      const std::string customMemoryStr = parseValueFromIniSection(
          ULTRAHAND_CONFIG_INI_PATH, MEMORY_STR, "custom_overlay_memory_MB");

      u32 customMemoryMB = 0;
      bool hasIniEntry = false;

      if (!customMemoryStr.empty()) {
        // Manual parsing without try-catch
        int parsedValue = 0;
        bool isValid = true;

        // Check if string contains only digits
        for (char c : customMemoryStr) {
          if (c < '0' || c > '9') {
            isValid = false;
            break;
          }
        }

        if (isValid && !customMemoryStr.empty()) {
          parsedValue = std::atoi(customMemoryStr.c_str());
          // Validate: must be even, greater than 8
          if (parsedValue > 8 && parsedValue % 2 == 0) {
            customMemoryMB = static_cast<u32>(parsedValue);
            heapSizeCache.customSizeMB = customMemoryMB;
            hasIniEntry = true;
          }
        }
      }

      // Get current heap size in MB
      const u32 currentHeapMB = bytesToMB(static_cast<u64>(currentHeapSize));

      // Check if current heap is larger than 8MB but no INI entry exists
      // This handles the case where memory was set but INI was removed/not
      // present
      if (!hasIniEntry && currentHeapMB > 8) {
        customMemoryMB = currentHeapMB;
      }

      // Create step descriptions for the trackbar
      std::vector<std::string> heapSizeLabels = {"4 MB", "6 MB", "8 MB"};

      // Add custom size if valid (either from INI or current heap > 8MB)
      if (customMemoryMB > 8) {
        heapSizeLabels.push_back(std::to_string(customMemoryMB) + " MB");
      }

      // Create the V2 trackbar
      auto *heapTrackbar = new tsl::elm::NamedStepTrackBarV2(
          OVERLAY_MEMORY,
          "", // Empty packagePath - callback will handle everything
          heapSizeLabels, nullptr, nullptr, {}, "", // No command system needed
          false, // unlockedTrackbar - can drag immediately
          false  // executeOnEveryTick - only save on release
      );

      // Set the initial position based on current heap size
      u8 initialStep = 1; // Default to 6MB (index 1)

      if (currentHeapMB == 4) {
        initialStep = 0;
      } else if (currentHeapMB == 6) {
        initialStep = 1;
      } else if (currentHeapMB == 8) {
        initialStep = 2;
      } else if (customMemoryMB > 8 && currentHeapMB == customMemoryMB) {
        initialStep = 3; // Custom size is always last (index 3 now)
      }

      // Track the last MB value the slider was at
      auto lastSliderMB = std::make_shared<u32>(currentHeapMB);

      // Use simple callback - gets called when user releases the trackbar
      heapTrackbar->setSimpleCallback([this, freeRamMB, lastSliderMB,
                                       customMemoryMB,
                                       hasIniEntry](s16 value, s16 index) {
        // Map step index → heap size
        u64 newHeapBytes;
        u32 newMB;

        switch (index) {
        case 0:
          newHeapBytes = 0x400000;
          newMB = 4;
          break; // 4MB
        case 1:
          newHeapBytes = 0x600000;
          newMB = 6;
          break; // 6MB
        case 2:
          newHeapBytes = 0x800000;
          newMB = 8;
          break; // 8MB
        case 3:
          // Only allow custom size if it came from INI entry
          if (hasIniEntry && customMemoryMB > 8) {
            newHeapBytes = mbToBytes(customMemoryMB);
            newMB = customMemoryMB;
          } else {
            return; // Invalid or temporary custom size
          }
          break;
        default:
          return;
        }

        OverlayHeapSize newHeapSize =
            static_cast<OverlayHeapSize>(newHeapBytes);

        const u32 oldMB = bytesToMB(static_cast<u64>(currentHeapSize));
        const u32 previousSliderMB = *lastSliderMB;

        // If no actual change, do nothing
        if (newMB == previousSliderMB) {
          return;
        }

        // Determine slider direction
        bool isSliderShrinking = (newMB < previousSliderMB);
        bool isSliderGrowing = (newMB > previousSliderMB);

        // If growing heap relative to ORIGINAL size, check if we have enough
        // memory
        if (newMB > oldMB) {
          // Calculate total memory that would be available after freeing
          // current heap
          const float totalAvailableMB = freeRamMB + static_cast<float>(oldMB);

          // Safety margin
          constexpr float SAFETY_MARGIN_MB = 5.3f;

          // Check if new heap size fits in total available memory
          if (static_cast<float>(newMB) >
              (totalAvailableMB - SAFETY_MARGIN_MB)) {
            // Not enough memory - REJECT the change
            if (tsl::notification) {
              tsl::notification->showNow(NOTIFY_HEADER + NOT_ENOUGH_MEMORY);
            }
            setOverlayHeapSize(currentHeapSize);
            this->exitOnBack = false;
            *lastSliderMB = newMB;
            return;
          }
        }

        // Change is allowed (either shrinking or enough memory for growth)
        setOverlayHeapSize(newHeapSize);
        this->exitOnBack = (currentHeapSize != newHeapSize);

        // Show feature notifications based on slider direction
        if (tsl::notification) {
          if (isSliderShrinking) {
            // Going down - check for disabled features
            if (previousSliderMB >= 8 && newMB < 8) {
              // Wallpaper disabled
              tsl::notification->showNow(
                  NOTIFY_HEADER + WALLPAPER_SUPPORT_DISABLED, 23);
            } else if (previousSliderMB >= 6 && newMB < 6) {
              // Sound disabled
              tsl::notification->showNow(NOTIFY_HEADER + SOUND_SUPPORT_DISABLED,
                                         23);
            }
          } else if (isSliderGrowing) {
            // Going up - check for enabled features
            if (previousSliderMB < 8 && newMB >= 8) {
              // Wallpaper enabled
              tsl::notification->showNow(
                  NOTIFY_HEADER + WALLPAPER_SUPPORT_ENABLED, 23);
            } else if (previousSliderMB < 6 && newMB >= 6) {
              // Sound enabled
              tsl::notification->showNow(NOTIFY_HEADER + SOUND_SUPPORT_ENABLED,
                                         23);
            }
          }
        }

        // Update slider position after successful change
        *lastSliderMB = newMB;
      });

      heapTrackbar->setProgress(initialStep);
      heapTrackbar->disableClickAnimation();
      list->addItem(heapTrackbar);

      addGap(list, 12);

      // Add an "Exit Overlay System" menu item
      auto *exitItem = new tsl::elm::ListItem(EXIT_OVERLAY_SYSTEM, "", true);
      exitItem->enableTouchHolding();
      exitItem->setValue("", true);

      exitItem->setClickListener([this, exitItem](uint64_t keys) {
        if ((keys & KEY_A) && !(keys & ~KEY_A & ALL_KEYS_MASK)) {

          if (!isHolding) {
            isHolding = true;
            runningInterpreter.store(true, release);
            exitItem->setValue(INPROGRESS_SYMBOL);
            lastSelectedListItem = exitItem;
            // Use touch hold start tick if available, otherwise use current
            // tick
            // if (exitItem->isTouchHolding()) {
            //    holdStartTick = exitItem->getTouchHoldStartTick();
            //} else {
            //    holdStartTick = armGetSystemTick();
            //}
            holdStartTick = armGetSystemTick();
            displayPercentage.store(1, std::memory_order_release);
          }
          return true;
        }
        return false;
      });
      exitItem->disableClickAnimation();
      list->addItem(exitItem);

      // Reboot required info
      // tableData.clear();
      // tableData = {{"", "", REBOOT_REQUIRED}};
      // addTable(list, tableData, "", 164, 28, 0, 0, DEFAULT_STR, DEFAULT_STR,
      // DEFAULT_STR, RIGHT_STR, true);

    } else if (dropdownSelection == "themeMenu") {
      addHeader(list, THEME);
      std::string currentTheme = parseValueFromIniSection(
          ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME, "current_theme");
      currentTheme = currentTheme.empty() ? DEFAULT_STR : currentTheme;
      auto *listItem = new tsl::elm::ListItem(DEFAULT);
      if (currentTheme == DEFAULT_STR) {
        listItem->setValue(CHECKMARK_SYMBOL);
        // lastSelectedListItem = nullptr;
        lastSelectedListItem = listItem;
      }
      listItem->setClickListener([defaultTheme = THEMES_PATH + "default.ini",
                                  listItem](uint64_t keys) {
        if (runningInterpreter.load(acquire))
          return false;

        if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
          setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
                          "current_theme", DEFAULT_STR);
          deleteFileOrDirectory(THEME_CONFIG_INI_PATH);
          if (isFile(defaultTheme)) {
            copyFileOrDirectory(defaultTheme, THEME_CONFIG_INI_PATH);
            copyPercentage.store(-1, release);
          } else
            initializeTheme();
          tsl::initializeThemeVars();
          reloadMenu = reloadMenu2 = true;
          if (lastSelectedListItem)
            lastSelectedListItem->setValue("");
          if (selectedListItem)
            selectedListItem->setValue(DEFAULT);
          listItem->setValue(CHECKMARK_SYMBOL);
          // lastSelectedListItem = nullptr;
          lastSelectedListItem = listItem;
          tsl::shiftItemFocus(listItem);
          if (lastSelectedListItem)
            lastSelectedListItem->triggerClickAnimation();
          themeWasChanged = true;
          return true;
        }
        return false;
      });
      list->addItem(listItem);

      filesList = getFilesListByWildcards(THEMES_PATH + "*.ini");
      std::sort(filesList.begin(), filesList.end());

      std::string themeName;
      for (const auto &themeFile : filesList) {
        themeName = getNameFromPath(themeFile);
        dropExtension(themeName);
        if (themeName == DEFAULT_STR)
          continue;

        tsl::elm::ListItem *listItem = new tsl::elm::ListItem(themeName);
        if (themeName == currentTheme) {
          listItem->setValue(CHECKMARK_SYMBOL);
          // lastSelectedListItem = nullptr;
          lastSelectedListItem = listItem;
        }
        listItem->setClickListener([themeName, themeFile,
                                    listItem](uint64_t keys) {
          if (runningInterpreter.load(acquire))
            return false;

          if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
            setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
                            "current_theme", themeName);
            // deleteFileOrDirectory(THEME_CONFIG_INI_PATH);
            copyFileOrDirectory(themeFile, THEME_CONFIG_INI_PATH);
            copyPercentage.store(-1, release);
            initializeTheme();
            tsl::initializeThemeVars();
            reloadMenu = reloadMenu2 = true;
            if (lastSelectedListItem)
              lastSelectedListItem->setValue("");
            if (selectedListItem)
              selectedListItem->setValue(themeName);
            listItem->setValue(CHECKMARK_SYMBOL);
            // lastSelectedListItem = nullptr;
            lastSelectedListItem = listItem;
            tsl::shiftItemFocus(listItem);
            if (lastSelectedListItem)
              lastSelectedListItem->triggerClickAnimation();
            themeWasChanged = true;
            return true;
          }
          return false;
        });
        list->addItem(listItem);
      }
    } else if (dropdownSelection == "wallpaperMenu") {
      addHeader(list, WALLPAPER);
      std::string currentWallpaper =
          parseValueFromIniSection(ULTRAHAND_CONFIG_INI_PATH,
                                   ULTRAHAND_PROJECT_NAME, "current_wallpaper");
      currentWallpaper =
          currentWallpaper.empty() ? OPTION_SYMBOL : currentWallpaper;

      auto *listItem = new tsl::elm::ListItem(OPTION_SYMBOL);
      if (currentWallpaper == OPTION_SYMBOL) {
        listItem->setValue(CHECKMARK_SYMBOL);
        // lastSelectedListItem = nullptr;
        lastSelectedListItem = listItem;
      }

      listItem->setClickListener([listItem](uint64_t keys) {
        if (runningInterpreter.load(acquire))
          return false;

        if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
          setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
                          "current_wallpaper", "");
          deleteFileOrDirectory(WALLPAPER_PATH);
          reloadWallpaper();
          // reloadMenu = reloadMenu2 = true;
          if (lastSelectedListItem)
            lastSelectedListItem->setValue("");
          if (selectedListItem)
            selectedListItem->setValue(OPTION_SYMBOL);
          listItem->setValue(CHECKMARK_SYMBOL);
          // lastSelectedListItem = nullptr;
          lastSelectedListItem = listItem;
          tsl::shiftItemFocus(listItem);
          if (lastSelectedListItem)
            lastSelectedListItem->triggerClickAnimation();
          return true;
        }
        return false;
      });
      list->addItem(listItem);

      filesList = getFilesListByWildcards(WALLPAPERS_PATH + "*.rgba");
      std::sort(filesList.begin(), filesList.end());

      std::string wallpaperName;
      for (const auto &wallpaperFile : filesList) {
        wallpaperName = getNameFromPath(wallpaperFile);
        dropExtension(wallpaperName);
        if (wallpaperName == DEFAULT_STR)
          continue;

        tsl::elm::ListItem *listItem = new tsl::elm::ListItem(wallpaperName);
        if (wallpaperName == currentWallpaper) {
          listItem->setValue(CHECKMARK_SYMBOL);
          // lastSelectedListItem = nullptr;
          lastSelectedListItem = listItem;
        }
        listItem->setClickListener([wallpaperName, wallpaperFile,
                                    listItem](uint64_t keys) {
          if (runningInterpreter.load(acquire))
            return false;

          if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
            setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
                            "current_wallpaper", wallpaperName);
            // deleteFileOrDirectory(THEME_CONFIG_INI_PATH);
            copyFileOrDirectory(wallpaperFile, WALLPAPER_PATH);
            copyPercentage.store(-1, release);
            reloadWallpaper();
            if (lastSelectedListItem)
              lastSelectedListItem->setValue("");
            if (selectedListItem)
              selectedListItem->setValue(wallpaperName);
            listItem->setValue(CHECKMARK_SYMBOL);
            // lastSelectedListItem = nullptr;
            lastSelectedListItem = listItem;
            tsl::shiftItemFocus(listItem);
            if (selectedListItem)
              lastSelectedListItem->triggerClickAnimation();
            return true;
          }
          return false;
        });
        list->addItem(listItem);
      }
    } else if (dropdownSelection == "widgetMenu") {
      addHeader(list, WIDGET_ITEMS);
      createToggleListItem(list, CLOCK, hideClock, "hide_clock", true);
      createToggleListItem(list, SOC_TEMPERATURE, hideSOCTemp, "hide_soc_temp",
                           true);
      createToggleListItem(list, PCB_TEMPERATURE, hidePCBTemp, "hide_pcb_temp",
                           true);
      createToggleListItem(list, BATTERY, hideBattery, "hide_battery", true);
      createToggleListItem(list, BACKDROP, hideWidgetBackdrop,
                           "hide_widget_backdrop", true);

      addHeader(list, WIDGET_SETTINGS);
      createToggleListItem(list, DYNAMIC_COLORS, dynamicWidgetColors,
                           "dynamic_widget_colors");
      createToggleListItem(list, CENTER_ALIGNMENT, centerWidgetAlignment,
                           "center_widget_alignment");
      createToggleListItem(list, EXTENDED_BACKDROP, extendedWidgetBackdrop,
                           "extended_widget_backdrop", true);

    } else if (dropdownSelection == "miscMenu") {
      // Load INI section once instead of 14 separate file reads
      auto ultrahandSection = getKeyValuePairsFromSection(
          ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME);

      // Helper lambda to safely get boolean values
      auto getBoolValue = [&](const std::string &key,
                              bool defaultValue = false) -> bool {
        auto it = ultrahandSection.find(key);
        return (it != ultrahandSection.end()) ? (it->second == TRUE_STR)
                                              : defaultValue;
      };

      addHeader(list, FEATURES);
      useLaunchCombos = getBoolValue("launch_combos", true); // TRUE_STR default
      createToggleListItem(list, LAUNCH_COMBOS, useLaunchCombos,
                           "launch_combos");
      useStartupNotification =
          getBoolValue("startup_notification", true); // TRUE_STR default
      createToggleListItem(list, STARTUP_NOTIFICATION, useStartupNotification,
                           "startup_notification");
      useNotifications =
          getBoolValue("notifications", true); // TRUE_STR default
      createToggleListItem(list, EXTERNAL_NOTIFICATIONS, useNotifications,
                           "notifications");

      if (!ult::limitedMemory) {
        useSoundEffects =
            getBoolValue("sound_effects", false); // TRUE_STR default
        createToggleListItem(list, SOUND_EFFECTS, useSoundEffects,
                             "sound_effects");
      }
      useHapticFeedback =
          getBoolValue("haptic_feedback", false); // FALSE_STR default
      createToggleListItem(list, HAPTIC_FEEDBACK, useHapticFeedback,
                           "haptic_feedback");
      useOpaqueScreenshots =
          getBoolValue("opaque_screenshots", true); // TRUE_STR default
      createToggleListItem(list, OPAQUE_SCREENSHOTS, useOpaqueScreenshots,
                           "opaque_screenshots");
      useSwipeToOpen = getBoolValue("swipe_to_open", true); // TRUE_STR default
      createToggleListItem(list, SWIPE_TO_OPEN, useSwipeToOpen,
                           "swipe_to_open");
      rightAlignmentState = useRightAlignment =
          getBoolValue("right_alignment"); // FALSE_STR default
      createToggleListItem(list, RIGHT_SIDE_MODE, useRightAlignment,
                           "right_alignment");

      addHeader(list, MENU_SETTINGS);
      hideUserGuide =
          getBoolValue("hide_user_guide", false); // FALSE_STR default
      createToggleListItem(list, USER_GUIDE, hideUserGuide, "hide_user_guide",
                           true, true, true);
      hideHidden = getBoolValue("hide_hidden", false); // FALSE_STR default
      createToggleListItem(list, SHOW_HIDDEN, hideHidden, "hide_hidden", true,
                           true);
      hideDelete = getBoolValue("hide_delete", false); // FALSE_STR default
      createToggleListItem(list, SHOW_DELETE, hideDelete, "hide_delete", true);
      if (requiresLNY2) {
        // hideForceSupport = getBoolValue("hide_force_support", true); //
        // FALSE_STR default createToggleListItem(list, "Show Force Support",
        // hideForceSupport, "hide_force_support", true);
        hideUnsupported =
            getBoolValue("hide_unsupported", false); // FALSE_STR default
        createToggleListItem(list, SHOW_UNSUPPORTED, hideUnsupported,
                             "hide_unsupported", true, true, true);
      }
      usePageSwap = getBoolValue("page_swap", false); // FALSE_STR default
      createToggleListItem(list, PAGE_SWAP, usePageSwap, "page_swap", false,
                           true);

      hideOverlayVersions =
          getBoolValue("hide_overlay_versions", false); // FALSE_STR default
      createToggleListItem(list, OVERLAY_VERSIONS, hideOverlayVersions,
                           "hide_overlay_versions", true, true);
      hidePackageVersions =
          getBoolValue("hide_package_versions", false); // FALSE_STR default
      createToggleListItem(list, PACKAGE_VERSIONS, hidePackageVersions,
                           "hide_package_versions", true, true);
      cleanVersionLabels =
          getBoolValue("clean_version_labels", false); // FALSE_STR default
      createToggleListItem(list, CLEAN_VERSIONS, cleanVersionLabels,
                           "clean_version_labels", false, true, true);

      addHeader(list, THEME_SETTINGS);
      useDynamicLogo = getBoolValue("dynamic_logo", true); // TRUE_STR default
      createToggleListItem(list, DYNAMIC_LOGO, useDynamicLogo, "dynamic_logo");
      useSelectionBG = getBoolValue("selection_bg", true); // TRUE_STR default
      createToggleListItem(list, SELECTION_BACKGROUND, useSelectionBG,
                           "selection_bg", false, true);
      useSelectionText =
          getBoolValue("selection_text", false); // TRUE_STR default
      createToggleListItem(list, SELECTION_TEXT, useSelectionText,
                           "selection_text", false, true);
      useSelectionValue =
          getBoolValue("selection_value", false); // FALSE_STR default
      createToggleListItem(list, SELECTION_VALUE, useSelectionValue,
                           "selection_value", false, true);
      useLibultrahandTitles =
          getBoolValue("libultrahand_titles", false); // FALSE_STR default
      createToggleListItem(list, LIBULTRAHAND_TITLES, useLibultrahandTitles,
                           "libultrahand_titles", false, true);
      useLibultrahandVersions =
          getBoolValue("libultrahand_versions", true); // TRUE_STR default
      createToggleListItem(list, LIBULTRAHAND_VERSIONS, useLibultrahandVersions,
                           "libultrahand_versions", false, true);
      usePackageTitles =
          getBoolValue("package_titles", false); // TRUE_STR default
      createToggleListItem(list, PACKAGE_TITLES, usePackageTitles,
                           "package_titles", false, true);
      usePackageVersions =
          getBoolValue("package_versions", true); // TRUE_STR default
      createToggleListItem(list, PACKAGE_VERSIONS, usePackageVersions,
                           "package_versions", false, true);

      // addHeader(list, "Version Labels");

    } else {
      addBasicListItem(list, FAILED_TO_OPEN + ": " + settingsIniPath);
    }

    auto *rootFrame = new tsl::elm::OverlayFrame(CAPITAL_ULTRAHAND_PROJECT_NAME,
                                                 versionLabel);
    if (inSubSettingsMenu && ((dropdownSelection == "languageMenu") ||
                              (dropdownSelection == KEY_COMBO_STR) ||
                              (dropdownSelection == "themeMenu") ||
                              (dropdownSelection == "wallpaperMenu"))) {
      {
        // std::lock_guard<std::mutex> lock(jumpItemMutex);
        jumpItemName = "";
        jumpItemValue = CHECKMARK_SYMBOL;
        jumpItemExactMatch.store(false, release);
        // g_overlayFilename = "";
      }
      // list->jumpToItem(jumpItemName, jumpItemValue,
      // jumpItemExactMatch.load(acquire));
    } else {
      if (languageWasChanged.exchange(false, std::memory_order_acq_rel)) {
        {
          // std::lock_guard<std::mutex> lock(jumpItemMutex);
          jumpItemName = LANGUAGE;
          jumpItemValue = "";
          jumpItemExactMatch.store(true, release);
          // g_overlayFilename = "";
        }
        // languageWasChanged.store(false, release);
        // list->jumpToItem(jumpItemName, jumpItemValue,
        // jumpItemExactMatch.load(acquire));
      } else if (themeWasChanged) {
        {
          // std::lock_guard<std::mutex> lock(jumpItemMutex);
          jumpItemName = THEME;
          jumpItemValue = "";
          jumpItemExactMatch.store(true, release);
          // g_overlayFilename = "";
        }
        themeWasChanged = false;
        // list->jumpToItem(jumpItemName, jumpItemValue,
        // jumpItemExactMatch.load(acquire));
      } else {
        {
          // std::lock_guard<std::mutex> lock(jumpItemMutex);
          jumpItemName = "";
          jumpItemValue = "";
          jumpItemExactMatch.store(true, release);
          // g_overlayFilename = "";
        }
        // list->jumpToItem(jumpItemName, jumpItemValue,
        // jumpItemExactMatch.load(acquire));
      }
    }
    list->jumpToItem(jumpItemName, jumpItemValue,
                     jumpItemExactMatch.load(acquire));

    rootFrame->setContent(list);
    return rootFrame;

    // return returnRootFrame(list, CAPITAL_ULTRAHAND_PROJECT_NAME,
    // versionLabel);
  }

  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override;
};

int settingsMenuPageDepth = 0;
std::string rootEntryName, rootEntryMode, rootTitle, rootVersion;

bool modeComboModified = false;

class SettingsMenu : public tsl::Gui {
private:
  std::string entryName, entryMode, title, version, dropdownSelection,
      settingsIniPath;

  bool requiresAMS110Handling = false;

  bool isInSection, inQuotes, isFromMainMenu;
  int MAX_PRIORITY = 20;

  std::string modeTitle;

  u64 holdStartTick;
  bool isHolding = false;
  // tsl::elm::ListItem* deleteListItem = nullptr;
  bool deleteItemFocused = false;

public:
  SettingsMenu(const std::string &name, const std::string &mode,
               const std::string &title = "", const std::string &version = "",
               const std::string &selection = "",
               bool _requiresAMS110Handling = false)
      : entryName(name), entryMode(mode), title(title), version(version),
        dropdownSelection(selection),
        requiresAMS110Handling(_requiresAMS110Handling) {
    // Only store once
    if (settingsMenuPageDepth == 0) {
      rootEntryName = name;
      rootEntryMode = mode;
      rootTitle = title;
      rootVersion = version;
    }
    settingsMenuPageDepth++;
  }

  ~SettingsMenu() {
    if (settingsMenuPageDepth > 0) {
      settingsMenuPageDepth--;
    }
    lastSelectedListItem = nullptr;
    // tsl::elm::clearFrameCache();
  }

  void createAndAddToggleListItem(tsl::elm::List *list,
                                  const std::string &label, bool initialState,
                                  const std::string &iniKey,
                                  std::string currentValue,
                                  const std::string &settingsIniPath,
                                  const std::string &entryName,
                                  bool handleReload = false) {
    if (currentValue.empty() && !initialState)
      currentValue = FALSE_STR;

    // auto forceSupportNotificationShown = std::make_shared<bool>(false);

    auto *toggleListItem =
        new tsl::elm::ToggleListItem(label, initialState, ON, OFF);
    toggleListItem->setState(currentValue != FALSE_STR);
    toggleListItem->setStateChangedListener(
        [this, iniKey, listItem = toggleListItem, handleReload](bool state) {
          // toggleListItem->setStateChangedListener([this, iniKey, listItem =
          // toggleListItem, handleReload, forceSupportNotificationShown](bool
          // state) {
          tsl::Overlay::get()->getCurrentGui()->requestFocus(
              listItem, tsl::FocusDirection::None);
          setIniFileValue(this->settingsIniPath, this->entryName, iniKey,
                          state ? TRUE_STR : FALSE_STR);
          if (handleReload) {
            reloadMenu = state || (reloadMenu2 = !state);
          }
          if (iniKey == "force_support") {
            if (state && tsl::notification) {
              // First time for THIS notification OR wait until not active
              // if (!*forceSupportNotificationShown) {
              //    tsl::notification->showNow(NOTIFY_HEADER+FORCED_SUPPORT_WARNING,
              //    20); *forceSupportNotificationShown = true;
              //}
              tsl::notification->showNow(NOTIFY_HEADER + FORCED_SUPPORT_WARNING,
                                         20);
            }
          }
        });
    list->addItem(toggleListItem);
  }

  void createAndAddListItem(tsl::elm::List *list, const std::string &iStr,
                            const std::string &priorityValue,
                            const std::string &settingsIniPath,
                            const std::string &entryName, bool isMini = false) {
    auto *listItem = new tsl::elm::ListItem(iStr, "", isMini);

    if (iStr == priorityValue) {
      listItem->setValue(CHECKMARK_SYMBOL);
      lastSelectedListItem = listItem;
    }

    listItem->setClickListener([settingsIniPath = settingsIniPath,
                                entryName = entryName, iStr, priorityValue,
                                listItem](uint64_t keys) {
      if (runningInterpreter.load(acquire))
        return false;

      if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
        if (iStr != priorityValue) {
          reloadMenu = true; // Modify the global variable
        } else {
          reloadMenu = false;
        }

        setIniFileValue(settingsIniPath, entryName, PRIORITY_STR, iStr);
        if (lastSelectedListItem)
          lastSelectedListItem->setValue("");
        if (selectedListItem)
          selectedListItem->setValue(iStr);
        listItem->setValue(CHECKMARK_SYMBOL);
        lastSelectedListItem = listItem;
        tsl::shiftItemFocus(listItem);
        if (lastSelectedListItem)
          lastSelectedListItem->triggerClickAnimation();
      }
      return false;
    });

    list->addItem(listItem);
  }

  void addDeleteItem(tsl::elm::List *list, bool isOverlay) {
    // static const std::vector<std::vector<std::string>> tableData = {
    //     {"", "", ""}
    // };
    // addTable(list, tableData, "", 165, 0, 10, 0, DEFAULT_STR, DEFAULT_STR,
    // DEFAULT_STR, RIGHT_STR, true, false, false, true, "none", false);

    addGap(list, 12);

    auto *deleteListItem =
        new tsl::elm::ListItem(isOverlay ? DELETE_OVERLAY : DELETE_PACKAGE);
    deleteListItem->setValue("", true);

    deleteListItem->setClickListener(
        [this, deleteListItem](uint64_t keys) -> bool {
          if (runningInterpreter.load(std::memory_order_acquire))
            return false;

          // Start holding when A key is first pressed
          if ((keys & KEY_A) && !(keys & ~KEY_A & ALL_KEYS_MASK)) {
            if (!isHolding) {
              isHolding = true;
              runningInterpreter.store(true, release);
              deleteListItem->setValue(INPROGRESS_SYMBOL);
              lastSelectedListItem = deleteListItem;
              holdStartTick = armGetSystemTick();
              displayPercentage.store(1, std::memory_order_release);
            }
            return true;
          }

          return false;
        });
    deleteListItem->disableClickAnimation();

    list->addItem(deleteListItem);
  }

  // Helper lambdas and common setup
  virtual tsl::elm::Element *createUI() override {
    setFooterBackLabel(false);
    settingsIniPath = (entryMode == OVERLAY_STR) ? OVERLAYS_INI_FILEPATH
                                                 : PACKAGES_INI_FILEPATH;
    const std::string header =
        title + (!version.empty() ? (" " + version) : "");
    inSettingsMenu = dropdownSelection.empty();
    inSubSettingsMenu = !dropdownSelection.empty();

    // Load settings INI once
    auto settingsData = getParsedDataFromIniFile(settingsIniPath);
    auto secIt = settingsData.find(entryName);

    // Optimized value getter
    auto getValue = [&](const std::string &key) -> std::string {
      return (secIt != settingsData.end() && secIt->second.count(key))
                 ? secIt->second.at(key)
                 : "";
    };

    // Common click handler for navigation
    auto navClick = [this](const std::string &name, const std::string &mode,
                           const std::string &overlayName,
                           const std::string &overlayVersion,
                           const std::string &selection,
                           tsl::elm::ListItem *item) {
      return [=](uint64_t keys) -> bool {
        if (runningInterpreter.load(acquire))
          return false;
        if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
          inMainMenu.store(false, std::memory_order_release);
          tsl::shiftItemFocus(item);
          tsl::changeTo<SettingsMenu>(name, mode, overlayName, overlayVersion,
                                      selection);
          selectedListItem = item;
          if (lastSelectedListItem)
            lastSelectedListItem->triggerClickAnimation();
          return true;
        }
        return false;
      };
    };

    auto *list = new tsl::elm::List();

    if (inSettingsMenu) {
      addHeader(list, SETTINGS + " " + DIVIDER_SYMBOL + " " + header);

      // Key Combo Item
      {
        const std::string currentCombo = getValue(KEY_COMBO_STR);
        std::string displayCombo =
            currentCombo.empty() ? OPTION_SYMBOL : currentCombo;
        if (!currentCombo.empty())
          convertComboToUnicode(displayCombo);

        auto *item = new tsl::elm::ListItem(KEY_COMBO);
        item->setValue(displayCombo);
        item->setClickListener(navClick(entryName, entryMode, title, version,
                                        KEY_COMBO_STR, item));
        list->addItem(item);
      }

      // Hide Toggle
      createAndAddToggleListItem(
          list, (entryMode == OVERLAY_STR) ? HIDE_OVERLAY : HIDE_PACKAGE, false,
          HIDE_STR, getValue(HIDE_STR), settingsIniPath, entryName, true);

      // Sort Priority Item
      {
        auto *item = new tsl::elm::ListItem(SORT_PRIORITY);
        item->setValue(getValue(PRIORITY_STR));
        item->setClickListener(
            navClick(entryName, entryMode, title, version, PRIORITY_STR, item));
        list->addItem(item);
      }

      // Mode-specific items
      if (entryMode == OVERLAY_STR) {
        createAndAddToggleListItem(
            list, LAUNCH_ARGUMENTS, false, USE_LAUNCH_ARGS_STR,
            getValue(USE_LAUNCH_ARGS_STR), settingsIniPath, entryName);

        const std::vector<std::string> modeList =
            splitIniList(getValue("mode_args"));
        if (!modeList.empty()) {
          auto *item = new tsl::elm::ListItem(LAUNCH_MODES);
          item->setValue(DROPDOWN_SYMBOL);
          item->setClickListener(
              navClick(entryName, entryMode, title, version, MODE_STR, item));
          list->addItem(item);
        }
        // if (!hideForceSupport && requiresLNY2 && requiresAMS110Handling) {
        if (requiresLNY2 && requiresAMS110Handling) {
          createAndAddToggleListItem(list, FORCE_AMS110_SUPPORT, false,
                                     "force_support", getValue("force_support"),
                                     settingsIniPath, entryName, true);
        }
      } else if (entryMode == PACKAGE_STR) {
        auto *item = new tsl::elm::ListItem(OPTIONS);
        item->setValue(DROPDOWN_SYMBOL);
        item->setClickListener(
            navClick(entryName, entryMode, title, version, "options", item));
        list->addItem(item);
      }

      if (!hideDelete)
        addDeleteItem(list, (entryMode == OVERLAY_STR));

    } else if (dropdownSelection == MODE_STR) {
      const std::vector<std::string> modeList =
          splitIniList(getValue("mode_args"));
      const std::vector<std::string> comboList =
          splitIniList(getValue("mode_combos"));
      const std::vector<std::string> labelList =
          splitIniList(getValue("mode_labels"));

      if (!modeList.empty()) {
        addTable(list, {{MODE, "", KEY_COMBO}}, "", 165 + 2, 19 - 2, 19 - 2, 0,
                 "header", "header", DEFAULT_STR, RIGHT_STR, true, true, false,
                 true, "none", false);

        std::vector<std::string> combos = comboList;
        if (combos.size() < modeList.size())
          combos.resize(modeList.size(), "");

        for (size_t i = 0; i < modeList.size(); ++i) {
          const std::string &mode = modeList[i];
          const std::string displayName =
              (i < labelList.size() && !labelList[i].empty()) ? labelList[i]
                                                              : mode;
          std::string comboDisplay =
              combos[i].empty() ? OPTION_SYMBOL : combos[i];
          convertComboToUnicode(comboDisplay);

          auto *item = new tsl::elm::ListItem(displayName);
          item->setValue(comboDisplay);
          item->setClickListener([=, this](uint64_t keys) -> bool {
            if (runningInterpreter.load(acquire))
              return false;
            if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
              inMainMenu.store(false, std::memory_order_release);

              tsl::shiftItemFocus(item);
              tsl::changeTo<SettingsMenu>(entryName, OVERLAY_STR, mode, "",
                                          "mode_combo_" + std::to_string(i));
              selectedListItem = item;
              if (lastSelectedListItem)
                lastSelectedListItem->triggerClickAnimation();
              return true;
            }
            return false;
          });
          list->addItem(item);
        }
      }

    } else if (dropdownSelection == "options") {
      addHeader(list, "Options");
      createAndAddToggleListItem(
          list, QUICK_LAUNCH, false, USE_QUICK_LAUNCH_STR,
          getValue(USE_QUICK_LAUNCH_STR), settingsIniPath, entryName);
      createAndAddToggleListItem(
          list, BOOT_COMMANDS, true, USE_BOOT_PACKAGE_STR,
          getValue(USE_BOOT_PACKAGE_STR), settingsIniPath, entryName);
      createAndAddToggleListItem(
          list, EXIT_COMMANDS, true, USE_EXIT_PACKAGE_STR,
          getValue(USE_EXIT_PACKAGE_STR), settingsIniPath, entryName);
      createAndAddToggleListItem(list, ERROR_LOGGING, false, USE_LOGGING_STR,
                                 getValue(USE_LOGGING_STR), settingsIniPath,
                                 entryName);

    } else if (dropdownSelection == PRIORITY_STR) {
      addHeader(list, SORT_PRIORITY);
      const std::string priorityValue = getValue(PRIORITY_STR);
      for (int i = 0; i <= MAX_PRIORITY; ++i) {
        createAndAddListItem(list, ult::to_string(i), priorityValue,
                             settingsIniPath, entryName, true);
      }

    } else if (dropdownSelection == KEY_COMBO_STR) {
      addHeader(list, KEY_COMBO);
      const std::string currentCombo = getValue(KEY_COMBO_STR);

      // Get global default combo
      auto uhData = getParsedDataFromIniFile(ULTRAHAND_CONFIG_INI_PATH);
      auto uhIt = uhData.find(ULTRAHAND_PROJECT_NAME);
      std::string globalDefault =
          (uhIt != uhData.end() && uhIt->second.count(KEY_COMBO_STR))
              ? uhIt->second.at(KEY_COMBO_STR)
              : "";
      trim(globalDefault);

      // No combo option
      {
        auto *item = new tsl::elm::ListItem(OPTION_SYMBOL);
        if (currentCombo.empty()) {
          item->setValue(CHECKMARK_SYMBOL);
          lastSelectedListItem = item;
        }
        item->setClickListener([=, this](uint64_t keys) -> bool {
          if (runningInterpreter.load(acquire))
            return false;
          if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
            setIniFileValue(settingsIniPath, entryName, KEY_COMBO_STR, "");
            tsl::hlp::loadEntryKeyCombos();
            reloadMenu2 = true;
            if (lastSelectedListItem)
              lastSelectedListItem->setValue("");
            if (selectedListItem)
              selectedListItem->setValue(OPTION_SYMBOL);
            item->setValue(CHECKMARK_SYMBOL);
            lastSelectedListItem = item;
            tsl::shiftItemFocus(item);
            if (lastSelectedListItem)
              lastSelectedListItem->triggerClickAnimation();
            return true;
          }
          return false;
        });
        list->addItem(item);
      }

      // Predefined combos
      std::string mapped;
      for (const auto &combo : defaultCombos) {
        if (combo == globalDefault)
          continue;

        mapped = combo;
        convertComboToUnicode(mapped);

        auto *item = new tsl::elm::ListItem(mapped);
        if (combo == currentCombo) {
          item->setValue(CHECKMARK_SYMBOL);
          lastSelectedListItem = item;
        }
        item->setClickListener([=, this](uint64_t keys) -> bool {
          if (runningInterpreter.load(acquire))
            return false;
          if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
            if (combo != currentCombo) {
              removeKeyComboFromOthers(combo, entryName);
              setIniFileValue(settingsIniPath, entryName, KEY_COMBO_STR, combo);
              tsl::hlp::loadEntryKeyCombos();
            }
            reloadMenu2 = true;
            if (lastSelectedListItem)
              lastSelectedListItem->setValue("");
            if (selectedListItem)
              selectedListItem->setValue(mapped);
            item->setValue(CHECKMARK_SYMBOL);
            lastSelectedListItem = item;
            tsl::shiftItemFocus(item);
            if (lastSelectedListItem)
              lastSelectedListItem->triggerClickAnimation();
            return true;
          }
          return false;
        });
        list->addItem(item);
      }

    } else if (dropdownSelection.rfind("mode_combo_", 0) == 0) {
      const size_t idx =
          std::stoi(dropdownSelection.substr(11)); // 11 = strlen("mode_combo_")

      const std::vector<std::string> labelList =
          splitIniList(getValue("mode_labels"));
      const std::string labelText =
          (idx < labelList.size() && !labelList[idx].empty())
              ? labelList[idx]
              : "'" + title + "'";
      modeTitle = (idx < labelList.size() && !labelList[idx].empty())
                      ? labelList[idx]
                      : title;

      addHeader(list, KEY_COMBO + " " + DIVIDER_SYMBOL + " " + labelText);

      std::vector<std::string> comboList =
          splitIniList(getValue("mode_combos"));
      if (idx >= comboList.size())
        comboList.resize(idx + 1, "");
      const std::string currentCombo = comboList[idx];

      // Get global default
      auto uhData = getParsedDataFromIniFile(ULTRAHAND_CONFIG_INI_PATH);
      auto uhIt = uhData.find(ULTRAHAND_PROJECT_NAME);
      std::string globalDefault =
          (uhIt != uhData.end() && uhIt->second.count(KEY_COMBO_STR))
              ? uhIt->second.at(KEY_COMBO_STR)
              : "";
      trim(globalDefault);

      // No combo option
      {
        auto *item = new tsl::elm::ListItem(OPTION_SYMBOL);
        if (currentCombo.empty()) {
          item->setValue(CHECKMARK_SYMBOL);
          lastSelectedListItem = item;
        }
        item->setClickListener([=, this](uint64_t keys) mutable -> bool {
          if (runningInterpreter.load(acquire))
            return false;
          if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
            auto combos = comboList; // copy for modification
            combos[idx] = "";
            const std::string newStr = "(" + joinIniList(combos) + ")";
            removeKeyComboFromOthers(newStr, entryName);
            setIniFileValue(settingsIniPath, entryName, "mode_combos", newStr);
            tsl::hlp::loadEntryKeyCombos();
            modeComboModified = true;
            if (lastSelectedListItem)
              lastSelectedListItem->setValue("");
            if (selectedListItem)
              selectedListItem->setValue(OPTION_SYMBOL);
            item->setValue(CHECKMARK_SYMBOL);
            lastSelectedListItem = item;
            tsl::shiftItemFocus(item);
            if (lastSelectedListItem)
              lastSelectedListItem->triggerClickAnimation();
            return true;
          }
          return false;
        });
        list->addItem(item);
      }

      // Valid combos
      std::string mapped;
      for (const auto &combo : defaultCombos) {
        if (combo == globalDefault)
          continue;

        mapped = combo;
        convertComboToUnicode(mapped);

        auto *item = new tsl::elm::ListItem(mapped);
        if (combo == currentCombo) {
          item->setValue(CHECKMARK_SYMBOL);
          lastSelectedListItem = item;
        }
        item->setClickListener([=, this](uint64_t keys) mutable -> bool {
          if (runningInterpreter.load(acquire))
            return false;
          if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
            if (combo != comboList[idx]) {
              removeKeyComboFromOthers(combo, entryName);
              const std::string comboStr = parseValueFromIniSection(
                  settingsIniPath, entryName, "mode_combos");
              auto combos = splitIniList(comboStr);
              if (idx >= combos.size())
                combos.resize(idx + 1, "");
              combos[idx] = combo;
              setIniFileValue(settingsIniPath, entryName, "mode_combos",
                              "(" + joinIniList(combos) + ")");
              tsl::hlp::loadEntryKeyCombos();
            }
            modeComboModified = true;
            if (lastSelectedListItem)
              lastSelectedListItem->setValue("");
            if (selectedListItem)
              selectedListItem->setValue(mapped);
            item->setValue(CHECKMARK_SYMBOL);
            lastSelectedListItem = item;
            tsl::shiftItemFocus(item);
            if (lastSelectedListItem)
              lastSelectedListItem->triggerClickAnimation();
            return true;
          }
          return false;
        });
        list->addItem(item);
      }
    } else {
      addBasicListItem(list, FAILED_TO_OPEN + ": " + settingsIniPath);
    }

    auto *rootFrame = new tsl::elm::OverlayFrame(CAPITAL_ULTRAHAND_PROJECT_NAME,
                                                 versionLabel);

    if (inSubSettingsMenu && (dropdownSelection == KEY_COMBO_STR ||
                              dropdownSelection == PRIORITY_STR ||
                              dropdownSelection.rfind("mode_combo_", 0) == 0)) {
      jumpItemName = "";
      jumpItemValue = CHECKMARK_SYMBOL;
      jumpItemExactMatch.store(true, release);
      // g_overlayFilename = "";
    }

    list->jumpToItem(jumpItemName, jumpItemValue,
                     jumpItemExactMatch.load(acquire));
    rootFrame->setContent(list);
    return rootFrame;
  }

  /**
   * @brief Handles user input for the configuration overlay.
   *
   * This function processes user input and responds accordingly within the
   * configuration overlay. It captures key presses and performs actions based
   * on user interactions.
   *
   * @param keysDown   A bitset representing keys that are currently pressed.
   * @param keysHeld   A bitset representing keys that are held down.
   * @param touchInput Information about touchscreen input.
   * @param leftJoyStick Information about the left joystick input.
   * @param rightJoyStick Information about the right joystick input.
   * @return `true` if the input was handled within the overlay, `false`
   * otherwise.
   */
  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override;
};

// For persistent versions and colors across nested packages (when not
// specified)
std::string packageRootLayerTitle;
std::string packageRootLayerName;
std::string packageRootLayerVersion;
std::string packageRootLayerColor;
// bool packageRootLayerIsStarred = false;

// std::string lastPackageHeader;

bool overrideTitle = false, overrideVersion = false;

class ScriptOverlay : public tsl::Gui {
private:
  std::vector<std::vector<std::string>> commands;
  std::string filePath, specificKey;
  bool isInSection = false, inQuotes = false, isFromMainMenu = false,
       isFromPackage = false, isFromSelectionMenu = false;
  bool tableMode = false;

  std::string lastPackageHeader;
  bool showWidget = false;

  // u64 holdStartTick;
  // bool isHolding = false;

  void addListItem(tsl::elm::List *list, const std::string &line) {
    auto *listItem = new tsl::elm::ListItem(line);

    listItem->setClickListener([filePath = filePath, specificKey = specificKey,
                                listItem, line](uint64_t keys) {
      if (runningInterpreter.load(acquire))
        return false;

      if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {

        std::vector<std::vector<std::string>> commandVec;
        std::vector<std::string> commandParts;
        std::string currentPart;
        bool inQuotes = false;

        for (char ch : line) {
          if (ch == '\'') {
            inQuotes = !inQuotes;
            if (!inQuotes) {
              commandParts.emplace_back(std::move(currentPart));
              // currentPart.clear();
              currentPart.shrink_to_fit();
            }
          } else if (ch == ' ' && !inQuotes) {
            if (!currentPart.empty()) {
              commandParts.emplace_back(std::move(currentPart));
              // currentPart.clear();
              currentPart.shrink_to_fit();
            }
          } else {
            currentPart += ch;
          }
        }
        if (!currentPart.empty()) {
          commandParts.emplace_back(std::move(currentPart));
        }

        commandVec.emplace_back(std::move(commandParts));
        commandParts.shrink_to_fit();

        executeInterpreterCommands(std::move(commandVec), filePath,
                                   specificKey);
        listItem->disableClickAnimation();
        // startInterpreterThread();
        listItem->setValue(INPROGRESS_SYMBOL);

        lastSelectedListItem = listItem;

        lastRunningInterpreter.store(true, std::memory_order_release);
        listItem->triggerClickAnimation();
        return true;
      }
      return false;
    });
    list->addItem(listItem);
  }

public:
  ScriptOverlay(std::vector<std::vector<std::string>> &&cmds,
                const std::string &file, const std::string &key = "",
                const std::string &fromMenu = "", bool tableMode = false,
                const std::string &_lastPackageHeader = "",
                bool showWidget = false)
      : commands(cmds), filePath(file), specificKey(key), tableMode(tableMode),
        lastPackageHeader(_lastPackageHeader), showWidget(showWidget) {

    isFromMainMenu = (fromMenu == "main");
    isFromPackage = (fromMenu == "package");
    isFromSelectionMenu = (fromMenu == "selection");
    triggerRumbleClick.store(true, std::memory_order_release);
    triggerSettingsSound.store(true, std::memory_order_release);
  }

  ~ScriptOverlay() {
    // reset cache (script overlay doesnt use it)
    // tsl::elm::clearFrameCache();
  }

  virtual tsl::elm::Element *createUI() override {
    inScriptMenu = true;
    std::string packageName = getNameFromPath(filePath);
    if (packageName == ".packages")
      packageName = ROOT_PACKAGE;
    else if (!packageRootLayerTitle.empty())
      packageName = packageRootLayerTitle;
    auto *list = new tsl::elm::List();

    bool noClickableItems = false;
    if (!tableMode) {
      size_t index = 0, tryCount = 0;
      std::string combinedCommand;
      // If not in table mode, loop through commands and display each command as
      // a list item
      for (auto &command : commands) {
        if (index == 0 && command[0] != "try:" && command[0] != "on:" &&
            command[0] != "off:") {
          addHeader(list, specificKey);
        }
        if (command[0] == "try:") {
          tryCount++;
          index++;
          addHeader(list, specificKey + " " + DIVIDER_SYMBOL + " " + "Try" +
                              " #" + ult::to_string(tryCount));
          continue;
        }
        if (command[0] == "on:") {
          index++;
          addHeader(list, specificKey + " " + DIVIDER_SYMBOL + " " + ON);
          continue;
        }
        if (command[0] == "off:") {
          index++;
          addHeader(list, specificKey + " " + DIVIDER_SYMBOL + " " + OFF);
          continue;
        }
        combinedCommand = joinCommands(
            command); // Join commands into a single line for display
        addListItem(list, combinedCommand);
        index++;
        command = {};
        // command.clear();
        // command.shrink_to_fit();
      }
    } else {

      noClickableItems = true;
      std::vector<std::string> sectionLines; // Holds the sections (commands)
      std::vector<std::string> infoLines; // Holds the info (empty in this case)
      // Table mode: Collect command data for the table
      std::string sectionLine;

      std::string packageSourcePath;

      for (auto &command : commands) {
        if (command.size() > 1 && command[0] == "package_source") {
          packageSourcePath = command[1];
          preprocessPath(packageSourcePath, filePath);
        }
        // Each command will be treated as a section with no corresponding info
        sectionLine =
            joinCommands(command); // Combine command parts into a section line
        sectionLines.push_back(sectionLine); // Add to section lines
        infoLines.push_back("");             // Empty info line
        command = {};
        // command.clear();
        // command.shrink_to_fit();
      }

      // Use default parameters for the table view
      constexpr size_t tableColumnOffset = 163;
      constexpr size_t tableStartGap = 20;
      constexpr size_t tableEndGap = 9;
      constexpr size_t tableSpacing = 4;
      const std::string &tableSectionTextColor = DEFAULT_STR;
      const std::string &tableInfoTextColor = DEFAULT_STR;
      const std::string &tableAlignment = LEFT_STR;
      constexpr bool hideTableBackground = false;
      constexpr bool useHeaderIndent = false;
      constexpr bool isPolling = false;
      constexpr bool isScrollableTable = true;
      const std::string wrappingMode = "char";
      constexpr bool useWrappedTextIndent = true;

      std::vector<std::vector<std::string>> dummyTableData;

      // addDummyListItem(list);
      addHeader(list, specificKey);

      addDummyListItem(list);
      // Draw the table using the sectionLines and empty infoLines
      drawTable(list, dummyTableData, sectionLines, infoLines,
                tableColumnOffset, tableStartGap, tableEndGap, tableSpacing,
                tableSectionTextColor, tableInfoTextColor, tableInfoTextColor,
                tableAlignment, hideTableBackground, useHeaderIndent, isPolling,
                isScrollableTable, wrappingMode, useWrappedTextIndent);

      if (!packageSourcePath.empty()) {

        std::vector<std::string> sourceCommands =
            readListFromFile(packageSourcePath, 0, true);
        sectionLines.clear();
        // sectionLines.shrink_to_fit();
        infoLines.clear();
        // infoLines.shrink_to_fit();
        sectionLine = "";
        for (auto &command : sourceCommands) {
          // Each command will be treated as a section with no corresponding
          // info
          // sectionLine = joinCommands(command);  // Combine command parts into
          // a section line
          sectionLines.push_back(std::move(command)); // Add to section lines
          command.shrink_to_fit();
          infoLines.push_back(""); // Empty info line
        }
        // sourceCommands.clear();
        sourceCommands.shrink_to_fit();

        const std::string packageSourceName =
            getNameFromPath(packageSourcePath);

        addHeader(list, packageSourceName);
        drawTable(list, dummyTableData, sectionLines, infoLines,
                  tableColumnOffset, tableStartGap, tableEndGap, tableSpacing,
                  tableSectionTextColor, tableInfoTextColor, tableInfoTextColor,
                  tableAlignment, hideTableBackground, useHeaderIndent,
                  isPolling, isScrollableTable, wrappingMode,
                  useWrappedTextIndent);
      }
      // addDummyListItem(list);
    }

    const std::string packageVersion =
        isFromMainMenu ? "" : packageRootLayerVersion;

    auto *rootFrame = new tsl::elm::OverlayFrame(
        packageName,
        !lastPackageHeader.empty()
            ? lastPackageHeader + "?Ultrahand Script"
            : (packageVersion.empty()
                   ? CAPITAL_ULTRAHAND_PROJECT_NAME + " Script"
                   : packageVersion + " " + DIVIDER_SYMBOL + " " +
                         CAPITAL_ULTRAHAND_PROJECT_NAME + " Script"),
        noClickableItems);
    // list->disableCaching(!isFromSelectionMenu);
    rootFrame->setContent(list);
    if (showWidget)
      rootFrame->m_showWidget = true;
    return rootFrame;
  }

  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {

    const bool isRunningInterp = runningInterpreter.load(acquire);

    if (isRunningInterp)
      return handleRunningInterpreter(keysDown, keysHeld);

    if (lastRunningInterpreter.exchange(false, std::memory_order_acq_rel)) {
      isDownloadCommand.store(false, release);
      if (lastSelectedListItem) {
        lastSelectedListItem->setValue(
            commandSuccess.load(acquire) ? CHECKMARK_SYMBOL : CROSSMARK_SYMBOL);
        lastSelectedListItem->enableClickAnimation();
        lastSelectedListItem = nullptr;
      }
      closeInterpreterThread();

      if (!commandSuccess.load()) {
        triggerRumbleDoubleClick.store(true, std::memory_order_release);
      }
      if (!limitedMemory && useSoundEffects) {
        reloadSoundCacheNow.store(true, std::memory_order_release);
        // ult::Audio::initialize();
      }
      // lastRunningInterpreter.store(false, std::memory_order_release);
      return true;
    }

    if (goBackAfter.exchange(false, std::memory_order_acq_rel)) {
      disableSound.store(true, std::memory_order_release);
      simulatedBack.store(true, std::memory_order_release);
      return true;
    }

    if (inScriptMenu) {
      // if (simulatedNextPage.load(acquire))
      //     simulatedNextPage.store(false, release);
      // if (simulatedMenu.load(acquire))
      //     simulatedMenu.store(false, release);
      simulatedNextPage.exchange(false, std::memory_order_acq_rel);
      simulatedMenu.exchange(false, std::memory_order_acq_rel);

      const bool isTouching = stillTouching.load(acquire);
      const bool backKeyPressed =
          !isTouching &&
          (((keysDown & KEY_B) && !(keysHeld & ~KEY_B & ALL_KEYS_MASK)));

      if (backKeyPressed) {
        if (allowSlide.load(acquire))
          allowSlide.store(false, release);
        if (unlockedSlide.load(acquire))
          unlockedSlide.store(false, release);
        inScriptMenu = false;

        // Handle return destination logic
        if (isFromPackage) {
          returningToPackage = lastMenu == "packageMenu";
          returningToSubPackage = lastMenu == "subPackageMenu";
        } else if (isFromSelectionMenu) {
          returningToSelectionMenu = isFromSelectionMenu;
        } else if (isFromMainMenu) {
          returningToMain = isFromMainMenu;
        }

        tsl::goBack();
        return true;
      }
    }

    if (triggerExit.exchange(false, std::memory_order_acq_rel)) {
      // triggerExit.store(false, release);
      ult::launchingOverlay.store(true, std::memory_order_release);
      tsl::setNextOverlay(OVERLAY_PATH + "ovlmenu.ovl");
      tsl::Overlay::get()->close();
    }

    return false;
  }

private:
  std::string joinCommands(const std::vector<std::string> &commandParts) {
    std::string combinedCommand;

    // Check if this is a section header (e.g., "[*Boot Entry]" or "[hekate]")
    if (!commandParts.empty() && commandParts.front().front() == '[' &&
        commandParts.back().back() == ']') {
      for (const auto &part : commandParts) {
        combinedCommand += part + " "; // Simply join the parts with spaces
      }
      return combinedCommand.substr(0, combinedCommand.size() -
                                           1); // Return joined section header
    }

    // Regular command processing
    std::string argument;
    for (const auto &part : commandParts) {
      argument = part;

      // If the argument is exactly '', skip processing (preserve the empty
      // quotes)
      if (argument == "") {
        argument = "''";
        // continue;  // Do nothing, preserve as is
      }

      // If the argument already has quotes, skip it
      if ((argument.front() == '"' && argument.back() == '"') ||
          (argument.front() == '\'' && argument.back() == '\'')) {
        combinedCommand += argument + " ";
        continue; // Already quoted, skip
      }

      // If the argument contains no spaces, do not add quotes
      if (argument.find(' ') == std::string::npos) {
        combinedCommand += argument + " ";
        continue; // No spaces, so no need for quotes
      }

      // If the argument contains a single quote, wrap it in double quotes
      if (argument.find('\'') != std::string::npos) {
        combinedCommand += "\"" + argument + "\" ";
      }
      // If the argument contains a double quote, wrap it in single quotes
      else if (argument.find('"') != std::string::npos) {
        combinedCommand += "\'" + argument + "\' ";
      }
      // If the argument contains spaces but no quotes, wrap it in single quotes
      // by default
      else {
        combinedCommand += "\'" + argument + "\' ";
      }
    }

    return combinedCommand.substr(0, combinedCommand.size() -
                                         1); // Remove trailing space
  }
};

// Set to globals for reduced lambda overhead

/**
 * @brief The `SelectionOverlay` class manages the selection overlay
 * functionality.
 *
 * This class handles the selection overlay, allowing users to interact with and
 * select various options. It provides functions for creating the graphical user
 * interface (GUI), handling user input, and executing commands.
 */
class SelectionOverlay : public tsl::Gui {
private:
  std::string filePath, specificKey, pathPattern, pathPatternOn, pathPatternOff,
      groupingName, lastGroupingName;
  std::string specifiedFooterKey;
  bool toggleState = false;
  std::string packageConfigIniPath;
  std::string commandSystem, commandMode, commandGrouping;

  std::string lastPackageHeader;

  // Variables moved from createUI to class scope
  std::vector<std::string> filesList, filesListOn, filesListOff;
  std::vector<std::string> filterList, filterListOn, filterListOff;
  std::string sourceType, sourceTypeOn, sourceTypeOff;
  std::string jsonPath, jsonPathOn, jsonPathOff;
  std::string jsonKey, jsonKeyOn, jsonKeyOff;
  std::string listPath, listPathOn, listPathOff;
  std::string iniPath, iniPathOn, iniPathOff;
  std::string listString, listStringOn, listStringOff;
  std::string jsonString, jsonStringOn, jsonStringOff;
  std::vector<std::string> selectedItemsList;
  std::string hexPath;

  std::vector<std::vector<std::string>> selectionCommands = {};
  std::vector<std::vector<std::string>> selectionCommandsOn = {};
  std::vector<std::vector<std::string>> selectionCommandsOff = {};
  std::string lastSelectedListItemFooter2 = "";

  std::unordered_map<int, int> toggleCount;
  std::unordered_map<int, bool> currentPatternIsOriginal;
  // For handling on/off file_source toggle states
  std::unordered_map<int, std::string> currentSelectedItems;
  std::unordered_map<int, bool> isInitialized;

  bool showWidget = false;

  bool usingProgress = false;
  bool isHold = false;
  bool isMini = false;

  size_t maxItemsLimit = 250; // 0 = uncapped, any other value = max size

  // Helper function to apply size limit to any vector
  void applyItemsLimit(std::vector<std::string> &vec) {
    if (maxItemsLimit == 0 || vec.size() <= maxItemsLimit)
      return;
    vec.resize(maxItemsLimit);
    vec.shrink_to_fit();
  }

public:
  SelectionOverlay(const std::string &path, const std::string &key,
                   const std::string &footerKey,
                   const std::string &_lastPackageHeader,
                   const std::vector<std::vector<std::string>> &commands,
                   bool showWidget = false)
      : filePath(path), specificKey(key), specifiedFooterKey(footerKey),
        lastPackageHeader(_lastPackageHeader), selectionCommands(commands),
        showWidget(showWidget) {
    std::lock_guard<std::mutex> lock(transitionMutex);
    // lastSelectedListItemFooter2 = "";
    lastSelectedListItem = nullptr;
    // selectedFooterDict.clear();
    tsl::clearGlyphCacheNow.store(true, release);
  }

  ~SelectionOverlay() {
    std::lock_guard<std::mutex> lock(transitionMutex);
    // tsl::elm::clearFrameCache();
    // 0lastSelectedListItemFooter2 = "";
    lastSelectedListItem = nullptr;
    // selectedFooterDict.clear();
    tsl::clearGlyphCacheNow.store(true, release);
  }

  void processSelectionCommands() {
    if (ult::expandedMemory)
      maxItemsLimit = 0; // uncapped for loader+

    removeEmptyCommands(selectionCommands);

    bool inEristaSection = false;
    bool inMarikoSection = false;
    std::string currentSection = GLOBAL_STR;

    // Track all source file paths for placeholder replacement
    std::string _iniFilePath;
    std::string _hexFilePath;
    std::string _listString;
    std::string _listFilePath;
    std::string _jsonString;
    std::string _jsonFilePath;

    // Use string_view for read-only operations to avoid copying
    // std::string commandName;
    std::string filterEntry;
    std::vector<std::string> matchedFiles, tempFiles;

    // Pre-cache pattern lengths for better performance
    // static const size_t SYSTEM_PATTERN_LEN = SYSTEM_PATTERN.length();
    // static const size_t MODE_PATTERN_LEN = MODE_PATTERN.length();
    // static const size_t GROUPING_PATTERN_LEN = GROUPING_PATTERN.length();
    // static const size_t SELECTION_MINI_PATTERN_LEN =
    // SELECTION_MINI_PATTERN.length(); static const size_t HOLD_PATTERN_LEN =
    // HOLD_PATTERN.length(); static const size_t PROGRESS_PATTERN_LEN =
    // PROGRESS_PATTERN.length();

    bool afterSource = false;
    // updateGeneralPlaceholders();

    for (auto &cmd : selectionCommands) {

      // if (afterSource) {
      //     // Apply placeholder replacements in-place
      //     for (auto& arg : cmd) {
      //         replacePlaceholdersInArg(arg, generalPlaceholders);
      //     }
      //     static bool runOnce = true;
      //     if (runOnce) {
      //         _iniFilePath = "";
      //         _hexFilePath = "";
      //         _listString = "";
      //         _listFilePath = "";
      //         _jsonString = "";
      //         _jsonFilePath = "";
      //     }
      // }
      //  Apply placeholder replacements in-place
      for (auto &arg : cmd) {
        replacePlaceholdersInArg(arg, generalPlaceholders);
      }

      const std::string &commandName =
          cmd[0]; // Now assigns to string_view - no copy

      // Keep original case-insensitive logic
      if (stringToLowercase(commandName) == "erista:") {
        inEristaSection = true;
        inMarikoSection = false;
        continue;
      } else if (stringToLowercase(commandName) == "mariko:") {
        inEristaSection = false;
        inMarikoSection = true;
        continue;
      }

      if ((inEristaSection && !inMarikoSection && usingErista) ||
          (!inEristaSection && inMarikoSection && usingMariko) ||
          (!inEristaSection && !inMarikoSection)) {

        // Optimized pattern matching with bounds checking
        if (commandName.size() > SYSTEM_PATTERN_LEN &&
            commandName.compare(0, SYSTEM_PATTERN_LEN, SYSTEM_PATTERN) == 0) {
          commandSystem.assign(commandName, SYSTEM_PATTERN_LEN,
                               commandName.size() - SYSTEM_PATTERN_LEN);
          if (std::find(commandSystems.begin(), commandSystems.end(),
                        commandSystem) == commandSystems.end())
            commandSystem = commandSystems[0];
        } else if (commandName.size() > MODE_PATTERN_LEN &&
                   commandName.compare(0, MODE_PATTERN_LEN, MODE_PATTERN) ==
                       0) {
          commandMode = commandName.substr(MODE_PATTERN_LEN);
          if (std::find(commandModes.begin(), commandModes.end(),
                        commandMode) == commandModes.end())
            commandMode = commandModes[0];
        } else if (commandName.size() > GROUPING_PATTERN_LEN &&
                   commandName.compare(0, GROUPING_PATTERN_LEN,
                                       GROUPING_PATTERN) == 0) {
          commandGrouping = commandName.substr(GROUPING_PATTERN_LEN);
          if (std::find(commandGroupings.begin(), commandGroupings.end(),
                        commandGrouping) == commandGroupings.end())
            commandGrouping = commandGroupings[0];
        } else if (commandName.size() > SELECTION_MINI_PATTERN_LEN &&
                   commandName.compare(0, SELECTION_MINI_PATTERN_LEN,
                                       SELECTION_MINI_PATTERN) == 0) {
          isMini = (commandName.substr(SELECTION_MINI_PATTERN_LEN) == TRUE_STR);
        } else if (commandName.size() > HOLD_PATTERN_LEN &&
                   commandName.compare(0, HOLD_PATTERN_LEN, HOLD_PATTERN) ==
                       0) {
          isHold = (commandName.substr(HOLD_PATTERN_LEN) == TRUE_STR);
        } else if (commandName.size() > PROGRESS_PATTERN_LEN &&
                   commandName.compare(0, PROGRESS_PATTERN_LEN,
                                       PROGRESS_PATTERN) == 0) {
          usingProgress =
              (commandName.substr(PROGRESS_PATTERN_LEN) == TRUE_STR);
        }

        if (commandMode == TOGGLE_STR) {
          if (commandName == "on:")
            currentSection = ON_STR;
          else if (commandName == "off:")
            currentSection = OFF_STR;
        }

        if (cmd.size() > 1) {
          // Apply ALL placeholder replacements using the comprehensive function
          // This handles nesting, recursion, and proper order automatically
          if (!afterSource)
            applyPlaceholderReplacements(cmd, _hexFilePath, _iniFilePath,
                                         _listString, _listFilePath,
                                         _jsonString, _jsonFilePath);

          // Now handle source declarations
          if (commandName == "ini_file") {
            _iniFilePath = cmd[1];
            preprocessPath(_iniFilePath, filePath);
            continue;
          } else if (commandName == "hex_file") {
            _hexFilePath = cmd[1];
            preprocessPath(_hexFilePath, filePath);
            continue;
          } else if (commandName == "list") {
            _listString = cmd[1];
            removeQuotes(_listString);
            continue;
          } else if (commandName == "list_file") {
            _listFilePath = cmd[1];
            preprocessPath(_listFilePath, filePath);
            continue;
          } else if (commandName == "json") {
            _jsonString = cmd[1];
            removeQuotes(_jsonString);
            continue;
          } else if (commandName == "json_file") {
            _jsonFilePath = cmd[1];
            preprocessPath(_jsonFilePath, filePath);
            continue;
          } else if (commandName == "filter") {
            // Avoid copying by directly assigning and then processing
            filterEntry = std::move(cmd[1]); // Move instead of copy
            // cmd[1].shrink_to_fit();
            removeQuotes(filterEntry);
            if (sourceType == FILE_STR) {
              preprocessPath(filterEntry, filePath);
            }

            if (filterEntry.find('*') != std::string::npos) {
              // Get files directly into temporary, avoid intermediate storage
              tempFiles.clear();
              tempFiles = getFilesListByWildcards(filterEntry, maxItemsLimit);
              if (currentSection == GLOBAL_STR) {
                filterList.insert(filterList.end(),
                                  std::make_move_iterator(tempFiles.begin()),
                                  std::make_move_iterator(tempFiles.end()));
              } else if (currentSection == ON_STR) {
                filterListOn.insert(filterListOn.end(),
                                    std::make_move_iterator(tempFiles.begin()),
                                    std::make_move_iterator(tempFiles.end()));
              } else if (currentSection == OFF_STR) {
                filterListOff.insert(filterListOff.end(),
                                     std::make_move_iterator(tempFiles.begin()),
                                     std::make_move_iterator(tempFiles.end()));
              }
              // tempFiles.clear(); // automatically cleared since it is moved
              // out of list.
            } else {
              if (currentSection == GLOBAL_STR)
                filterList.push_back(std::move(filterEntry));
              else if (currentSection == ON_STR)
                filterListOn.push_back(std::move(filterEntry));
              else if (currentSection == OFF_STR)
                filterListOff.push_back(std::move(filterEntry));
              // filterEntry.shrink_to_fit();
            }
            filterEntry.shrink_to_fit();
          } else if (commandName == "file_source") {
            sourceType = FILE_STR;
            if (currentSection == GLOBAL_STR) {
              pathPattern = cmd[1];
              preprocessPath(pathPattern, filePath);
              updateGeneralPlaceholders();
              replacePlaceholdersInArg(pathPattern, generalPlaceholders);
              // Get files directly, avoid storing in intermediate variable
              tempFiles.clear();
              tempFiles = getFilesListByWildcards(pathPattern, maxItemsLimit);
              filesList.insert(filesList.end(),
                               std::make_move_iterator(tempFiles.begin()),
                               std::make_move_iterator(tempFiles.end()));
              // tempFiles.clear();
            } else if (currentSection == ON_STR) {
              pathPatternOn = cmd[1];
              preprocessPath(pathPatternOn, filePath);
              tempFiles.clear();
              tempFiles = getFilesListByWildcards(pathPatternOn, maxItemsLimit);
              filesListOn.insert(filesListOn.end(),
                                 std::make_move_iterator(tempFiles.begin()),
                                 std::make_move_iterator(tempFiles.end()));
              // tempFiles.clear();
              sourceTypeOn = FILE_STR;
            } else if (currentSection == OFF_STR) {
              pathPatternOff = cmd[1];
              preprocessPath(pathPatternOff, filePath);
              tempFiles.clear();
              tempFiles =
                  getFilesListByWildcards(pathPatternOff, maxItemsLimit);
              filesListOff.insert(filesListOff.end(),
                                  std::make_move_iterator(tempFiles.begin()),
                                  std::make_move_iterator(tempFiles.end()));
              // tempFiles.clear();
              sourceTypeOff = FILE_STR;
            }
            afterSource = true;
          } else if (commandName == "json_file_source") {
            sourceType = JSON_FILE_STR;
            if (currentSection == GLOBAL_STR) {
              jsonPath = cmd[1];
              preprocessPath(jsonPath, filePath);
              if (cmd.size() > 2)
                jsonKey = cmd[2];
            } else if (currentSection == ON_STR) {
              jsonPathOn = cmd[1];
              preprocessPath(jsonPathOn, filePath);
              sourceTypeOn = JSON_FILE_STR;
              if (cmd.size() > 2)
                jsonKeyOn = cmd[2];
            } else if (currentSection == OFF_STR) {
              jsonPathOff = cmd[1];
              preprocessPath(jsonPathOff, filePath);
              sourceTypeOff = JSON_FILE_STR;
              if (cmd.size() > 2)
                jsonKeyOff = cmd[2];
            }
            afterSource = true;
          } else if (commandName == "list_file_source") {
            sourceType = LIST_FILE_STR;
            if (currentSection == GLOBAL_STR) {
              listPath = cmd[1];
              preprocessPath(listPath, filePath);
            } else if (currentSection == ON_STR) {
              listPathOn = cmd[1];
              preprocessPath(listPathOn, filePath);
              sourceTypeOn = LIST_FILE_STR;
            } else if (currentSection == OFF_STR) {
              listPathOff = cmd[1];
              preprocessPath(listPathOff, filePath);
              sourceTypeOff = LIST_FILE_STR;
            }
            afterSource = true;
          } else if (commandName == "list_source") {
            sourceType = LIST_STR;
            if (currentSection == GLOBAL_STR) {
              listString = cmd[1];
              removeQuotes(listString);
            } else if (currentSection == ON_STR) {
              listStringOn = cmd[1];
              removeQuotes(listStringOn);
              sourceTypeOn = LIST_STR;
            } else if (currentSection == OFF_STR) {
              listStringOff = cmd[1];
              removeQuotes(listStringOff);
              sourceTypeOff = LIST_STR;
            }
            afterSource = true;
          } else if (commandName == "ini_file_source") {
            sourceType = INI_FILE_STR;
            if (currentSection == GLOBAL_STR) {
              iniPath = cmd[1];
              preprocessPath(iniPath, filePath);
            } else if (currentSection == ON_STR) {
              iniPathOn = cmd[1];
              preprocessPath(iniPathOn, filePath);
              sourceTypeOn = INI_FILE_STR;
            } else if (currentSection == OFF_STR) {
              iniPathOff = cmd[1];
              preprocessPath(iniPathOff, filePath);
              sourceTypeOff = INI_FILE_STR;
            }
            afterSource = true;
          } else if (commandName == "json_source") {
            sourceType = JSON_STR;
            if (currentSection == GLOBAL_STR) {
              jsonString = cmd[1];
              removeQuotes(jsonString);
              if (cmd.size() > 2) {
                jsonKey = cmd[2];
                removeQuotes(jsonKey);
              }
            } else if (currentSection == ON_STR) {
              jsonStringOn = cmd[1];
              removeQuotes(jsonStringOn);
              sourceTypeOn = JSON_STR;
              if (cmd.size() > 2) {
                jsonKeyOn = cmd[2];
                removeQuotes(jsonKeyOn);
              }
            } else if (currentSection == OFF_STR) {
              jsonStringOff = cmd[1];
              removeQuotes(jsonStringOff);
              sourceTypeOff = JSON_STR;
              if (cmd.size() > 2) {
                jsonKeyOff = cmd[2];
                removeQuotes(jsonKeyOff);
              }
            }
            afterSource = true;
          }
        }

        if (commandMode == TOGGLE_STR) {
          if (currentSection == GLOBAL_STR) {
            selectionCommandsOn.push_back(cmd);
            selectionCommandsOff.push_back(std::move(cmd));
            cmd.shrink_to_fit();
          } else if (currentSection == ON_STR) {
            selectionCommandsOn.push_back(std::move(cmd));
            cmd.shrink_to_fit();
          } else if (currentSection == OFF_STR) {
            selectionCommandsOff.push_back(std::move(cmd));
            cmd.shrink_to_fit();
          }
        }
        // if (commandMode == TOGGLE_STR)
        //     cmd.clear();
      }
    }
  }

  virtual tsl::elm::Element *createUI() override {
    std::lock_guard<std::mutex> lock(transitionMutex);
    inSelectionMenu = true;

    auto *list = new tsl::elm::List();
    packageConfigIniPath = filePath + CONFIG_FILENAME;

    commandSystem = commandSystems[0];
    commandMode = commandModes[0];
    commandGrouping = commandGroupings[0];

    processSelectionCommands();

    std::vector<std::string> selectedItemsListOn, selectedItemsListOff;
    std::string currentPackageHeader;

    if (commandMode == DEFAULT_STR || commandMode == OPTION_STR) {
      if (sourceType == FILE_STR) {
        selectedItemsList = std::move(filesList);
        filesList.shrink_to_fit();
      } else if (sourceType == LIST_STR || sourceType == LIST_FILE_STR) {
        selectedItemsList = (sourceType == LIST_STR)
                                ? stringToList(listString)
                                : readListFromFile(listPath, maxItemsLimit);
        listString = "";
        listPath = "";
        // listString.clear();
        // listPath.clear();
      } else if (sourceType == INI_FILE_STR) {
        selectedItemsList = parseSectionsFromIni(iniPath);
        iniPath.clear();
      } else if (sourceType == JSON_STR || sourceType == JSON_FILE_STR) {
        populateSelectedItemsListFromJson(
            sourceType, (sourceType == JSON_STR) ? jsonString : jsonPath,
            jsonKey, selectedItemsList);
        jsonPath = "";
        jsonString = "";

        // jsonPath.clear();
        // jsonPath.shrink_to_fit();
        // jsonString.clear();
        // jsonString.shrink_to_fit();
      }
      applyItemsLimit(selectedItemsList);

    } else if (commandMode == TOGGLE_STR) {
      if (sourceTypeOn == FILE_STR) {
        selectedItemsListOn = std::move(filesListOn);
        filesListOn.shrink_to_fit();
      } else if (sourceTypeOn == LIST_STR || sourceTypeOn == LIST_FILE_STR) {
        selectedItemsListOn = (sourceTypeOn == LIST_STR)
                                  ? stringToList(listStringOn)
                                  : readListFromFile(listPathOn, maxItemsLimit);
        listStringOn = "";
        listPathOn = "";
        // listStringOn.clear();
        // listPathOn.clear();
      } else if (sourceTypeOn == INI_FILE_STR) {
        selectedItemsListOn = parseSectionsFromIni(iniPathOn);
        iniPathOn = "";
        // iniPathOn.clear();
      } else if (sourceTypeOn == JSON_STR || sourceTypeOn == JSON_FILE_STR) {
        populateSelectedItemsListFromJson(
            sourceTypeOn,
            (sourceTypeOn == JSON_STR) ? jsonStringOn : jsonPathOn, jsonKeyOn,
            selectedItemsListOn);
        jsonPathOff = "";
        jsonStringOff = "";

        // jsonPathOn.clear();
        // jsonPathOn.shrink_to_fit();
        // jsonStringOn.clear();
        // jsonStringOn.shrink_to_fit();
      }
      applyItemsLimit(selectedItemsListOn);

      if (sourceTypeOff == FILE_STR) {
        selectedItemsListOff = std::move(filesListOff);
        filesListOff.shrink_to_fit();
      } else if (sourceTypeOff == LIST_STR || sourceTypeOff == LIST_FILE_STR) {
        selectedItemsListOff =
            (sourceTypeOff == LIST_STR)
                ? stringToList(listStringOff)
                : readListFromFile(listPathOff, maxItemsLimit);
        listStringOff = "";
        listPathOff = "";
        // listStringOff.clear();
        // listPathOff.clear();
      } else if (sourceTypeOff == INI_FILE_STR) {
        selectedItemsListOff = parseSectionsFromIni(iniPathOff);
        iniPathOff = "";
        // iniPathOff.clear();
      } else if (sourceTypeOff == JSON_STR || sourceTypeOff == JSON_FILE_STR) {
        populateSelectedItemsListFromJson(
            sourceTypeOff,
            (sourceTypeOff == JSON_STR) ? jsonStringOff : jsonPathOff,
            jsonKeyOff, selectedItemsListOff);
        jsonPathOff = "";
        jsonStringOff = "";

        // jsonPathOff.clear();
        // jsonStringOff.shrink_to_fit();
        // jsonStringOff.clear();
        // jsonStringOff.shrink_to_fit();
      } //
      applyItemsLimit(selectedItemsListOff);

      // selectedItemsList.reserve(selectedItemsListOn.size() +
      // selectedItemsListOff.size());
      selectedItemsList.insert(selectedItemsList.end(),
                               selectedItemsListOn.begin(),
                               selectedItemsListOn.end());
      selectedItemsList.insert(selectedItemsList.end(),
                               selectedItemsListOff.begin(),
                               selectedItemsListOff.end());
    }

    if (sourceType == FILE_STR) {
      if (commandGrouping == "split2" || commandGrouping == "split4") {
        std::sort(selectedItemsList.begin(), selectedItemsList.end(),
                  [](const std::string &a, const std::string &b) {
                    const std::string &parentDirA = getParentDirNameFromPath(a);
                    const std::string &parentDirB = getParentDirNameFromPath(b);
                    return (parentDirA != parentDirB)
                               ? (parentDirA < parentDirB)
                               : (getNameFromPath(a) < getNameFromPath(b));
                  });
      } else if (commandGrouping == "split5") {
        std::sort(selectedItemsList.begin(), selectedItemsList.end(),
                  [](const std::string &a, const std::string &b) {
                    std::string ga = getParentDirNameFromPath(a);
                    std::string gb = getParentDirNameFromPath(b);
                    removeQuotes(ga);
                    removeQuotes(gb);

                    const size_t posA = ga.find(" - ");
                    const size_t posB = gb.find(" - ");

                    if (posA != posB) {
                      if (posA == std::string::npos)
                        return true;
                      if (posB == std::string::npos)
                        return false;
                    }

                    const int cmp = ga.compare(0, posA, gb, 0, posB);
                    if (cmp != 0)
                      return cmp < 0;

                    if (posA == std::string::npos)
                      return false;
                    return ga.compare(posA + 3, std::string::npos, gb, posB + 3,
                                      std::string::npos) < 0;
                  });
      } else {
        std::sort(selectedItemsList.begin(), selectedItemsList.end(),
                  [](const std::string &a, const std::string &b) {
                    return getNameFromPath(a) < getNameFromPath(b);
                  });
      }
    }

    if (commandGrouping == DEFAULT_STR) {
      std::string cleanSpecificKey = specificKey.substr(1);
      removeTag(cleanSpecificKey);
      addHeader(list, cleanSpecificKey);
      currentPackageHeader = cleanSpecificKey;
    }

    // tsl::elm::ListItem* listItem;
    size_t pos;
    std::string parentDirName;
    std::string footer;
    // std::string optionName;

    if (selectedItemsList.empty()) {
      if (commandGrouping != DEFAULT_STR) {
        std::string cleanSpecificKey = specificKey.substr(1);
        removeTag(cleanSpecificKey);
        addHeader(list, cleanSpecificKey);
        currentPackageHeader = cleanSpecificKey;
      }
      // tsl::elm::ListItem* listItem = new tsl::elm::ListItem(EMPTY);
      // list->addItem(listItem);

      // addDummyListItem(list);
      // auto* warning = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer*
      // renderer, u16 x, u16 y, u16 w, u16 h){
      //     renderer->drawString("\uE150", false, 180, 274+50, 90,
      //     (tsl::defaultTextColor)); renderer->drawString(SELECTION_IS_EMPTY,
      //     false, 110, 360+50, 25, (tsl::defaultTextColor));
      // });
      // list->addItem(warning);

      addSelectionIsEmptyDrawer(list);
      noClickableItems = true;
    }

    std::string tmpSelectedItem;
    const size_t selectedItemsSize =
        selectedItemsList.size(); // Cache size to avoid repeated calls

    // Pre-process filtered items for reduced memory imprint
    for (size_t i = 0; i < selectedItemsSize; ++i) {
      std::string &selectedItem = selectedItemsList[i];

      // if (isFileOrDirectory(selectedItem)) {
      const std::string itemName = getNameFromPath(selectedItem);
      if (itemName.front() == '.') { // Skip hidden items
        selectedItem = "";
        continue;
      }
      //}

      if (commandMode == TOGGLE_STR) {
        auto it =
            std::find(filterListOn.begin(), filterListOn.end(), selectedItem);
        if (it != filterListOn.end()) {
          filterListOn.erase(it);
          selectedItem = "";
          continue;
        }
        it =
            std::find(filterListOff.begin(), filterListOff.end(), selectedItem);
        if (it != filterListOff.end()) {
          filterListOff.erase(it);
          selectedItem = "";
          continue;
        }
      } else {
        auto it = std::find(filterList.begin(), filterList.end(), selectedItem);
        if (it != filterList.end()) {
          filterList.erase(it);
          selectedItem = "";
          continue;
        }
      }
    }

    filterList = {};
    filterListOn = {};
    filterListOff = {};

    // filterList.clear();
    // filterList.shrink_to_fit();
    // filterListOn.clear();
    // filterListOn.shrink_to_fit();
    // filterListOff.clear();
    // filterListOff.shrink_to_fit();

    std::string itemName;
    for (size_t i = 0; i < selectedItemsSize; ++i) {
      std::string &selectedItem = selectedItemsList[i];

      itemName = getNameFromPath(selectedItem);
      if (itemName.empty()) // Skip empty lines
        continue;

      tmpSelectedItem = selectedItem;
      preprocessPath(tmpSelectedItem, filePath);
      if (!isDirectory(tmpSelectedItem))
        dropExtension(itemName);

      if (sourceType == FILE_STR) {
        if (commandGrouping == "split") {
          groupingName = getParentDirNameFromPath(selectedItem);
          removeQuotes(groupingName);

          if (lastGroupingName != groupingName) { // Simplified comparison
            addHeader(list, groupingName);
            currentPackageHeader = groupingName;
            lastGroupingName = groupingName;
          }
        } else if (commandGrouping == "split2") {
          groupingName = getParentDirNameFromPath(selectedItem);
          removeQuotes(groupingName);

          pos = groupingName.find(" - ");
          if (pos != std::string::npos) {
            itemName = groupingName.substr(pos + 3);
            groupingName = groupingName.substr(0, pos);
          }

          if (lastGroupingName != groupingName) { // Simplified comparison
            addHeader(list, groupingName);
            currentPackageHeader = groupingName;
            lastGroupingName = groupingName;
          }
        } else if (commandGrouping == "split3") {
          groupingName = getNameFromPath(selectedItem);
          removeQuotes(groupingName);

          pos = groupingName.find(" - ");
          if (pos != std::string::npos) {
            itemName = groupingName.substr(pos + 3);
            groupingName = groupingName.substr(0, pos);
          }

          if (lastGroupingName != groupingName) { // Simplified comparison
            addHeader(list, groupingName);
            currentPackageHeader = groupingName;
            lastGroupingName = groupingName;
          }
        } else if (commandGrouping == "split4") {
          groupingName = getParentDirNameFromPath(selectedItem, 2);
          removeQuotes(groupingName);
          itemName = getNameFromPath(selectedItem);
          dropExtension(itemName);
          removeQuotes(itemName);
          trim(itemName);
          footer = getParentDirNameFromPath(selectedItem);
          removeQuotes(footer);

          if (lastGroupingName != groupingName) { // Simplified comparison
            addHeader(list, groupingName);
            currentPackageHeader = groupingName;
            lastGroupingName = groupingName;
          }
        } else if (commandGrouping == "split5") {
          groupingName = getParentDirNameFromPath(selectedItem);
          removeQuotes(groupingName);

          pos = groupingName.find(" - ");
          if (pos != std::string::npos) {
            itemName = groupingName.substr(pos + 3);
            groupingName = groupingName.substr(0, pos);
          }

          if (lastGroupingName != groupingName) { // Simplified comparison
            addHeader(list, groupingName);
            currentPackageHeader = groupingName;
            lastGroupingName = groupingName;
          }
        }
        //} else {
        //    if (commandMode == TOGGLE_STR) {
        //        auto it = std::find(filterListOn.begin(), filterListOn.end(),
        //        itemName); if (it != filterListOn.end()) {
        //            filterListOn.erase(it);
        //            selectedItem.clear();
        //            continue;
        //        }
        //        it = std::find(filterListOff.begin(), filterListOff.end(),
        //        itemName); if (it != filterListOff.end()) {
        //            filterListOff.erase(it);
        //            selectedItem.clear();
        //            continue;
        //        }
        //    } else {
        //        auto it = std::find(filterList.begin(), filterList.end(),
        //        itemName); if (it != filterList.end()) {
        //            filterList.erase(it);
        //            selectedItem.clear();
        //            continue;
        //        }
        //    }
      }

      if (commandMode == DEFAULT_STR || commandMode == OPTION_STR) {
        if (sourceType != FILE_STR && commandGrouping != "split2" &&
            commandGrouping != "split3" && commandGrouping != "split4" &&
            commandGrouping != "split5") {
          pos = selectedItem.find(" - ");
          footer = "";
          itemName = selectedItem;
          if (pos != std::string::npos) {
            footer = selectedItem.substr(pos + 2);
            itemName = selectedItem.substr(0, pos);
          }
        } else if (commandGrouping == "split2") {
          footer = getNameFromPath(selectedItem);
          dropExtension(footer);
        }

        tsl::elm::ListItem *listItem =
            new tsl::elm::ListItem(itemName, "", isMini);

        // for handling footers that use translations / replacements
        applyLangReplacements(footer, true);
        convertComboToUnicode(footer);
        applyLangReplacements(specifiedFooterKey, true);
        convertComboToUnicode(specifiedFooterKey);

        applyLangReplacements(itemName, true);
        convertComboToUnicode(itemName);
        applyLangReplacements(selectedFooterDict[specifiedFooterKey], true);
        convertComboToUnicode(selectedFooterDict[specifiedFooterKey]);

        if (commandMode == OPTION_STR) {
          if (selectedFooterDict[specifiedFooterKey] == itemName) {
            lastSelectedListItem = listItem;
            lastSelectedListItemFooter2 = footer;
            listItem->setValue(CHECKMARK_SYMBOL);
          } else {
            if (pos != std::string::npos) {
              listItem->setValue(footer, true);
            } else {
              listItem->setValue(footer);
            }
          }
        } else {
          listItem->setValue(footer, true);
        }

        if (isHold) {
          listItem->disableClickAnimation();
          listItem->enableTouchHolding();
        }

        listItem->setClickListener([this, i, selectedItem, footer, listItem,
                                    currentPackageHeader,
                                    itemName](uint64_t keys) {
          if (runningInterpreter.load(acquire)) {
            return false;
          }

          if (((keys & KEY_A) && !(keys & ~KEY_A & ALL_KEYS_MASK))) {

            isDownloadCommand.store(false, release);
            runningInterpreter.store(true, release);

            auto modifiedCmds = getSourceReplacement(selectionCommands,
                                                     selectedItem, i, filePath);
            if (isHold) {
              lastSelectedListItemFooter = footer;
              // lastFooterHighlight = commandFooterHighlight;  // STORE THE
              // VALUE lastFooterHighlightDefined =
              // commandFooterHighlightDefined;  // STORE WHETHER IT WAS DEFINED
              listItem->setValue(INPROGRESS_SYMBOL);
              lastSelectedListItem = listItem;

              // Use touch hold start tick if available, otherwise use current
              // tick
              // if (listItem->isTouchHolding()) {
              //    holdStartTick = listItem->getTouchHoldStartTick();
              //} else {
              //    holdStartTick = armGetSystemTick();
              //}
              holdStartTick = armGetSystemTick();

              storedCommands = std::move(modifiedCmds);
              lastCommandMode = commandMode;
              lastCommandIsHold = true;
              lastKeyName = specificKey;
              return true;
            }

            executeInterpreterCommands(std::move(modifiedCmds), filePath,
                                       specificKey);
            listItem->disableClickAnimation();
            // startInterpreterThread(filePath);

            listItem->setValue(INPROGRESS_SYMBOL);

            if (commandMode == OPTION_STR) {
              selectedFooterDict[specifiedFooterKey] = listItem->getText();
              if (lastSelectedListItem && listItem &&
                  lastSelectedListItem != listItem) {

                lastSelectedListItem->setValue(lastSelectedListItemFooter2,
                                               true);
              }
              lastSelectedListItemFooter2 = footer;
            }

            lastSelectedListItem = listItem;
            // lastFooterHighlight = commandFooterHighlight;  // STORE THE VALUE
            // lastFooterHighlightDefined = commandFooterHighlightDefined;  //
            // STORE WHETHER IT WAS DEFINED
            tsl::shiftItemFocus(listItem);

            lastRunningInterpreter.store(true, std::memory_order_release);
            if (lastSelectedListItem)
              lastSelectedListItem->triggerClickAnimation();
            return true;
          }

          else if (keys & SCRIPT_KEY && !(keys & ~SCRIPT_KEY & ALL_KEYS_MASK)) {
            // inSelectionMenu = false;

            auto modifiedCmds = getSourceReplacement(selectionCommands,
                                                     selectedItem, i, filePath);
            applyPlaceholderReplacementsToCommands(modifiedCmds, filePath);
            // tsl::elm::g_cachedTop.disabled = true;
            // tsl::elm::g_cachedBottom.disabled = true;
            tsl::shiftItemFocus(listItem);
            tsl::changeTo<ScriptOverlay>(std::move(modifiedCmds), filePath,
                                         itemName, "selection", false,
                                         currentPackageHeader, showWidget);
            return true;
          }

          return false;
        });
        list->addItem(listItem);

      } else if (commandMode == TOGGLE_STR) {
        auto *toggleListItem = new tsl::elm::ToggleListItem(itemName, false, ON,
                                                            OFF, isMini, true);
        toggleListItem->enableShortHoldKey();
        toggleListItem->m_shortHoldKey = SCRIPT_KEY;

        // Use const iterators for better performance
        const bool toggleStateOn =
            std::find(selectedItemsListOn.cbegin(), selectedItemsListOn.cend(),
                      selectedItem) != selectedItemsListOn.cend();
        toggleListItem->setState(toggleStateOn);

        toggleListItem->setStateChangedListener(
            [this, i, toggleListItem, selectedItem, itemName](bool state) {
              if (runningInterpreter.load(std::memory_order_acquire)) {
                return;
              }

              tsl::Overlay::get()->getCurrentGui()->requestFocus(
                  toggleListItem, tsl::FocusDirection::None);

              if (toggleCount.find(i) == toggleCount.end())
                toggleCount[i] = 0;
              if (isInitialized.find(i) == isInitialized.end() ||
                  !isInitialized[i]) {
                currentSelectedItems[i] = selectedItem;
                isInitialized[i] = true;
                currentPatternIsOriginal[i] = true; // start in original pattern
              }

              const auto &activeCommands =
                  !state ? selectionCommandsOn : selectionCommandsOff;
              const auto &inactiveCommands =
                  !state ? selectionCommandsOff : selectionCommandsOn;

              // Optimized pattern search with early exit
              std::string oldPattern, newPattern;
              for (const auto &cmd : inactiveCommands) {
                if (cmd.size() > 1 && cmd[0] == "file_source") {
                  oldPattern = cmd[1];
                  break; // Early exit once found
                }
              }
              for (const auto &cmd : activeCommands) {
                if (cmd.size() > 1 && cmd[0] == "file_source") {
                  newPattern = cmd[1];
                  break; // Early exit once found
                }
              }

              preprocessPath(oldPattern, filePath);
              preprocessPath(newPattern, filePath);

              std::string pathToUse;

              if (toggleCount[i] % 2 == 0) {
                // Even toggle: use original selectedItemsList[i] (pattern A)
                pathToUse = selectedItem;
                currentPatternIsOriginal[i] = true;
              } else {
                // Odd toggle: resolve from previous path
                if (currentPatternIsOriginal[i]) {
                  // currentSelectedItems[i] corresponds to pattern A, resolve
                  // to pattern B
                  pathToUse = resolveWildcardFromKnownPath(
                      oldPattern, currentSelectedItems[i], newPattern);
                  currentPatternIsOriginal[i] = false;
                } else {
                  // currentSelectedItems[i] corresponds to pattern B, resolve
                  // back to pattern A
                  pathToUse = resolveWildcardFromKnownPath(
                      newPattern, currentSelectedItems[i], oldPattern);
                  currentPatternIsOriginal[i] = true;
                }
              }

              auto modifiedCmds =
                  getSourceReplacement(activeCommands, pathToUse, i, filePath);

              if (sourceType == FILE_STR) {
                // Optimized search with early exit
                for (const auto &cmd : modifiedCmds) {
                  if (cmd.size() > 1 && cmd[0] == "sourced_path") {
                    currentSelectedItems[i] = cmd[1];
                    break; // Early exit once found
                  }
                }
              }

              if (usingProgress)
                toggleListItem->setValue(INPROGRESS_SYMBOL);

              nextToggleState = !state ? CAPITAL_OFF_STR : CAPITAL_ON_STR;
              runningInterpreter.store(true, release);
              lastRunningInterpreter.store(true, release);
              lastSelectedListItem = toggleListItem;
              // interpretAndExecuteCommands(std::move(modifiedCmds), filePath,
              // specificKey);
              executeInterpreterCommands(std::move(modifiedCmds), filePath,
                                         specificKey);
              // resetPercentages();

              toggleCount[i]++;
            });

        // Set the script key listener (for SCRIPT_KEY)
        toggleListItem->setScriptKeyListener([this, toggleListItem, i,
                                              currentPackageHeader, itemName,
                                              selectedItem](bool state) {
          // Initialize currentSelectedItem for this index if it does not exist
          if (isInitialized.find(i) == isInitialized.end() ||
              !isInitialized[i]) {
            currentSelectedItems[i] = selectedItem;
            isInitialized[i] = true;
          }

          // inSelectionMenu = false;
          //  Custom logic for SCRIPT_KEY handling
          auto modifiedCmds = getSourceReplacement(
              !state ? selectionCommandsOn : selectionCommandsOff,
              currentSelectedItems[i], i, filePath);
          applyPlaceholderReplacementsToCommands(modifiedCmds, filePath);
          // tsl::elm::g_cachedTop.disabled = true;
          // tsl::elm::g_cachedBottom.disabled = true;
          tsl::shiftItemFocus(toggleListItem);
          tsl::changeTo<ScriptOverlay>(std::move(modifiedCmds), filePath,
                                       itemName, "selection", false,
                                       currentPackageHeader, showWidget);
        });

        list->addItem(toggleListItem);
      }
      selectedItem = "";
      // selectedItem.clear();
      // selectedItem.shrink_to_fit();
    }

    // clear everything
    selectedItemsList = {};
    selectedItemsListOn = {};
    selectedItemsListOff = {};

    // selectedItemsList.clear();
    // selectedItemsList.shrink_to_fit();
    // selectedItemsListOn.clear();
    // selectedItemsListOn.shrink_to_fit();
    // selectedItemsListOff.clear();
    // selectedItemsListOff.shrink_to_fit();

    if (!packageRootLayerTitle.empty())
      overrideTitle = true;
    if (!packageRootLayerVersion.empty())
      overrideVersion = true;

    PackageHeader packageHeader =
        getPackageHeaderFromIni(filePath + PACKAGE_FILENAME);
    if (!packageHeader.title.empty() && packageRootLayerTitle.empty())
      packageRootLayerTitle = packageHeader.title;
    if (!packageHeader.version.empty() && packageRootLayerVersion.empty())
      packageRootLayerVersion = packageHeader.version;
    if (!packageHeader.color.empty() && packageRootLayerColor.empty())
      packageRootLayerColor = packageHeader.color;

    if (packageHeader.title.empty() || overrideTitle)
      packageHeader.title = packageRootLayerTitle;
    if (packageHeader.version.empty() || overrideVersion)
      packageHeader.version = packageRootLayerVersion;
    if (packageHeader.color.empty())
      packageHeader.color = packageRootLayerColor;

    tsl::elm::OverlayFrame *rootFrame;

    if (filePath == PACKAGE_PATH) {
      rootFrame = new tsl::elm::OverlayFrame(CAPITAL_ULTRAHAND_PROJECT_NAME,
                                             versionLabel);
    } else {
      rootFrame = new tsl::elm::OverlayFrame(
          (!packageHeader.title.empty())
              ? packageHeader.title
              : (!packageRootLayerTitle.empty() ? packageRootLayerTitle
                                                : getNameFromPath(filePath)),
          !lastPackageHeader.empty()
              ? lastPackageHeader
              : (packageHeader.version != "" ? (!packageRootLayerVersion.empty()
                                                    ? packageRootLayerVersion
                                                    : packageHeader.version) +
                                                   "  Ultrahand Package"
                                             : "Ultrahand Package"),
          noClickableItems, "", packageHeader.color);
    }

    list->jumpToItem(jumpItemName, jumpItemValue,
                     jumpItemExactMatch.load(acquire));

    // list->disableCaching(true);
    rootFrame->setContent(list);
    if (showWidget)
      rootFrame->m_showWidget = true;

    return rootFrame;
  }

  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {

    bool isHolding = (lastCommandIsHold &&
                      runningInterpreter.load(std::memory_order_acquire));
    if (isHolding) {
      processHold(
          keysDown, keysHeld, holdStartTick, isHolding,
          [&]() {
            // Execute interpreter commands if needed
            displayPercentage.store(-1, std::memory_order_release);
            // lastCommandMode.clear();
            lastCommandIsHold = false;
            // lastKeyName.clear();
            lastSelectedListItem->setValue(INPROGRESS_SYMBOL);
            // lastSelectedListItemFooter.clear();
            // lastSelectedListItem->enableClickAnimation();
            // lastSelectedListItem->triggerClickAnimation();
            // lastSelectedListItem->disableClickAnimation();
            triggerEnterFeedback();
            executeInterpreterCommands(std::move(storedCommands), filePath,
                                       lastKeyName);
            lastRunningInterpreter.store(true, std::memory_order_release);
          },
          nullptr, true); // true = reset storedCommands
      return true;
    }

    const bool isRunningInterp = runningInterpreter.load(acquire);

    if (isRunningInterp) {
      return handleRunningInterpreter(keysDown, keysHeld);
    }

    if (lastRunningInterpreter.exchange(false, std::memory_order_acq_rel)) {
      isDownloadCommand.store(false, release);

      if (lastSelectedListItem) {
        const bool success = commandSuccess.load(acquire);

        if (nextToggleState.empty()) {
          // No toggle state, just show a check or cross
          lastSelectedListItem->setValue(success ? CHECKMARK_SYMBOL
                                                 : CROSSMARK_SYMBOL);
        } else {
          // Update displayed value
          lastSelectedListItem->setValue(
              success ? nextToggleState
                      : (nextToggleState == CAPITAL_ON_STR ? CAPITAL_OFF_STR
                                                           : CAPITAL_ON_STR));

          // Update toggle state (single simplified line)
          static_cast<tsl::elm::ToggleListItem *>(lastSelectedListItem)
              ->setState(nextToggleState == CAPITAL_ON_STR ? success
                                                           : !success);

          nextToggleState.clear();
        }

        lastSelectedListItem->enableClickAnimation();
        // lastSelectedListItem = nullptr;
      }

      closeInterpreterThread();
      resetPercentages();

      if (!commandSuccess.load()) {
        triggerRumbleDoubleClick.store(true, std::memory_order_release);
      }

      if (!limitedMemory && useSoundEffects) {
        reloadSoundCacheNow.store(true, std::memory_order_release);
        // ult::Audio::initialize();
      }
      // lastRunningInterpreter.store(false, std::memory_order_release);
      return true;
    }

    if (goBackAfter.exchange(false, std::memory_order_acq_rel)) {
      disableSound.store(true, std::memory_order_release);
      simulatedBack.store(true, std::memory_order_release);
      return true;
    }

    // Cache touching state for refresh operations (used in same logical block)
    const bool isTouching = stillTouching.load(acquire);

    if (refreshPage.load(acquire) && !isTouching) {
      // tsl::goBack();
      tsl::swapTo<SelectionOverlay>(filePath, specificKey, specifiedFooterKey,
                                    lastPackageHeader, selectionCommands,
                                    showWidget);
      refreshPage.store(false, release);
    }

    if (refreshPackage.load(acquire) && !isTouching) {
      tsl::goBack();
    }

    if (inSelectionMenu) {
      simulatedNextPage.exchange(false, std::memory_order_acq_rel);
      simulatedMenu.exchange(false, std::memory_order_acq_rel);

      // Check touching state again for the key handling (different timing
      // context)
      const bool isTouchingForKeys = stillTouching.load(acquire);
      const bool backKeyPressed =
          !isTouchingForKeys &&
          (((keysDown & KEY_B) && !(keysHeld & ~KEY_B & ALL_KEYS_MASK)));

      if (backKeyPressed) {
        allowSlide.exchange(false, std::memory_order_acq_rel);
        unlockedSlide.exchange(false, std::memory_order_acq_rel);
        inSelectionMenu = false;

        // Determine return destination
        if (filePath == PACKAGE_PATH) {
          returningToMain = true;
        } else {
          if (lastPackageMenu == "subPackageMenu") {
            returningToSubPackage = true;
          } else {
            returningToPackage = true;
          }
        }

        // Handle package config footer logic
        if (commandMode == OPTION_STR && isFile(packageConfigIniPath)) {
          const auto packageConfigData =
              getParsedDataFromIniFile(packageConfigIniPath);
          auto it = packageConfigData.find(specificKey);
          if (it != packageConfigData.end()) {
            auto &optionSection = it->second;
            auto footerIt = optionSection.find(FOOTER_STR);
            if (footerIt != optionSection.end() &&
                (footerIt->second.find(NULL_STR) == std::string::npos)) {
              if (selectedListItem)
                selectedListItem->setValue(footerIt->second);
            }
          }
        }
        // lastSelectedListItem = nullptr;
        // if (selectedListItem) {
        //     jumpItemName = selectedListItem->getText();
        //     jumpItemValue = selectedListItem->getValue();
        //     jumpItemExactMatch.store(false, std::memory_order_release);
        //     tsl::elm::s_returningJumpToItem.store(true,
        //     std::memory_order_release);
        // }

        tsl::goBack();
        return true;
      }
    }

    if (returningToSelectionMenu && !(keysDown & KEY_B)) {
      returningToSelectionMenu = false;
      inSelectionMenu = true;
    }

    if (triggerExit.exchange(false, std::memory_order_acq_rel)) {
      ult::launchingOverlay.store(true, std::memory_order_release);
      tsl::setNextOverlay(OVERLAY_PATH + "ovlmenu.ovl");
      tsl::Overlay::get()->close();
    }

    return false;
  }
};

std::vector<std::vector<std::string>> gatherPromptCommands(
    const std::string &dropdownSection,
    std::vector<std::pair<std::string, std::vector<std::vector<std::string>>>>
        &&options) {

  std::vector<std::vector<std::string>> promptCommands;

  bool inRelevantSection = false;
  bool isFirstSection = true;

  // Pre-define vectors outside loop to avoid repeated allocations
  std::vector<std::string> fillerCommand;
  std::vector<std::string> sectionCommand;
  std::vector<std::string> fullCmd;
  std::vector<std::string> splitParts;

  // Pre-allocate filler command (whitespace)
  fillerCommand.push_back("\u00A0");

  std::vector<std::vector<std::string>> commands;

  for (auto &nextOption : options) {
    const std::string sectionName = std::move(nextOption.first);
    nextOption.first.shrink_to_fit();
    commands = std::move(nextOption.second);
    nextOption.second.shrink_to_fit();

    // Check if this is the start of the relevant section
    if (sectionName == dropdownSection) {
      inRelevantSection = true;
      commands.clear();
      continue;
    }

    // Stop capturing if we encounter a new section with no commands (empty
    // section)
    if (inRelevantSection && commands.empty()) {
      break;
    }

    // Gather commands if we are in the relevant section
    if (inRelevantSection) {
      // Add section header as a command (with brackets)
      if (!sectionName.empty()) {
        if (!isFirstSection) {
          promptCommands.push_back(fillerCommand); // Add whitespace separator
        } else {
          isFirstSection = false;
        }

        // Clear and prepare section command
        sectionCommand.clear();
        // sectionCommand.shrink_to_fit();
        sectionCommand.push_back("[" + sectionName + "]");
        promptCommands.push_back(sectionCommand);
      }

      // Process each command by splitting on spaces
      for (auto &cmd : commands) {
        fullCmd.clear();
        // fullCmd.shrink_to_fit();

        for (auto &part : cmd) {
          splitParts = splitString(part, " ");
          fullCmd.insert(fullCmd.end(), splitParts.begin(), splitParts.end());
          part.clear();
        }

        if (!fullCmd.empty()) {
          promptCommands.push_back(fullCmd);
        }
        cmd.clear();
      }
    }
    commands.clear();
  }
  // commands.clear();

  // Return placeholder if no commands are found
  if (promptCommands.empty()) {
    promptCommands.push_back({UNAVAILABLE_SELECTION});
  }

  return promptCommands;
}

// auto clearAndDelete = [](auto& vec) {
//     for (auto* p : vec) delete p;
//     vec.clear();
//     vec.shrink_to_fit();
// };

// For returning with menu redrawing
struct ReturnContext {
  std::string packagePath;
  std::string sectionName;
  std::string currentPage;
  std::string packageName;
  std::string pageHeader;
  std::string option;
  size_t nestedLayer = 0;

  void clear() {
    packagePath.clear();
    sectionName.clear();
    currentPage.clear();
    packageName.clear();
    pageHeader.clear();
    option.clear();
    nestedLayer = 0;
  }
};

// static std::vector<std::vector<std::string>> storedCommands;

struct CommandSettings {
  bool isMini = false;
  bool isSlot = false;
};

static CommandSettings
parseCommandSettings(std::vector<std::vector<std::string>> &commands) {
  CommandSettings settings;

  if (commands.empty() || commands.size() > 2) {
    return settings;
  }

  bool foundMini = false;
  bool foundMode = false;
  bool modeIsSlot = false;
  bool miniValue = false;

  for (const auto &command : commands) {
    // if (!command.empty()) {
    {
      const std::string &commandName = command[0];
      const size_t cmdLen = commandName.length();

      if (cmdLen > 1 && commandName[0] == ';' && commandName[1] == 'm') {
        // Check for ;mini=
        if (!foundMini && cmdLen >= MINI_PATTERN_LEN + TRUE_STR_LEN &&
            commandName[2] == 'i') {
          if (commandName.compare(0, MINI_PATTERN_LEN, MINI_PATTERN) == 0) {
            switch (commandName[MINI_PATTERN_LEN]) {
            case 't':
              if (commandName.compare(MINI_PATTERN_LEN, TRUE_STR_LEN,
                                      TRUE_STR) == 0) {
                foundMini = true;
                miniValue = true;
              }
              break;
            case 'f':
              if (commandName.compare(MINI_PATTERN_LEN, FALSE_STR_LEN,
                                      FALSE_STR) == 0) {
                foundMini = true;
                miniValue = false;
              }
              break;
            }
          }
        }

        // Check for ;mode=
        if (!foundMode && cmdLen >= MODE_PATTERN_LEN + SLOT_STR.length() &&
            commandName[2] == 'o') {
          if (commandName.compare(0, MODE_PATTERN_LEN, MODE_PATTERN) == 0) {
            foundMode = true;
            if (commandName[MODE_PATTERN_LEN] == 's') {
              modeIsSlot =
                  (commandName.compare(MODE_PATTERN_LEN, SLOT_STR.length(),
                                       SLOT_STR) == 0);
            }
          }
        }

        if (foundMini && foundMode) {
          break;
        }
      }
    }
  }

  // Apply the settings
  if (foundMini) {
    settings.isMini = miniValue;
  } else if (modeIsSlot) {
    settings.isSlot = true;
  }

  // Determine if commands should be cleared
  const bool shouldClearCommands =
      (commands.size() == 1 && (foundMini || foundMode)) ||
      (commands.size() == 2 && foundMini && foundMode);

  if (shouldClearCommands) {
    commands.clear();
  }

  return settings;
}

// ReturnContext returnTo;
static std::stack<ReturnContext> returnContextStack;

class PackageMenu; // forwarding

// returns if there are or are not cickable items.
bool drawCommandsMenu(
    tsl::elm::List *list, const std::string &packageIniPath,
    const std::string &packageConfigIniPath, const PackageHeader &packageHeader,
    const std::string &pageHeader, std::string &pageLeftName,
    std::string &pageRightName, const std::string &packagePath,
    const std::string &currentPage, const std::string &packageName,
    const std::string &dropdownSection, const size_t nestedLayer,
    std::string &pathPattern, std::string &pathPatternOn,
    std::string &pathPatternOff, bool &usingPages, const bool packageMenuMode,
    const bool showWidget = false) {

  tsl::hlp::ini::IniData packageConfigData;
  // tsl::elm::ListItem* listItem;
  // auto toggleListItem = new tsl::elm::ToggleListItem("", true, "", "");

  bool toggleStateOn;

  bool skipSection = false;
  bool skipSystem = false;

  std::string lastSection;
  std::string drawLocation;

  std::string commandName;
  std::string commandFooter;
  bool commandFooterHighlight;
  bool commandFooterHighlightDefined;
  bool isHold;
  std::string commandSystem;
  std::string commandMode;
  std::string commandGrouping;

  std::string currentSection;
  std::string defaultToggleState;
  std::string sourceType, sourceTypeOn, sourceTypeOff;

  std::string packageSource;
  // std::string jsonPath, jsonPathOn, jsonPathOff;
  // std::string jsonKey, jsonKeyOn, jsonKeyOff;

  // std::string iniFilePath;

  std::string optionName;
  std::vector<std::vector<std::string>> commands, commandsOn, commandsOff;
  std::vector<std::vector<std::string>> tableData;

  std::string itemName, parentDirName;

  s16 minValue;
  s16 maxValue;
  std::string units;
  size_t steps;
  bool unlockedTrackbar;
  bool onEveryTick;
  std::string footer;
  bool useSelection;
  size_t pos;

  bool inEristaSection, inMarikoSection;
  bool _inEristaSection, _inMarikoSection;

  bool hideTableBackground, useHeaderIndent;
  size_t tableStartGap, tableEndGap, tableColumnOffset, tableSpacing;
  std::string tableSectionTextColor, tableInfoTextColor, tableAlignment;
  std::string tableWrappingMode;

  bool useWrappingIndent;

  size_t delimiterPos;

  bool usingProgress;

  bool isPolling;
  bool isScrollableTable;
  bool usingTopPivot, usingBottomPivot;
  bool onlyTables = true;

  // std::vector<std::string> entryList;

  std::string commandNameLower;

  std::string cleanOptionName;

  std::string lastPackageHeader;

  bool isMini;

  // update general placeholders
  updateGeneralPlaceholders();

  std::vector<std::pair<std::string, std::vector<std::vector<std::string>>>>
      options = loadOptionsFromIni(packageIniPath);
  for (size_t i = 0; i < options.size(); ++i) {
    optionName.clear();
    commands.clear();
    commandsOn.clear();
    commandsOff.clear();
    tableData.clear();

    auto &option = options[i];

    optionName = std::move(option.first);
    // option.first.shrink_to_fit();

    commands = std::move(option.second);
    // option.second.shrink_to_fit();

    // Remove all empty command strings
    removeEmptyCommands(commands);

    // option.first.clear();
    // option.second.clear();
    // option.second.shrink_to_fit();

    footer = "";
    useSelection = false;

    isMini = false;

    // toggle settings
    usingProgress = false;

    // Table settings
    isPolling = false;
    isScrollableTable = true;
    usingTopPivot = false;
    usingBottomPivot = false;
    hideTableBackground = false;
    useHeaderIndent = false;
    tableStartGap = 20;
    tableEndGap = 9;
    tableColumnOffset = 164;
    tableSpacing = 0;
    tableSectionTextColor = DEFAULT_STR;
    tableInfoTextColor = DEFAULT_STR;
    tableAlignment = RIGHT_STR;
    tableWrappingMode = "none";
    useWrappingIndent = false;

    // Trackbar settings
    minValue = 0;
    maxValue = 100;
    units = "";
    steps = 0;
    unlockedTrackbar = true;
    onEveryTick = false;
    commandFooter = "";
    commandFooterHighlight = false;
    commandFooterHighlightDefined = false;
    isHold = false;
    commandSystem = DEFAULT_STR;
    commandMode = DEFAULT_STR;
    commandGrouping = DEFAULT_STR;

    defaultToggleState = "";
    currentSection = GLOBAL_STR;
    sourceType = DEFAULT_STR;
    sourceTypeOn = DEFAULT_STR;
    sourceTypeOff = DEFAULT_STR;

    bool isSlot = false;

    if (drawLocation.empty() || (currentPage == drawLocation) ||
        (optionName.front() == '@')) {

      // Custom header implementation
      if (!dropdownSection.empty()) {
        if (i == 0) {
          // Add a section break with small text to indicate the "Commands"
          // section
          std::string headerTitle = dropdownSection.substr(1);
          removeTag(headerTitle);

          addHeader(list, headerTitle);
          lastPackageHeader = headerTitle;
          // wasHeader = true;
          skipSection = true;
          lastSection = dropdownSection;
        }
        cleanOptionName = optionName;
        removeTag(cleanOptionName);
        if (cleanOptionName == PACKAGE_INFO ||
            cleanOptionName == "Package Info") {
          if (!skipSection) {
            lastSection = optionName;
            addPackageInfo(list, packageHeader);
          }
        }
        if (optionName.front() == '*' && commands.size() > 0 &&
            commands.size() <= 2) {
          auto settings = parseCommandSettings(commands);
          isMini = settings.isMini;
          isSlot = settings.isSlot;
        }
        if (commands.size() == 0) {
          if (optionName == dropdownSection)
            skipSection = false;
          else
            skipSection = true;
          continue;
        }
      } else {

        if (optionName.front() == '*' && commands.size() > 0 &&
            commands.size() <= 2) {
          auto settings = parseCommandSettings(commands);
          isMini = settings.isMini;
          isSlot = settings.isSlot;
        }

        if (commands.size() == 0) {
          if (optionName.front() == '@') {
            if (drawLocation.empty()) {
              pageLeftName = optionName.substr(1);
              drawLocation = LEFT_STR;
            } else {
              pageRightName = optionName.substr(1);
              usingPages = true;
              drawLocation = RIGHT_STR;
            }
          } else if (optionName.front() == '*') {
            if (i == 0) {
              // Add a section break with small text to indicate the "Commands"
              // section
              addHeader(list, COMMANDS);
              lastPackageHeader = COMMANDS;
              // wasHeader = true;
              skipSection = false;
              lastSection = "Commands";
            }
            // commandFooter = parseValueFromIniSection(packageConfigIniPath,
            // optionName, FOOTER_STR);

            // First, scan for patterns to establish defaults
            bool foundFooter = false;
            bool foundFooterHighlight = false;

            for (const auto &command : commands) {
              // if (!command.empty()) {
              {
                const std::string &_commandName = command[0];
                const size_t cmdLen = _commandName.length();

                // Quick check: if starts with ';' and has at least one more
                // char
                if (cmdLen > 1 && _commandName[0] == ';') {
                  const char secondChar = _commandName[1];

                  switch (secondChar) {
                  case 'f':
                    // Could be ;footer= or ;footer_highlight=
                    if (!foundFooter && cmdLen > FOOTER_PATTERN_LEN &&
                        _commandName[7] == '=') {
                      if (_commandName.compare(0, FOOTER_PATTERN_LEN,
                                               FOOTER_PATTERN) == 0) {
                        commandFooter = _commandName.substr(FOOTER_PATTERN_LEN);
                        // If there are additional arguments, append them
                        // (handles spaces in footer)
                        for (size_t j = 1; j < command.size(); ++j) {
                          commandFooter += " " + command[j];
                        }
                        removeQuotes(commandFooter);
                        foundFooter = true;
                        if (foundFooterHighlight)
                          break; // Early exit
                      }
                    }

                    if (!foundFooterHighlight &&
                        cmdLen >= FOOTER_HIGHLIGHT_PATTERN_LEN + TRUE_STR_LEN &&
                        _commandName[7] == '_') {
                      if (_commandName.compare(0, FOOTER_HIGHLIGHT_PATTERN_LEN,
                                               FOOTER_HIGHLIGHT_PATTERN) == 0) {
                        switch (_commandName[FOOTER_HIGHLIGHT_PATTERN_LEN]) {
                        case 't':
                          if (_commandName.compare(FOOTER_HIGHLIGHT_PATTERN_LEN,
                                                   TRUE_STR_LEN,
                                                   TRUE_STR) == 0) {
                            commandFooterHighlight = true;
                            commandFooterHighlightDefined = true;
                            foundFooterHighlight = true;
                          }
                          break;
                        case 'f':
                          if (_commandName.compare(FOOTER_HIGHLIGHT_PATTERN_LEN,
                                                   FALSE_STR_LEN,
                                                   FALSE_STR) == 0) {
                            commandFooterHighlight = false;
                            commandFooterHighlightDefined = true;
                            foundFooterHighlight = true;
                          }
                          break;
                        }
                      }
                    }
                    break;
                  }

                  // Early exit if both found
                  if (foundFooter && foundFooterHighlight) {
                    break;
                  }
                }
              }
            }

            // Then, load from config INI (overwrites defaults if they exist)
            packageConfigData = getParsedDataFromIniFile(packageConfigIniPath);
            auto optionIt = packageConfigData.find(optionName);
            if (optionIt != packageConfigData.end()) {
              auto footerIt = optionIt->second.find(FOOTER_STR);
              if (footerIt != optionIt->second.end()) {
                commandFooter = footerIt->second;
              }

              // Load footer_highlight from config INI if it exists
              auto footerHighlightIt =
                  optionIt->second.find("footer_highlight");
              if (footerHighlightIt != optionIt->second.end()) {
                commandFooterHighlight =
                    (footerHighlightIt->second == TRUE_STR);
                commandFooterHighlightDefined = true;
              }
            }
            packageConfigData.clear();

            // override loading of the command footer

            tsl::elm::ListItem *listItem;
            if (!commandFooter.empty() && commandFooter != NULL_STR) {
              footer = commandFooter;
              cleanOptionName = optionName.substr(1);
              // removeTag(cleanOptionName);
              listItem = new tsl::elm::ListItem(cleanOptionName, "", isMini);
              listItem->enableShortHoldKey();
              listItem->m_shortHoldKey = SCRIPT_KEY;

              listItem->setValue(footer, commandFooterHighlightDefined
                                             ? !commandFooterHighlight
                                             : false);
            } else {
              footer = !isSlot ? DROPDOWN_SYMBOL : OPTION_SYMBOL;
              cleanOptionName = optionName.substr(1);
              // removeTag(cleanOptionName);
              //  Create reference to PackageMenu with dropdownSection set to
              //  optionName
              listItem =
                  new tsl::elm::ListItem(cleanOptionName, footer, isMini);
              listItem->enableShortHoldKey();
              listItem->m_shortHoldKey = SCRIPT_KEY;
            }

            if (packageMenuMode) {
              listItem->setClickListener([listItem, packagePath,
                                          dropdownSection, currentPage,
                                          packageName, nestedLayer, i,
                                          packageIniPath, optionName,
                                          cleanOptionName, pageHeader,
                                          lastPackageHeader,
                                          showWidget](s64 keys) {
                if (runningInterpreter.load(acquire))
                  return false;

                if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
                  inPackageMenu = false;

                  nestedMenuCount++;
                  // tsl::clearGlyphCacheNow.store(true, release);
                  //  Store return info

                  returnContextStack.push({
                      .packagePath = packagePath,
                      .sectionName = dropdownSection,
                      .currentPage = currentPage,
                      .packageName = packageName,
                      .pageHeader = pageHeader, // Move this before nestedLayer
                      .option = cleanOptionName,
                      .nestedLayer = nestedLayer // Move this to the end
                  });

                  tsl::swapTo<PackageMenu>(packagePath, optionName, currentPage,
                                           packageName, nestedMenuCount,
                                           lastPackageHeader);

                  return true;

                } else if (keys & SCRIPT_KEY &&
                           !(keys & ~SCRIPT_KEY & ALL_KEYS_MASK)) {
                  // if (inPackageMenu)
                  //     inPackageMenu = false;
                  // if (inSubPackageMenu)
                  //     inSubPackageMenu = false;

                  // Gather the prompt commands for the current dropdown section
                  // const std::vector<std::vector<std::string>> promptCommands
                  // = gatherPromptCommands(optionName, options);

                  // auto options = loadOptionsFromIni(packageIniPath);
                  //  Pass all gathered commands to the ScriptOverlay
                  tsl::shiftItemFocus(listItem);
                  tsl::changeTo<ScriptOverlay>(
                      std::move(gatherPromptCommands(
                          optionName,
                          std::move(loadOptionsFromIni(packageIniPath)))),
                      packagePath, optionName, "package", true,
                      lastPackageHeader, showWidget);
                  return true;
                }
                return false;
              });
              listItem->disableClickAnimation();
            } else {
              listItem->setClickListener([listItem, optionName, cleanOptionName,
                                          i, packageIniPath, lastPackageHeader,
                                          showWidget](s64 keys) {
                if (runningInterpreter.load(acquire))
                  return false;

                if (simulatedMenu.load(std::memory_order_acquire)) {
                  keys |= SYSTEM_SETTINGS_KEY;
                }

                if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
                  inPackageMenu = false;

                  tsl::shiftItemFocus(listItem);
                  // tsl::changeTo<MainMenu>("", optionName);
                  TransitionToMainMenu("", optionName);
                  return true;
                } else if (keys & SCRIPT_KEY &&
                           !(keys & ~SCRIPT_KEY & ALL_KEYS_MASK)) {
                  // if (inMainMenu.load(acquire)) {
                  //     inMainMenu.store(false, std::memory_order_release);
                  // }

                  // Gather the prompt commands for the current dropdown section
                  // const std::vector<std::vector<std::string>> promptCommands
                  // = gatherPromptCommands(optionName, options); auto options =
                  // loadOptionsFromIni(packageIniPath)

                  tsl::shiftItemFocus(listItem);
                  tsl::changeTo<ScriptOverlay>(
                      std::move(gatherPromptCommands(
                          optionName,
                          std::move(loadOptionsFromIni(packageIniPath)))),
                      PACKAGE_PATH, optionName, "main", true, lastPackageHeader,
                      showWidget);
                  return true;
                } else if (keys & SYSTEM_SETTINGS_KEY &&
                           !(keys & ~SYSTEM_SETTINGS_KEY & ALL_KEYS_MASK)) {
                  skipJumpReset.store(false, std::memory_order_release);
                  returnJumpItemName = cleanOptionName;
                  returnJumpItemValue = "";
                  return true;
                }
                return false;
              });
            }
            onlyTables = false;
            // lastItemIsScrollableTable = false;
            list->addItem(listItem);

            skipSection = true;
          } else {
            if (optionName != lastSection) {
              cleanOptionName = optionName;
              removeTag(cleanOptionName);
              if (cleanOptionName == PACKAGE_INFO ||
                  cleanOptionName == "Package Info") {
                // logMessage("pre-before adding app info");
                if (!skipSection) {
                  lastSection = optionName;
                  // logMessage("before adding app info");
                  addPackageInfo(list, packageHeader);
                  // logMessage("after adding app info");
                }
              } else {
                // Add a section break with small text to indicate the
                // "Commands" section
                addHeader(list, cleanOptionName);
                lastPackageHeader = cleanOptionName;

                // wasHeader = true;
                lastSection = optionName;
              }
            }
            skipSection = false;
          }

          continue;
        } else if (i == 0) {
          // Add a section break with small text to indicate the "Commands"
          // section
          addHeader(list, COMMANDS);
          lastPackageHeader = COMMANDS;
          // wasHeader = true;
          skipSection = false;
          lastSection = "Commands";
        }
      }

      // Call the function
      // processCommands(cmdOptions, cmdData);

      inEristaSection = false;
      inMarikoSection = false;

      // Remove all empty command strings
      // removeEmptyCommands(commands);

      // Initial processing of commands (DUPLICATE CODE)
      for (auto &cmd : commands) {
        // if (cmd.empty()) continue;

        commandName = cmd[0];

        // Quick check for section markers
        if (commandName.length() == 7) {
          if ((commandName[0] == 'e' || commandName[0] == 'E') &&
              commandName[1] == 'r' && commandName[2] == 'i' &&
              commandName[3] == 's' && commandName[4] == 't' &&
              commandName[5] == 'a' && commandName[6] == ':') {
            inEristaSection = true;
            inMarikoSection = false;
            continue;
          } else if ((commandName[0] == 'm' || commandName[0] == 'M') &&
                     commandName[1] == 'a' && commandName[2] == 'r' &&
                     commandName[3] == 'i' && commandName[4] == 'k' &&
                     commandName[5] == 'o' && commandName[6] == ':') {
            inEristaSection = false;
            inMarikoSection = true;
            continue;
          }
        }

        if ((inEristaSection && !inMarikoSection && usingErista) ||
            (!inEristaSection && inMarikoSection && usingMariko) ||
            (!inEristaSection && !inMarikoSection)) {

          // Quick check: if starts with ';' and has at least one more char
          if (commandName.length() > 1 && commandName[0] == ';') {
            const char secondChar = commandName[1];

            // Branch on second character for faster pattern matching
            switch (secondChar) {
            case 's':
              if (commandName.compare(0, SYSTEM_PATTERN_LEN, SYSTEM_PATTERN) ==
                  0) {
                commandSystem = commandName.substr(SYSTEM_PATTERN_LEN);
                if (std::find(commandSystems.begin(), commandSystems.end(),
                              commandSystem) == commandSystems.end())
                  commandSystem = commandSystems[0];
                continue;
              }
              if (commandName.compare(0, SCROLLABLE_PATTERN_LEN,
                                      SCROLLABLE_PATTERN) == 0) {
                switch (commandName[SCROLLABLE_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(SCROLLABLE_PATTERN_LEN, TRUE_STR_LEN,
                                          TRUE_STR) == 0) {
                    isScrollableTable = true;
                  }
                  break;
                case 'f':
                  if (commandName.compare(SCROLLABLE_PATTERN_LEN, FALSE_STR_LEN,
                                          FALSE_STR) == 0) {
                    isScrollableTable = false;
                  }
                  break;
                }
                continue;
              }
              if (commandName.compare(0, START_GAP_PATTERN_LEN,
                                      START_GAP_PATTERN) == 0) {
                tableStartGap =
                    ult::stoi(commandName.substr(START_GAP_PATTERN_LEN));
                continue;
              }
              if (commandName.compare(0, SPACING_PATTERN_LEN,
                                      SPACING_PATTERN) == 0) {
                tableSpacing =
                    ult::stoi(commandName.substr(SPACING_PATTERN_LEN));
                continue;
              }
              if (commandName.compare(0, SECTION_TEXT_COLOR_PATTERN_LEN,
                                      SECTION_TEXT_COLOR_PATTERN) == 0) {
                tableSectionTextColor =
                    commandName.substr(SECTION_TEXT_COLOR_PATTERN_LEN);
                continue;
              }
              if (commandName.compare(0, STEPS_PATTERN_LEN, STEPS_PATTERN) ==
                  0) {
                steps = ult::stoi(commandName.substr(STEPS_PATTERN_LEN));
                continue;
              }
              break;

            case 'm':
              if (commandName.compare(0, MODE_PATTERN_LEN, MODE_PATTERN) == 0) {
                commandMode = commandName.substr(MODE_PATTERN_LEN);
                if (commandMode.find(TOGGLE_STR) != std::string::npos) {
                  delimiterPos = commandMode.find('?');
                  if (delimiterPos != std::string::npos) {
                    defaultToggleState = commandMode.substr(delimiterPos + 1);
                  }
                  commandMode = TOGGLE_STR;
                } else if (std::find(commandModes.begin(), commandModes.end(),
                                     commandMode) == commandModes.end()) {
                  commandMode = commandModes[0];
                }
                continue;
              }
              if (commandName.compare(0, MINI_PATTERN_LEN, MINI_PATTERN) == 0) {
                switch (commandName[MINI_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(MINI_PATTERN_LEN, TRUE_STR_LEN,
                                          TRUE_STR) == 0) {
                    isMini = true;
                  }
                  break;
                case 'f':
                  if (commandName.compare(MINI_PATTERN_LEN, FALSE_STR_LEN,
                                          FALSE_STR) == 0) {
                    isMini = false;
                  }
                  break;
                }
                continue;
              }
              if (commandName.compare(0, MIN_VALUE_PATTERN_LEN,
                                      MIN_VALUE_PATTERN) == 0) {
                minValue = ult::stoi(commandName.substr(MIN_VALUE_PATTERN_LEN));
                continue;
              }
              if (commandName.compare(0, MAX_VALUE_PATTERN_LEN,
                                      MAX_VALUE_PATTERN) == 0) {
                maxValue = ult::stoi(commandName.substr(MAX_VALUE_PATTERN_LEN));
                continue;
              }
              break;

            case 'g':
              if (commandName.compare(0, GROUPING_PATTERN_LEN,
                                      GROUPING_PATTERN) == 0) {
                commandGrouping = commandName.substr(GROUPING_PATTERN_LEN);
                if (std::find(commandGroupings.begin(), commandGroupings.end(),
                              commandGrouping) == commandGroupings.end())
                  commandGrouping = commandGroupings[0];
                continue;
              }
              if (commandName.compare(0, END_GAP_PATTERN_ALIAS_LEN,
                                      END_GAP_PATTERN_ALIAS) == 0) {
                tableEndGap =
                    ult::stoi(commandName.substr(END_GAP_PATTERN_ALIAS_LEN));
                continue;
              }
              break;

            case 'f':
              if (commandName.compare(0, FOOTER_HIGHLIGHT_PATTERN_LEN,
                                      FOOTER_HIGHLIGHT_PATTERN) == 0) {
                switch (commandName[FOOTER_HIGHLIGHT_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(FOOTER_HIGHLIGHT_PATTERN_LEN,
                                          TRUE_STR_LEN, TRUE_STR) == 0) {
                    commandFooterHighlight = true;
                    commandFooterHighlightDefined = true;
                  }
                  break;
                case 'f':
                  if (commandName.compare(FOOTER_HIGHLIGHT_PATTERN_LEN,
                                          FALSE_STR_LEN, FALSE_STR) == 0) {
                    commandFooterHighlight = false;
                    commandFooterHighlightDefined = true;
                  }
                  break;
                }
                continue;
              }
              if (commandName.compare(0, FOOTER_PATTERN_LEN, FOOTER_PATTERN) ==
                  0) {
                commandFooter = commandName.substr(FOOTER_PATTERN_LEN);
                // If there are additional arguments, append them (handles
                // spaces in footer)
                for (size_t j = 1; j < cmd.size(); ++j) {
                  commandFooter += " " + cmd[j];
                }
                removeQuotes(commandFooter);
                continue;
              }
              break;

            case 'h':
              if (commandName.compare(0, HOLD_PATTERN_LEN, HOLD_PATTERN) == 0) {
                switch (commandName[HOLD_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(HOLD_PATTERN_LEN, TRUE_STR_LEN,
                                          TRUE_STR) == 0) {
                    isHold = true;
                  }
                  break;
                case 'f':
                  if (commandName.compare(HOLD_PATTERN_LEN, FALSE_STR_LEN,
                                          FALSE_STR) == 0) {
                    isHold = false;
                  }
                  break;
                }
                continue;
              }
              if (commandName.compare(0, HEADER_INDENT_PATTERN_LEN,
                                      HEADER_INDENT_PATTERN) == 0) {
                switch (commandName[HEADER_INDENT_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(HEADER_INDENT_PATTERN_LEN,
                                          TRUE_STR_LEN, TRUE_STR) == 0) {
                    useHeaderIndent = true;
                  }
                  break;
                case 'f':
                  if (commandName.compare(HEADER_INDENT_PATTERN_LEN,
                                          FALSE_STR_LEN, FALSE_STR) == 0) {
                    useHeaderIndent = false;
                  }
                  break;
                }
                continue;
              }
              break;

            case 'p':
              if (commandName.compare(0, PROGRESS_PATTERN_LEN,
                                      PROGRESS_PATTERN) == 0) {
                switch (commandName[PROGRESS_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(PROGRESS_PATTERN_LEN, TRUE_STR_LEN,
                                          TRUE_STR) == 0) {
                    usingProgress = true;
                  }
                  break;
                case 'f':
                  if (commandName.compare(PROGRESS_PATTERN_LEN, FALSE_STR_LEN,
                                          FALSE_STR) == 0) {
                    usingProgress = false;
                  }
                  break;
                }
                continue;
              }
              if (commandName.compare(0, POLLING_PATTERN_LEN,
                                      POLLING_PATTERN) == 0) {
                switch (commandName[POLLING_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(POLLING_PATTERN_LEN, TRUE_STR_LEN,
                                          TRUE_STR) == 0) {
                    isPolling = true;
                  }
                  break;
                case 'f':
                  if (commandName.compare(POLLING_PATTERN_LEN, FALSE_STR_LEN,
                                          FALSE_STR) == 0) {
                    isPolling = false;
                  }
                  break;
                }
                continue;
              }
              break;

            case 't':
              if (commandName.compare(0, TOP_PIVOT_PATTERN_LEN,
                                      TOP_PIVOT_PATTERN) == 0) {
                switch (commandName[TOP_PIVOT_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(TOP_PIVOT_PATTERN_LEN, TRUE_STR_LEN,
                                          TRUE_STR) == 0) {
                    usingTopPivot = true;
                  }
                  break;
                case 'f':
                  if (commandName.compare(TOP_PIVOT_PATTERN_LEN, FALSE_STR_LEN,
                                          FALSE_STR) == 0) {
                    usingTopPivot = false;
                  }
                  break;
                }
                continue;
              }
              break;

            case 'b':
              if (commandName.compare(0, BOTTOM_PIVOT_PATTERN_LEN,
                                      BOTTOM_PIVOT_PATTERN) == 0) {
                switch (commandName[BOTTOM_PIVOT_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(BOTTOM_PIVOT_PATTERN_LEN,
                                          TRUE_STR_LEN, TRUE_STR) == 0) {
                    usingBottomPivot = true;
                  }
                  break;
                case 'f':
                  if (commandName.compare(BOTTOM_PIVOT_PATTERN_LEN,
                                          FALSE_STR_LEN, FALSE_STR) == 0) {
                    usingBottomPivot = false;
                  }
                  break;
                }
                continue;
              }
              if (commandName.compare(0, BACKGROUND_PATTERN_LEN,
                                      BACKGROUND_PATTERN) == 0) {
                switch (commandName[BACKGROUND_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(BACKGROUND_PATTERN_LEN, TRUE_STR_LEN,
                                          TRUE_STR) == 0) {
                    hideTableBackground = false;
                  }
                  break;
                case 'f':
                  if (commandName.compare(BACKGROUND_PATTERN_LEN, FALSE_STR_LEN,
                                          FALSE_STR) == 0) {
                    hideTableBackground = true;
                  }
                  break;
                }
                continue;
              }
              break;

            case 'e':
              if (commandName.compare(0, END_GAP_PATTERN_LEN,
                                      END_GAP_PATTERN) == 0) {
                tableEndGap =
                    ult::stoi(commandName.substr(END_GAP_PATTERN_LEN));
                continue;
              }
              break;

            case 'o':
              if (commandName.compare(0, OFFSET_PATTERN_LEN, OFFSET_PATTERN) ==
                  0) {
                tableColumnOffset =
                    ult::stoi(commandName.substr(OFFSET_PATTERN_LEN));
                continue;
              }
              if (commandName.compare(0, ON_EVERY_TICK_PATTERN_LEN,
                                      ON_EVERY_TICK_PATTERN) == 0) {
                switch (commandName[ON_EVERY_TICK_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(ON_EVERY_TICK_PATTERN_LEN,
                                          TRUE_STR_LEN, TRUE_STR) == 0) {
                    onEveryTick = true;
                  }
                  break;
                case 'f':
                  if (commandName.compare(ON_EVERY_TICK_PATTERN_LEN,
                                          FALSE_STR_LEN, FALSE_STR) == 0) {
                    onEveryTick = false;
                  }
                  break;
                }
                continue;
              }
              break;

            case 'i':
              if (commandName.compare(0, INFO_TEXT_COLOR_PATTERN_LEN,
                                      INFO_TEXT_COLOR_PATTERN) == 0) {
                tableInfoTextColor =
                    commandName.substr(INFO_TEXT_COLOR_PATTERN_LEN);
                continue;
              }
              break;

            case 'a':
              if (commandName.compare(0, ALIGNMENT_PATTERN_LEN,
                                      ALIGNMENT_PATTERN) == 0) {
                tableAlignment = commandName.substr(ALIGNMENT_PATTERN_LEN);
                continue;
              }
              break;

            case 'w':
              if (commandName.compare(0, WRAPPING_MODE_PATTERN_LEN,
                                      WRAPPING_MODE_PATTERN) == 0) {
                tableWrappingMode =
                    commandName.substr(WRAPPING_MODE_PATTERN_LEN);
                continue;
              }
              if (commandName.compare(0, WRAPPING_INDENT_PATTERN_LEN,
                                      WRAPPING_INDENT_PATTERN) == 0) {
                switch (commandName[WRAPPING_INDENT_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(WRAPPING_INDENT_PATTERN_LEN,
                                          TRUE_STR_LEN, TRUE_STR) == 0) {
                    useWrappingIndent = true;
                  }
                  break;
                case 'f':
                  if (commandName.compare(WRAPPING_INDENT_PATTERN_LEN,
                                          FALSE_STR_LEN, FALSE_STR) == 0) {
                    useWrappingIndent = false;
                  }
                  break;
                }
                continue;
              }
              break;

            case 'u':
              if (commandName.compare(0, UNITS_PATTERN_LEN, UNITS_PATTERN) ==
                  0) {
                units = commandName.substr(UNITS_PATTERN_LEN);
                removeQuotes(units);
                continue;
              }
              if (commandName.compare(0, UNLOCKED_PATTERN_LEN,
                                      UNLOCKED_PATTERN) == 0) {
                switch (commandName[UNLOCKED_PATTERN_LEN]) {
                case 't':
                  if (commandName.compare(UNLOCKED_PATTERN_LEN, TRUE_STR_LEN,
                                          TRUE_STR) == 0) {
                    unlockedTrackbar = true;
                  }
                  break;
                case 'f':
                  if (commandName.compare(UNLOCKED_PATTERN_LEN, FALSE_STR_LEN,
                                          FALSE_STR) == 0) {
                    unlockedTrackbar = false;
                  }
                  break;
                }
                continue;
              }
              break;
            }

            // If we got here, it's a `;` command that didn't match any pattern
            // Treat it as a comment and skip it
            continue;
          }

          if (commandMode == TOGGLE_STR) {
            if (commandName.compare(0, 3, "on:") == 0)
              currentSection = ON_STR;
            else if (commandName.compare(0, 4, "off:") == 0)
              currentSection = OFF_STR;

            if (currentSection == GLOBAL_STR) {
              commandsOn.push_back(cmd);
              commandsOff.push_back(cmd);
            } else if (currentSection == ON_STR) {
              commandsOn.push_back(cmd);
            } else if (currentSection == OFF_STR) {
              commandsOff.push_back(cmd);
            }
          } else if (commandMode == TABLE_STR) {
            tableData.push_back(cmd);
            continue;
          } else if (commandMode == TRACKBAR_STR ||
                     commandMode == STEP_TRACKBAR_STR ||
                     commandMode == NAMED_STEP_TRACKBAR_STR) {
            continue;
          }

          if (cmd.size() > 1) {
            if (commandName == "file_source") {
              if (currentSection == GLOBAL_STR) {
                pathPattern = cmd[1];
                preprocessPath(pathPattern, packagePath);
                sourceType = FILE_STR;
              } else if (currentSection == ON_STR) {
                pathPatternOn = cmd[1];
                preprocessPath(pathPatternOn, packagePath);
                sourceTypeOn = FILE_STR;
              } else if (currentSection == OFF_STR) {
                pathPatternOff = cmd[1];
                preprocessPath(pathPatternOff, packagePath);
                sourceTypeOff = FILE_STR;
              }
            } else if (commandName == "package_source") {
              packageSource = cmd[1];
              preprocessPath(packageSource, packagePath);
            }
          }
        }
      }

      if (isFile(packageConfigIniPath)) {
        packageConfigData = getParsedDataFromIniFile(packageConfigIniPath);

        bool shouldSaveINI = false;

        // Only sync footer if pattern was provided
        shouldSaveINI |= syncIniValue(packageConfigData, packageConfigIniPath,
                                      optionName, FOOTER_STR, commandFooter);

        // Always try to load footer_highlight from config.ini if it exists
        auto optionIt = packageConfigData.find(optionName);
        if (optionIt != packageConfigData.end()) {
          auto footerHighlightIt = optionIt->second.find("footer_highlight");
          if (footerHighlightIt != optionIt->second.end() &&
              !footerHighlightIt->second.empty()) {
            // Load the non-empty value from config.ini
            commandFooterHighlight = (footerHighlightIt->second == TRUE_STR);
            commandFooterHighlightDefined = true;
          }
        }

        // Only write footer_highlight back if it was defined in package INI but
        // missing/empty in config
        if (commandFooterHighlightDefined &&
            optionIt != packageConfigData.end()) {
          auto it = optionIt->second.find("footer_highlight");
          if (it == optionIt->second.end() || it->second.empty()) {
            packageConfigData[optionName]["footer_highlight"] =
                commandFooterHighlight ? TRUE_STR : FALSE_STR;
            shouldSaveINI = true;
          }
        }

        // Save all changes in one batch write
        if (shouldSaveINI)
          saveIniFileData(packageConfigIniPath, packageConfigData);
        packageConfigData.clear();
      } else { // write default data if settings are not loaded
        // Load any existing data first (might be empty if file doesn't exist)
        packageConfigData = getParsedDataFromIniFile(packageConfigIniPath);

        // Only add footer if pattern was provided
        if (!commandFooter.empty() && commandFooter != NULL_STR) {
          packageConfigData[optionName][FOOTER_STR] = commandFooter;
        }
        // Only add footer_highlight if it was defined in package INI
        if (commandFooterHighlightDefined) {
          packageConfigData[optionName]["footer_highlight"] =
              commandFooterHighlight ? TRUE_STR : FALSE_STR;
        }

        // Save the config file once
        if ((!commandFooter.empty() && commandFooter != NULL_STR) ||
            commandFooterHighlightDefined) {
          saveIniFileData(packageConfigIniPath, packageConfigData);
        }
        packageConfigData.clear();
      }

      // Get Option name and footer
      const std::string originalOptionName = optionName;
      if (!optionName.empty() && optionName[0] == '*') {
        useSelection = true;
        optionName.erase(0, 1); // Remove first character in-place
        footer = DROPDOWN_SYMBOL;
      } else {
        pos = optionName.find(" - ");
        if (pos != std::string::npos) {
          footer.assign(optionName, pos + 3,
                        std::string::npos); // Direct assignment from substring
          optionName.resize(pos); // Truncate in-place instead of substr
        }
      }

      if ((commandMode == OPTION_STR) || (commandMode == SLOT_STR) ||
          (commandMode == TOGGLE_STR && !useSelection)) {
        footer = OPTION_SYMBOL;
      }

      // override loading of the command footer
      if (!commandFooter.empty() && commandFooter != NULL_STR)
        footer = commandFooter;

      skipSystem = false;
      if (commandSystem == ERISTA_STR && !usingErista) {
        skipSystem = true;
      } else if (commandSystem == MARIKO_STR && !usingMariko) {
        skipSystem = true;
      }

      if (!skipSection && !skipSystem) { // for skipping the drawing of sections
        if (commandMode == TABLE_STR) {
          if (useHeaderIndent) {
            tableColumnOffset = 164;
            tableStartGap = tableEndGap =
                19 - 2; // for perfect alignment for header tables
            isScrollableTable = false;
            lastPackageHeader = getFirstSectionText(tableData, packagePath);
          }

          if (usingTopPivot) {
            if (list->getLastIndex() == 0)
              onlyTables = false;

            addDummyListItem(list);
          }

          addTable(list, tableData, packagePath, tableColumnOffset,
                   tableStartGap, tableEndGap, tableSpacing,
                   tableSectionTextColor, tableInfoTextColor,
                   tableInfoTextColor, tableAlignment, hideTableBackground,
                   useHeaderIndent, isPolling, isScrollableTable,
                   tableWrappingMode, useWrappingIndent);
          tableData.clear();

          if (usingBottomPivot) {
            addDummyListItem(list);
          }

          continue;
        } else if (commandMode == TRACKBAR_STR) {
          onlyTables = false;

          // Create TrackBarV2 instance and configure it
          auto trackBar = new tsl::elm::TrackBarV2(
              optionName, packagePath, minValue, maxValue, units,
              interpretAndExecuteCommands, getSourceReplacement, commands,
              originalOptionName, false, false, -1, unlockedTrackbar,
              onEveryTick);

          // Set the SCRIPT_KEY listener
          trackBar->setScriptKeyListener([commands,
                                          keyName = originalOptionName,
                                          packagePath, lastPackageHeader,
                                          showWidget, trackBar]() {
            // const std::string valueStr =
            // parseValueFromIniSection(packagePath+"config.ini", keyName,
            // "value"); std::string indexStr =
            // parseValueFromIniSection(packagePath+"config.ini", keyName,
            // "index");

            // Use trackbar's CURRENT state instead of reading from file
            const std::string valueStr =
                ult::to_string(trackBar->getProgress());
            const std::string indexStr = ult::to_string(trackBar->getIndex());

            // Handle the commands and placeholders for the trackbar
            auto modifiedCmds = getSourceReplacement(
                commands, keyName, ult::stoi(indexStr), packagePath);

            // auto modifiedCmds = getSourceReplacement(commands, valueStr,
            // m_index, m_packagePath);

            // Placeholder replacement
            // const std::string valuePlaceholder = "{value}";
            // const std::string indexPlaceholder = "{index}";
            // const size_t valuePlaceholderLength = valuePlaceholder.length();
            // const size_t indexPlaceholderLength = indexPlaceholder.length();

            size_t pos;
            for (auto &cmd : modifiedCmds) {
              for (auto &arg : cmd) {
                pos = 0;
                while ((pos = arg.find(valuePlaceholder, pos)) !=
                       std::string::npos) {
                  arg.replace(pos, valuePlaceholderLength, valueStr);
                  pos += valueStr.length();
                }
                pos = 0;
                while ((pos = arg.find(indexPlaceholder, pos)) !=
                       std::string::npos) {
                  arg.replace(pos, indexPlaceholderLength, indexStr);
                  pos += indexStr.length();
                }
              }
            }

            applyPlaceholderReplacementsToCommands(modifiedCmds, packagePath);

            const bool isFromMainMenu = (packagePath == PACKAGE_PATH);

            tsl::shiftItemFocus(trackBar);

            // Switch to ScriptOverlay
            tsl::changeTo<ScriptOverlay>(std::move(modifiedCmds), packagePath,
                                         keyName,
                                         isFromMainMenu ? "main" : "package",
                                         false, lastPackageHeader, showWidget);
          });

          // Add the TrackBarV2 to the list after setting the necessary
          // listeners
          list->addItem(trackBar);

          continue;
        } else if (commandMode == STEP_TRACKBAR_STR) {
          if (steps == 0) { // assign minimum steps
            steps = std::abs(maxValue - minValue) + 1;
          }
          onlyTables = false;

          auto stepTrackBar = new tsl::elm::StepTrackBarV2(
              optionName, packagePath, steps, minValue, maxValue, units,
              interpretAndExecuteCommands, getSourceReplacement, commands,
              originalOptionName, false, unlockedTrackbar, onEveryTick);

          // Set the SCRIPT_KEY listener
          stepTrackBar->setScriptKeyListener([commands,
                                              keyName = originalOptionName,
                                              packagePath, lastPackageHeader,
                                              showWidget, stepTrackBar]() {
            const bool isFromMainMenu = (packagePath == PACKAGE_PATH);

            // Parse the value and index from the INI file
            // const std::string valueStr = parseValueFromIniSection(packagePath
            // + "config.ini", keyName, "value"); std::string indexStr =
            // parseValueFromIniSection(packagePath + "config.ini", keyName,
            // "index");

            // Use stepTrackBar's CURRENT state
            const std::string valueStr =
                ult::to_string(stepTrackBar->getProgress());
            const std::string indexStr =
                ult::to_string(stepTrackBar->getIndex());

            // Get and modify the commands with the appropriate replacements
            auto modifiedCmds = getSourceReplacement(
                commands, keyName, ult::stoi(indexStr), packagePath);

            // Placeholder replacement for value and index
            // const std::string valuePlaceholder = "{value}";
            // const std::string indexPlaceholder = "{index}";
            // const size_t valuePlaceholderLength = valuePlaceholder.length();
            // const size_t indexPlaceholderLength = indexPlaceholder.length();

            size_t pos;
            for (auto &cmd : modifiedCmds) {
              for (auto &arg : cmd) {
                pos = 0;
                while ((pos = arg.find(valuePlaceholder, pos)) !=
                       std::string::npos) {
                  arg.replace(pos, valuePlaceholderLength, valueStr);
                  pos += valueStr.length();
                }
                pos = 0;
                while ((pos = arg.find(indexPlaceholder, pos)) !=
                       std::string::npos) {
                  arg.replace(pos, indexPlaceholderLength, indexStr);
                  pos += indexStr.length();
                }
              }
            }

            // Apply placeholder replacements and switch to ScriptOverlay
            applyPlaceholderReplacementsToCommands(modifiedCmds, packagePath);

            tsl::shiftItemFocus(stepTrackBar);

            tsl::changeTo<ScriptOverlay>(std::move(modifiedCmds), packagePath,
                                         keyName,
                                         isFromMainMenu ? "main" : "package",
                                         false, lastPackageHeader, showWidget);
          });

          // Add the StepTrackBarV2 to the list
          list->addItem(stepTrackBar);

          continue;
        } else if (commandMode == NAMED_STEP_TRACKBAR_STR) {
          // entryList.clear();
          // entryList.shrink_to_fit();
          std::vector<std::string> entryList = {};

          _inEristaSection = false;
          _inMarikoSection = false;

          // std::string commandName;
          for (auto it = commands.begin(); it != commands.end();
               /* no increment here */) {
            auto &cmd = *it;
            if (cmd.empty()) {
              it = commands.erase(it);
              continue;
            }

            commandName = cmd[0];

            if (commandName == "erista:") {
              _inEristaSection = true;
              _inMarikoSection = false;
              it = commands.erase(it);
              continue;
            } else if (commandName == "mariko:") {
              _inEristaSection = false;
              _inMarikoSection = true;
              it = commands.erase(it);
              continue;
            }

            if ((_inEristaSection && usingMariko) ||
                (_inMarikoSection && usingErista)) {
              it = commands.erase(it);
              continue;
            }

            if (cmd.size() > 1) {
              if (cmd[0] == "list_source") {
                std::string listString = cmd[1];
                removeQuotes(listString);
                entryList = stringToList(listString);
                break;
              } else if (cmd[0] == "list_file_source") {
                std::string listPath = cmd[1];
                preprocessPath(listPath, packagePath);
                entryList = readListFromFile(listPath);
                break;
              } else if (cmd[0] == "ini_file_source") {
                std::string iniPath = cmd[1];
                preprocessPath(iniPath, packagePath);
                entryList = parseSectionsFromIni(iniPath);
                break;
              }
            }

            if (cmd.size() > 2) {
              if (cmd[0] == "json_source") {
                std::string jsonString = cmd[1];
                removeQuotes(jsonString);
                std::string jsonKey = cmd[2];
                removeQuotes(jsonKey);
                populateSelectedItemsListFromJson(JSON_STR, jsonString, jsonKey,
                                                  entryList);
                break;
              } else if (cmd[0] == "json_file_source") {
                std::string jsonPath = cmd[1];
                preprocessPath(jsonPath, packagePath);
                std::string jsonKey = cmd[2];
                removeQuotes(jsonKey);
                populateSelectedItemsListFromJson(JSON_FILE_STR, jsonPath,
                                                  jsonKey, entryList);
                break;
              }
            }

            ++it;
          }
          onlyTables = false;

          // Create NamedStepTrackBarV2 instance and configure it
          auto namedStepTrackBar = new tsl::elm::NamedStepTrackBarV2(
              optionName, packagePath, entryList, interpretAndExecuteCommands,
              getSourceReplacement, commands, originalOptionName,
              unlockedTrackbar, onEveryTick);

          // Set the SCRIPT_KEY listener
          namedStepTrackBar->setScriptKeyListener(
              [commands, keyName = originalOptionName, packagePath,
               entryList = entryList, lastPackageHeader, showWidget,
               namedStepTrackBar]() {
                const bool isFromMainMenu = (packagePath == PACKAGE_PATH);

                // Parse the value and index from the INI file
                // std::string valueStr = parseValueFromIniSection(packagePath +
                // "config.ini", keyName, "value"); std::string indexStr =
                // parseValueFromIniSection(packagePath + "config.ini", keyName,
                // "index");

                // Use namedStepTrackBar's CURRENT state
                const std::string indexStr =
                    ult::to_string(namedStepTrackBar->getIndex());
                const size_t entryIndex =
                    std::min(static_cast<size_t>(namedStepTrackBar->getIndex()),
                             entryList.size() - 1);
                const std::string valueStr = entryList[entryIndex];

                // Get and modify the commands with the appropriate replacements
                auto modifiedCmds = getSourceReplacement(
                    commands, keyName, entryIndex, packagePath);

                // Placeholder replacement for value and index
                // const std::string valuePlaceholder = "{value}";
                // const std::string indexPlaceholder = "{index}";
                // const size_t valuePlaceholderLength =
                // valuePlaceholder.length(); const size_t
                // indexPlaceholderLength = indexPlaceholder.length();

                size_t pos;

                for (auto &cmd : modifiedCmds) {
                  for (auto &arg : cmd) {
                    pos = 0;
                    while ((pos = arg.find(valuePlaceholder, pos)) !=
                           std::string::npos) {
                      arg.replace(pos, valuePlaceholderLength, valueStr);
                      pos += valueStr.length();
                    }
                    pos = 0;
                    while ((pos = arg.find(indexPlaceholder, pos)) !=
                           std::string::npos) {
                      arg.replace(pos, indexPlaceholderLength, indexStr);
                      pos += indexStr.length();
                    }
                  }
                }

                // Apply placeholder replacements and switch to ScriptOverlay
                applyPlaceholderReplacementsToCommands(modifiedCmds,
                                                       packagePath);

                tsl::shiftItemFocus(namedStepTrackBar);

                tsl::changeTo<ScriptOverlay>(
                    std::move(modifiedCmds), packagePath, keyName,
                    isFromMainMenu ? "main" : "package", false,
                    lastPackageHeader, showWidget);
              });
          entryList.clear();

          // Add the NamedStepTrackBarV2 to the list
          list->addItem(namedStepTrackBar);

          continue;
        }
        if (useSelection) { // For wildcard commands (dropdown menus)
          tsl::elm::ListItem *listItem;
          if ((footer == DROPDOWN_SYMBOL) || (footer.empty()) ||
              footer == commandFooter) {
            cleanOptionName = optionName;
            // removeTag(cleanOptionName);
            listItem = new tsl::elm::ListItem(cleanOptionName, footer, isMini);
            listItem->enableShortHoldKey();
            listItem->m_shortHoldKey = SCRIPT_KEY;
          } else {
            cleanOptionName = optionName;
            // removeTag(cleanOptionName);
            listItem = new tsl::elm::ListItem(cleanOptionName, "", isMini);
            listItem->enableShortHoldKey();
            listItem->m_shortHoldKey = SCRIPT_KEY;

            if (commandMode == OPTION_STR)
              listItem->setValue(footer, commandFooterHighlightDefined
                                             ? !commandFooterHighlight
                                             : false);
            else
              listItem->setValue(footer, commandFooterHighlightDefined
                                             ? !commandFooterHighlight
                                             : true);
          }

          if (footer == UNAVAILABLE_SELECTION || footer == NOT_AVAILABLE_STR ||
              (footer.find(NULL_STR) != std::string::npos))
            listItem->setValue(UNAVAILABLE_SELECTION, true);
          if (commandMode == FORWARDER_STR) {

            const std::string &forwarderPackagePath =
                getParentDirFromPath(packageSource);
            const std::string &forwarderPackageIniName =
                getNameFromPath(packageSource);
            listItem->setClickListener([commands, keyName = originalOptionName,
                                        dropdownSection, packagePath,
                                        currentPage, packageName, nestedLayer,
                                        cleanOptionName, listItem,
                                        forwarderPackagePath,
                                        forwarderPackageIniName,
                                        lastPackageHeader, pageHeader,
                                        showWidget, i](s64 keys) mutable {
              if (simulatedMenu.load(std::memory_order_acquire)) {
                keys |= SYSTEM_SETTINGS_KEY;
              }

              if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
                interpretAndExecuteCommands(
                    std::move(getSourceReplacement(commands, keyName, i,
                                                   packagePath)),
                    packagePath, keyName);
                resetPercentages();

                nestedMenuCount++;
                // lastPackagePath = forwarderPackagePath;
                // lastPackageName = forwarderPackageIniName;
                if (dropdownSection.empty())
                  lastPackageMenu = "packageMenu";
                else
                  lastPackageMenu = "subPackageMenu";

                // set forwarder pointer for updating
                // forwarderListItem = listItem;
                // lastCommandMode = FORWARDER_STR;
                // lastKeyName = keyName;

                // Fix: Don't pass section headers as pageHeader when returning
                // to main package
                // std::string returnPageHeader = "";
                // if (nestedLayer > 0) {
                //    // Only preserve pageHeader for nested contexts, not main
                //    package returnPageHeader = lastPackageHeader;
                //}

                returnContextStack.push({
                    .packagePath = packagePath,
                    .sectionName = dropdownSection,
                    .currentPage = currentPage,
                    .packageName = packageName,
                    .pageHeader = pageHeader, // Move this before nestedLayer
                    .option = cleanOptionName,
                    .nestedLayer = nestedLayer // Move this to the end
                });

                allowSlide.exchange(false, std::memory_order_acq_rel);
                unlockedSlide.exchange(false, std::memory_order_acq_rel);

                // tsl::clearGlyphCacheNow.store(true, release);
                tsl::swapTo<PackageMenu>(forwarderPackagePath, "", LEFT_STR,
                                         forwarderPackageIniName,
                                         nestedMenuCount, lastPackageHeader);

                return true;
              } else if (keys & SCRIPT_KEY &&
                         !(keys & ~SCRIPT_KEY & ALL_KEYS_MASK)) {
                const bool isFromMainMenu = (packagePath == PACKAGE_PATH);
                // if (inPackageMenu) {
                //     inPackageMenu = false;
                //     lastMenu = "packageMenu";
                // }
                // if (inSubPackageMenu) {
                //     inSubPackageMenu = false;
                //     lastMenu = "subPackageMenu";
                // }

                // auto modifiedCmds = getSourceReplacement(commands, keyName,
                // i, packagePath);

                std::string selectionItem = keyName;
                removeTag(selectionItem);

                tsl::shiftItemFocus(listItem);

                // add lines ;mode=forwarder and package_source
                // 'forwarderPackagePath' to front of modifiedCmds
                tsl::changeTo<ScriptOverlay>(
                    std::move(getSourceReplacement(commands, keyName, i,
                                                   packagePath)),
                    packagePath, selectionItem,
                    isFromMainMenu ? "main" : "package", true,
                    lastPackageHeader, showWidget);
                return true;
              } else if ((packagePath == PACKAGE_PATH) &&
                         (keys & SYSTEM_SETTINGS_KEY) &&
                         !(keys & ~SYSTEM_SETTINGS_KEY & ALL_KEYS_MASK)) {
                skipJumpReset.store(false, std::memory_order_release);
                returnJumpItemName = cleanOptionName;
                returnJumpItemValue = "";
                return true;
              }
              return false;
            });
            listItem->disableClickAnimation();
          } else {
            listItem->setClickListener([commands, keyName = originalOptionName,
                                        cleanOptionName, dropdownSection,
                                        packagePath, packageName, footer,
                                        lastSection, listItem,
                                        lastPackageHeader, commandMode,
                                        showWidget, i](uint64_t keys) {
              // listItemPtr = std::shared_ptr<tsl::elm::ListItem>(listItem,
              // [](auto*){})](uint64_t keys) {

              if (runningInterpreter.load(acquire))
                return false;

              if (simulatedMenu.load(std::memory_order_acquire)) {
                keys |= SYSTEM_SETTINGS_KEY;
              }

              if (((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK)))) {
                if (footer != UNAVAILABLE_SELECTION &&
                    footer != NOT_AVAILABLE_STR &&
                    (footer.find(NULL_STR) == std::string::npos)) {
                  if (inPackageMenu)
                    inPackageMenu = false;
                  if (inSubPackageMenu)
                    inSubPackageMenu = false;
                  if (dropdownSection.empty())
                    lastPackageMenu = "packageMenu";
                  else
                    lastPackageMenu = "subPackageMenu";

                  selectedListItem = listItem;

                  std::string newKey = "";
                  if (inPackageMenu) {
                    newKey = lastSection + keyName;
                    if (selectedFooterDict.find(newKey) ==
                        selectedFooterDict.end())
                      selectedFooterDict[newKey] = footer;
                  } else {
                    newKey = "sub_" + lastSection + keyName;
                    if (selectedFooterDict.find(newKey) ==
                        selectedFooterDict.end())
                      selectedFooterDict[newKey] = footer;
                  }

                  if (commandMode == OPTION_STR || commandMode == SLOT_STR) {
                    // std::lock_guard<std::mutex> lock(jumpItemMutex);
                    jumpItemName = "";
                    jumpItemValue = CHECKMARK_SYMBOL;
                    jumpItemExactMatch.store(true, release);
                    // g_overlayFilename = "";
                  } else {
                    // std::lock_guard<std::mutex> lock(jumpItemMutex);
                    jumpItemName = "";
                    jumpItemValue = "";
                    jumpItemExactMatch.store(true, release);
                    // g_overlayFilename = "";
                  }

                  tsl::shiftItemFocus(listItem);

                  tsl::changeTo<SelectionOverlay>(packagePath, keyName, newKey,
                                                  lastPackageHeader, commands,
                                                  showWidget);
                  // lastKeyName = keyName;
                }
                return true;
              } else if (keys & SCRIPT_KEY &&
                         !(keys & ~SCRIPT_KEY & ALL_KEYS_MASK)) {
                const bool isFromMainMenu = (packagePath == PACKAGE_PATH);
                // if (inMainMenu) {
                //     isFromMainMenu = true;
                //     inMainMenu.store(false, std::memory_order_release);
                // }
                // if (inPackageMenu) {
                //     inPackageMenu = false;
                //     lastMenu = "packageMenu";
                // }
                // if (inSubPackageMenu) {
                //     inSubPackageMenu = false;
                //     lastMenu = "subPackageMenu";
                // }

                std::string selectionItem = keyName;
                removeTag(selectionItem);
                auto modifiedCmds = commands;
                applyPlaceholderReplacementsToCommands(modifiedCmds,
                                                       packagePath);
                tsl::shiftItemFocus(listItem);
                tsl::changeTo<ScriptOverlay>(
                    std::move(modifiedCmds), packagePath, selectionItem,
                    isFromMainMenu ? "main" : "package", true,
                    lastPackageHeader, showWidget);
                return true;
              } else if ((packagePath == PACKAGE_PATH) &&
                         (keys & SYSTEM_SETTINGS_KEY) &&
                         !(keys & ~SYSTEM_SETTINGS_KEY & ALL_KEYS_MASK)) {
                skipJumpReset.store(false, std::memory_order_release);
                returnJumpItemName = cleanOptionName;
                returnJumpItemValue = "";
                return true;
              }
              return false;
            });
          }
          onlyTables = false;

          list->addItem(listItem);
        } else { // For everything else

          const std::string &selectedItem = optionName;

          // For entries that are paths
          itemName = getNameFromPath(selectedItem);
          std::string tmpSelectedItem = selectedItem;
          preprocessPath(tmpSelectedItem, packagePath);
          if (!isDirectory(tmpSelectedItem))
            dropExtension(itemName);
          parentDirName = getParentDirNameFromPath(selectedItem);
          if (commandMode == DEFAULT_STR || commandMode == SLOT_STR ||
              commandMode == OPTION_STR) { // for handiling toggles
            cleanOptionName = optionName;
            // removeTag(cleanOptionName);
            tsl::elm::ListItem *listItem =
                new tsl::elm::ListItem(cleanOptionName, "", isMini);
            listItem->enableShortHoldKey();
            listItem->m_shortHoldKey = SCRIPT_KEY;

            if (commandMode == DEFAULT_STR)
              listItem->setValue(footer, commandFooterHighlightDefined
                                             ? !commandFooterHighlight
                                             : true);
            else
              listItem->setValue(footer, commandFooterHighlightDefined
                                             ? !commandFooterHighlight
                                             : false);

            if (isHold) {
              listItem->disableClickAnimation();
              listItem->enableTouchHolding();
            }

            listItem->setClickListener(
                [i, commands, keyName = originalOptionName, cleanOptionName,
                 packagePath, packageName, selectedItem, listItem,
                 lastPackageHeader, commandMode, footer, isHold, showWidget,
                 commandFooterHighlight,
                 commandFooterHighlightDefined](uint64_t keys) {
                  if (runningInterpreter.load(acquire)) {
                    return false;
                  }

                  if (simulatedMenu.load(std::memory_order_acquire)) {
                    keys |= SYSTEM_SETTINGS_KEY;
                  }

                  if (((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK)))) {
                    isDownloadCommand.store(false, release);
                    runningInterpreter.store(true, release);

                    auto modifiedCmds = getSourceReplacement(
                        commands, selectedItem, i, packagePath);
                    if (isHold) {
                      lastSelectedListItemFooter = footer;
                      lastFooterHighlight =
                          commandFooterHighlight; // STORE THE VALUE
                      lastFooterHighlightDefined =
                          commandFooterHighlightDefined; // STORE WHETHER IT WAS
                                                         // DEFINED
                      listItem->setValue(INPROGRESS_SYMBOL);
                      lastSelectedListItem = listItem;
                      // Use touch hold start tick if available, otherwise use
                      // current tick
                      // if (listItem->isTouchHolding()) {
                      //    holdStartTick = listItem->getTouchHoldStartTick();
                      //} else {
                      //    holdStartTick = armGetSystemTick();
                      //}
                      holdStartTick = armGetSystemTick();
                      storedCommands = std::move(modifiedCmds);
                      lastCommandMode = commandMode;
                      lastCommandIsHold = true;
                      lastKeyName = keyName;
                      return true;
                    }

                    // logMessage("selectedItem: "+selectedItem+" keyName:
                    // "+keyName);
                    executeInterpreterCommands(std::move(modifiedCmds),
                                               packagePath, keyName);
                    // startInterpreterThread(packagePath);
                    // listItem->disableClickAnimation();
                    listItem->setValue(INPROGRESS_SYMBOL);

                    // lastSelectedListItem = nullptr;
                    lastSelectedListItem = listItem;
                    lastFooterHighlight =
                        commandFooterHighlight; // STORE THE VALUE
                    lastFooterHighlightDefined =
                        commandFooterHighlightDefined; // STORE WHETHER IT WAS
                                                       // DEFINED
                    tsl::shiftItemFocus(listItem);
                    lastCommandMode = commandMode;
                    lastKeyName = keyName;

                    lastRunningInterpreter.store(true,
                                                 std::memory_order_release);
                    if (lastSelectedListItem)
                      lastSelectedListItem->triggerClickAnimation();
                    return true;
                  } else if (keys & SCRIPT_KEY &&
                             !(keys & ~SCRIPT_KEY & ALL_KEYS_MASK)) {
                    const bool isFromMainMenu = (packagePath == PACKAGE_PATH);
                    auto modifiedCmds = getSourceReplacement(
                        commands, selectedItem, i, packagePath);
                    applyPlaceholderReplacementsToCommands(modifiedCmds,
                                                           packagePath);
                    tsl::shiftItemFocus(listItem);
                    tsl::changeTo<ScriptOverlay>(
                        std::move(modifiedCmds), packagePath, keyName,
                        isFromMainMenu ? "main" : "package", false,
                        lastPackageHeader, showWidget);
                    return true;
                  } else if ((packagePath == PACKAGE_PATH) &&
                             (keys & SYSTEM_SETTINGS_KEY) &&
                             !(keys & ~SYSTEM_SETTINGS_KEY & ALL_KEYS_MASK)) {
                    skipJumpReset.store(false, std::memory_order_release);
                    returnJumpItemName = cleanOptionName;
                    returnJumpItemValue = "";
                    return true;
                  }
                  return false;
                });
            onlyTables = false;
            // lastItemIsScrollableTable = false;
            list->addItem(listItem);
          } else if (commandMode == TOGGLE_STR) {
            cleanOptionName = optionName;
            // removeTag(cleanOptionName);
            auto *toggleListItem = new tsl::elm::ToggleListItem(
                cleanOptionName, false, ON, OFF, isMini, true);
            toggleListItem->enableShortHoldKey();
            toggleListItem->m_shortHoldKey = SCRIPT_KEY;

            // Set the initial state of the toggle item
            if (!pathPatternOn.empty()) {
              // preprocessPath(pathPatternOn, packagePath);
              toggleStateOn = isFileOrDirectory(pathPatternOn);
            } else {
              if ((footer != CAPITAL_ON_STR && footer != CAPITAL_OFF_STR) &&
                  !defaultToggleState.empty()) {
                if (defaultToggleState == ON_STR)
                  footer = CAPITAL_ON_STR;
                else if (defaultToggleState == OFF_STR)
                  footer = CAPITAL_OFF_STR;
              }

              toggleStateOn = (footer == CAPITAL_ON_STR);
            }

            toggleListItem->setState(toggleStateOn);

            toggleListItem->setStateChangedListener(
                [i, usingProgress, toggleListItem, commandsOn, commandsOff,
                 keyName = originalOptionName, packagePath, pathPatternOn,
                 pathPatternOff](bool state) {
                  if (runningInterpreter.load(std::memory_order_acquire)) {
                    return;
                  }

                  tsl::Overlay::get()->getCurrentGui()->requestFocus(
                      toggleListItem, tsl::FocusDirection::None);

                  if (usingProgress)
                    toggleListItem->setValue(INPROGRESS_SYMBOL);
                  nextToggleState = !state ? CAPITAL_OFF_STR : CAPITAL_ON_STR;
                  lastKeyName = keyName;
                  runningInterpreter.store(true, release);
                  lastRunningInterpreter.store(true, release);
                  lastSelectedListItem = toggleListItem;

                  // Now pass the preprocessed paths to getSourceReplacement
                  // interpretAndExecuteCommands(std::move(state ?
                  // getSourceReplacement(commandsOn, pathPatternOn, i,
                  // packagePath) :
                  //    getSourceReplacement(commandsOff, pathPatternOff, i,
                  //    packagePath)), packagePath, keyName);

                  executeInterpreterCommands(
                      std::move(state ? getSourceReplacement(commandsOn,
                                                             pathPatternOn, i,
                                                             packagePath)
                                      : getSourceReplacement(commandsOff,
                                                             pathPatternOff, i,
                                                             packagePath)),
                      packagePath, keyName);
                  // resetPercentages();
                  //  Set the ini file value after executing the command
                  // setIniFileValue((packagePath + CONFIG_FILENAME), keyName,
                  // FOOTER_STR, state ? CAPITAL_ON_STR : CAPITAL_OFF_STR);
                });

            // Set the script key listener (for SCRIPT_KEY)
            toggleListItem->setScriptKeyListener(
                [toggleListItem, i, commandsOn, commandsOff,
                 keyName = originalOptionName, packagePath, pathPatternOn,
                 pathPatternOff, lastPackageHeader, showWidget](bool state) {
                  const bool isFromMainMenu = (packagePath == PACKAGE_PATH);
                  // if (inPackageMenu)
                  //     inPackageMenu = false;
                  // if (inSubPackageMenu)
                  //     inSubPackageMenu = false;

                  // Custom logic for SCRIPT_KEY handling
                  auto modifiedCmds =
                      state ? getSourceReplacement(commandsOn, pathPatternOn, i,
                                                   packagePath)
                            : getSourceReplacement(commandsOff, pathPatternOff,
                                                   i, packagePath);

                  applyPlaceholderReplacementsToCommands(modifiedCmds,
                                                         packagePath);

                  tsl::shiftItemFocus(toggleListItem);
                  tsl::changeTo<ScriptOverlay>(
                      std::move(modifiedCmds), packagePath, keyName,
                      isFromMainMenu ? "main" : "package", false,
                      lastPackageHeader, showWidget);
                });

            onlyTables = false;
            // lastItemIsScrollableTable = false;
            list->addItem(toggleListItem);
          }
        }
      }
    }
  }

  options.clear();
  optionName.clear();
  commands.clear();
  commandsOn.clear();
  commandsOff.clear();
  tableData.clear();

  if (onlyTables) {
    // auto dummyItem = new tsl::elm::DummyListItem();
    // list->addItem(dummyItem, 0, 1);
    addDummyListItem(list, 1); // assuming a header is always above
                               // addDummyListItem(list);
    // list->disableCaching(); // causes a visual glitch when entered fresh on
    // return, may look into again later.
  }

  return onlyTables;
}

/**
 * @brief The `PackageMenu` class handles sub-menu overlay functionality.
 *
 * This class manages sub-menu overlays, allowing users to interact with
 * specific menu options. It provides functions for creating, updating, and
 * navigating sub-menus, as well as handling user interactions related to
 * sub-menu items.
 */
class PackageMenu : public tsl::Gui {
private:
  std::string packagePath, dropdownSection, currentPage, pathReplace,
      pathReplaceOn, pathReplaceOff;
  std::string filePath, specificKey, pathPattern, pathPatternOn, pathPatternOff,
      itemName, parentDirName, lastParentDirName;
  bool usingPages = false;
  std::string packageName;
  size_t nestedLayer = 0;
  std::string pageHeader;

  std::string packageIniPath;
  std::string packageConfigIniPath;
  // bool menuIsGenerated = false;

public:
  /**
   * @brief Constructs a `PackageMenu` instance for a specific sub-menu path.
   *
   * Initializes a new instance of the `PackageMenu` class for the given
   * sub-menu path.
   *
   * @param path The path to the sub-menu.
   */
  PackageMenu(const std::string &path, const std::string &sectionName = "",
              const std::string &page = LEFT_STR,
              const std::string &_packageName = PACKAGE_FILENAME,
              const size_t _nestedlayer = 0,
              const std::string &_pageHeader = "")
      : packagePath(path), dropdownSection(sectionName), currentPage(page),
        packageName(_packageName), nestedLayer(_nestedlayer),
        pageHeader(_pageHeader) {
    std::lock_guard<std::mutex> lock(transitionMutex);
    // if (nestedLayer > 0)
    //     hexSumCache.clear();
    //{
    //     //std::lock_guard<std::mutex> lock(jumpItemMutex);
    //     if (!skipJumpReset.load(acquire)) {
    //         jumpItemName = "";
    //         jumpItemValue = "";
    //         jumpItemExactMatch.store(true, release);
    //         //g_overlayFilename = "";
    //     } else
    //         skipJumpReset.store(false, release);
    // }

    if (!skipJumpReset.load(acquire)) {
      jumpItemName = "";
      jumpItemValue = "";
      jumpItemExactMatch.store(true, release);
      // g_overlayFilename = "";
    } else
      skipJumpReset.store(false, release);
    // tsl::clearGlyphCacheNow.store(true, release);
    settingsInitialized.exchange(true, acq_rel);
  }

  /**
   * @brief Destroys the `PackageMenu` instance.
   *
   * Cleans up any resources associated with the `PackageMenu` instance.
   */
  ~PackageMenu() {
    std::lock_guard<std::mutex> lock(transitionMutex);

    if (returningToMain || returningToHiddenMain) {
      tsl::clearGlyphCacheNow.store(true, release);
      clearMemory();

      packageRootLayerTitle = packageRootLayerVersion = packageRootLayerColor =
          "";
      overrideTitle = false;
      overrideVersion = false;

      if (isFile(packagePath + EXIT_PACKAGE_FILENAME)) {
        const bool useExitPackage =
            !(parseValueFromIniSection(PACKAGES_INI_FILEPATH,
                                       getNameFromPath(packagePath),
                                       USE_EXIT_PACKAGE_STR) == FALSE_STR);

        if (useExitPackage) {
          // Load only the commands from the specific section (bootCommandName)
          auto exitCommands = loadSpecificSectionFromIni(
              packagePath + EXIT_PACKAGE_FILENAME, "exit");

          if (!exitCommands.empty()) {
            // bool resetCommandSuccess = false;
            // if (!commandSuccess.load(acquire)) resetCommandSuccess = true;
            const bool resetCommandSuccess =
                !commandSuccess.load(std::memory_order_acquire);

            interpretAndExecuteCommands(std::move(exitCommands), packagePath,
                                        "exit");
            resetPercentages();

            if (resetCommandSuccess) {
              commandSuccess.store(false, release);
            }
          }
        }
      }
      lastSelectedListItem = nullptr;

      //} else {
      //    hexSumCache.clear();
    }
  }

  /**
   * @brief Creates the graphical user interface (GUI) for the sub-menu overlay.
   *
   * This function initializes and sets up the GUI elements for the sub-menu
   * overlay, allowing users to interact with specific menu options.
   *
   * @return A pointer to the GUI element representing the sub-menu overlay.
   */
  virtual tsl::elm::Element *createUI() override {
    std::lock_guard<std::mutex> lock(transitionMutex);
    setFooterBackLabel(false);
    // menuIsGenerated = false;
    if (dropdownSection.empty()) {
      inPackageMenu = true;
      lastMenu = "packageMenu";
    } else {
      inSubPackageMenu = true;
      lastMenu = "subPackageMenu";
    }

    auto *list = new tsl::elm::List();

    packageIniPath = packagePath + packageName;
    packageConfigIniPath = packagePath + CONFIG_FILENAME;

    PackageHeader packageHeader = getPackageHeaderFromIni(packageIniPath);

    const bool showWidget = (!packageHeader.show_widget.empty() &&
                             packageHeader.show_widget == TRUE_STR);

    std::string pageLeftName, pageRightName;
    bool noClickableItems = drawCommandsMenu(
        list, packageIniPath, packageConfigIniPath, packageHeader,
        this->pageHeader, pageLeftName, pageRightName, this->packagePath,
        this->currentPage, this->packageName, this->dropdownSection,
        this->nestedLayer, this->pathPattern, this->pathPatternOn,
        this->pathPatternOff, this->usingPages, true, showWidget);

    if (nestedLayer == 0) {

      if (!packageRootLayerTitle.empty())
        overrideTitle = true;
      if (!packageRootLayerVersion.empty())
        overrideVersion = true;

      if (!packageHeader.title.empty() && packageRootLayerTitle.empty())
        packageRootLayerTitle = packageHeader.title;
      if (!packageHeader.display_title.empty())
        packageRootLayerTitle = packageHeader.display_title;
      if (!packageHeader.version.empty() && packageRootLayerVersion.empty())
        packageRootLayerVersion = packageHeader.version;
      if (!packageHeader.color.empty() && packageRootLayerColor.empty())
        packageRootLayerColor = packageHeader.color;
    }
    if (packageHeader.title.empty() || overrideTitle)
      packageHeader.title = packageRootLayerTitle;
    if (!packageHeader.display_title.empty() || overrideTitle)
      packageHeader.display_title = packageRootLayerTitle;
    if (packageHeader.version.empty() || overrideVersion)
      packageHeader.version = packageRootLayerVersion;
    if (packageHeader.color.empty())
      packageHeader.color = packageRootLayerColor;

    auto *rootFrame = new tsl::elm::OverlayFrame(
        (!packageHeader.title.empty())
            ? packageHeader.title
            : (!packageRootLayerTitle.empty() ? packageRootLayerTitle
                                              : getNameFromPath(packagePath)),
        ((!pageHeader.empty() && packageHeader.show_version != TRUE_STR)
             ? pageHeader
             : (packageHeader.version != ""
                    ? (!packageRootLayerVersion.empty()
                           ? packageRootLayerVersion
                           : packageHeader.version) +
                          " " + DIVIDER_SYMBOL + " Ultrahand Package"
                    : "Ultrahand Package")),
        noClickableItems, "", packageHeader.color,
        (usingPages && currentPage == RIGHT_STR) ? pageLeftName : "",
        (usingPages && currentPage == LEFT_STR) ? pageRightName : "");

    list->jumpToItem(jumpItemName, jumpItemValue, jumpItemExactMatch);
    rootFrame->setContent(list);
    if (showWidget)
      rootFrame->m_showWidget = true;
    return rootFrame;
  }

  // void handleForwarderFooter() {
  //     //if (lastCommandMode == FORWARDER_STR && isFile(packageConfigIniPath))
  //     {
  //     //    auto packageConfigData =
  //     getParsedDataFromIniFile(packageConfigIniPath);
  //     //    auto it = packageConfigData.find(lastKeyName);
  //     //    if (it != packageConfigData.end()) {
  //     //        auto& optionSection = it->second;
  //     //        auto footerIt = optionSection.find(FOOTER_STR);
  //     //        if (footerIt != optionSection.end() &&
  //     (footerIt->second.find(NULL_STR) == std::string::npos)) {
  //     //            if (forwarderListItem)
  //     //                forwarderListItem->setValue(footerIt->second);
  //     //        }
  //     //    }
  //     //    lastCommandMode = "";
  //     //}
  // }

  /**
   * @brief Handles user input for the sub-menu overlay.
   *
   * Processes user input and responds accordingly within the sub-menu overlay.
   * Captures key presses and performs actions based on user interactions.
   *
   * @param keysDown A bitset representing keys that are currently pressed.
   * @param keysHeld A bitset representing keys that are held down.
   * @param touchInput Information about touchscreen input.
   * @param leftJoyStick Information about the left joystick input.
   * @param rightJoyStick Information about the right joystick input.
   * @return `true` if the input was handled within the overlay, `false`
   * otherwise.
   */
  virtual bool handleInput(uint64_t keysDown, uint64_t keysHeld,
                           touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {

    bool isHolding = (lastCommandIsHold &&
                      runningInterpreter.load(std::memory_order_acquire));
    if (isHolding) {
      processHold(
          keysDown, keysHeld, holdStartTick, isHolding,
          [&]() {
            // Execute interpreter commands if needed
            displayPercentage.store(-1, std::memory_order_release);
            // lastCommandMode.clear();
            lastCommandIsHold = false;
            // lastKeyName.clear();
            lastSelectedListItem->setValue(INPROGRESS_SYMBOL);
            // lastSelectedListItemFooter.clear();
            // lastSelectedListItem->enableClickAnimation();
            // lastSelectedListItem->triggerClickAnimation();
            // lastSelectedListItem->disableClickAnimation();
            triggerEnterFeedback();
            executeInterpreterCommands(std::move(storedCommands), packagePath,
                                       lastKeyName);
            lastRunningInterpreter.store(true, std::memory_order_release);
          },
          nullptr, true); // true = reset storedCommands
      return true;
    }

    const bool isRunningInterp = runningInterpreter.load(acquire);
    const bool isTouching = stillTouching.load(acquire);

    if (isRunningInterp) {
      return handleRunningInterpreter(keysDown, keysHeld);
    }

    if (lastRunningInterpreter.exchange(false, std::memory_order_acq_rel)) {
      // tsl::clearGlyphCacheNow.store(true, release);
      isDownloadCommand.store(false, release);

      if (lastSelectedListItem) {
        const bool success = commandSuccess.load(acquire);

        if (lastCommandMode == OPTION_STR || lastCommandMode == SLOT_STR) {
          if (success) {
            if (isFile(packageConfigIniPath)) {
              auto packageConfigData =
                  getParsedDataFromIniFile(packageConfigIniPath);

              if (auto it = packageConfigData.find(lastKeyName);
                  it != packageConfigData.end()) {
                auto &optionSection = it->second;

                // Update footer if valid
                if (auto footerIt = optionSection.find(FOOTER_STR);
                    footerIt != optionSection.end() &&
                    footerIt->second.find(NULL_STR) == std::string::npos) {
                  // Check if footer_highlight exists in the INI
                  auto footerHighlightIt =
                      optionSection.find("footer_highlight");
                  if (footerHighlightIt != optionSection.end()) {
                    // Use the value from the INI
                    bool footerHighlightFromIni =
                        (footerHighlightIt->second == TRUE_STR);
                    lastSelectedListItem->setValue(footerIt->second,
                                                   !footerHighlightFromIni);
                  } else {
                    // footer_highlight not defined in INI, use default
                    lastSelectedListItem->setValue(footerIt->second, false);
                  }
                }
              }

              lastCommandMode.clear();
            } else {
              lastSelectedListItem->setValue(CHECKMARK_SYMBOL);
            }
          } else {
            lastSelectedListItem->setValue(CROSSMARK_SYMBOL);
          }
        } else {
          // Handle toggle or checkmark logic
          if (nextToggleState.empty()) {
            lastSelectedListItem->setValue(success ? CHECKMARK_SYMBOL
                                                   : CROSSMARK_SYMBOL);
          } else {
            // Determine the final state to save
            const std::string finalState =
                success ? nextToggleState
                        : (nextToggleState == CAPITAL_ON_STR ? CAPITAL_OFF_STR
                                                             : CAPITAL_ON_STR);

            // Update displayed value
            lastSelectedListItem->setValue(finalState);

            // Update toggle state
            static_cast<tsl::elm::ToggleListItem *>(lastSelectedListItem)
                ->setState(finalState == CAPITAL_ON_STR);

            // Save to config file
            setIniFileValue(packageConfigIniPath, lastKeyName, FOOTER_STR,
                            finalState);

            lastKeyName.clear();
            nextToggleState.clear();
          }
        }

        lastSelectedListItem->enableClickAnimation();
        lastSelectedListItem = nullptr;
        lastFooterHighlight = lastFooterHighlightDefined = false;
      }

      closeInterpreterThread();
      resetPercentages();

      if (!commandSuccess.load()) {
        triggerRumbleDoubleClick.store(true, std::memory_order_release);
      }

      if (!limitedMemory && useSoundEffects) {
        reloadSoundCacheNow.store(true, std::memory_order_release);
        // ult::Audio::initialize();
      }
      // lastRunningInterpreter.store(false, std::memory_order_release);
      return true;
    }

    if (ult::refreshWallpaperNow.exchange(false, std::memory_order_acq_rel)) {
      closeInterpreterThread();
      ult::reloadWallpaper();
      if (!limitedMemory && useSoundEffects) {
        reloadSoundCacheNow.store(true, std::memory_order_release);
        // ult::Audio::initialize();
      }
    }

    if (goBackAfter.exchange(false, std::memory_order_acq_rel)) {
      disableSound.store(true, std::memory_order_release);
      simulatedBack.store(true, std::memory_order_release);
      return true;
    }

    if (!returningToPackage && !isTouching) {
      if (refreshPage.exchange(false, std::memory_order_acq_rel)) {

        // Function to handle the transition and state resetting
        auto handleMenuTransition = [&] {
          // const std::string lastPackagePath = packagePath;
          // const std::string lastDropdownSection = dropdownSection;
          // const std::string lastPage = currentPage;
          // const std::string lastPackageName = packageName;
          // const size_t lastNestedLayer = nestedLayer;

          inSubPackageMenu = false;
          inPackageMenu = false;

          // selectedListItem = nullptr;
          // lastSelectedListItem = nullptr;
          // tsl::clearGlyphCacheNow.store(true, release);
          // tsl::goBack();
          tsl::swapTo<PackageMenu>(packagePath, dropdownSection, currentPage,
                                   packageName, nestedLayer, pageHeader);
        };

        if (inPackageMenu) {
          handleMenuTransition();
          inPackageMenu = true;
          return true;
        } else if (inSubPackageMenu) {
          handleMenuTransition();
          inSubPackageMenu = true;
          return true;
        }
      }
      if (refreshPackage.load(acquire)) {
        if (nestedMenuCount == nestedLayer) {
          // astPackagePath = packagePath;
          // lastPage = currentPage;
          // lastPackageName = PACKAGE_FILENAME;

          // tsl::goBack(nestedMenuCount+1);
          // nestedMenuCount = 0;

          tsl::swapTo<PackageMenu>(SwapDepth(nestedMenuCount + 1), packagePath,
                                   "");
          nestedMenuCount = 0;
          inPackageMenu = true;
          inSubPackageMenu = false;
          refreshPackage.store(false, release);
          return true;
        }
      }
    }

    if (usingPages) {
      simulatedMenu.exchange(false, std::memory_order_acq_rel);

      bool wasSimulated = false;
      {
        // std::lock_guard<std::mutex> lock(ult::simulatedNextPageMutex);
        if (simulatedNextPage.exchange(false, std::memory_order_acq_rel)) {
          if (currentPage == LEFT_STR) {
            keysDown |= KEY_DRIGHT;
          } else if (currentPage == RIGHT_STR) {
            keysDown |= KEY_DLEFT;
          }
          wasSimulated = true;
        }
      }

      // Cache slide-related values

      const bool onTrack = onTrackBar.load(acquire);
      const bool slideAllowed = allowSlide.load(acquire);
      const bool slideUnlocked = unlockedSlide.load(acquire);
      const bool slideCondition =
          ((!slideAllowed && onTrack && !slideUnlocked) ||
           (onTrack && keysHeld & KEY_R)) ||
          !onTrack;

      // Helper lambda for slide transitions
      auto resetSlideState = [&]() {
        // if (allowSlide.load(acquire))
        allowSlide.store(false, release);
        // if (unlockedSlide.load(acquire))
        unlockedSlide.store(false, release);
      };

      if (currentPage == LEFT_STR) {
        if (!isTouching && slideCondition && (keysDown & KEY_RIGHT) &&
            (!onTrack ? !(keysHeld & ~KEY_RIGHT & ALL_KEYS_MASK)
                      : !(keysHeld & ~KEY_R & ~KEY_RIGHT & ALL_KEYS_MASK))) {
          {
            std::lock_guard<std::mutex> lock(tsl::elm::s_safeToSwapMutex);
            if (tsl::elm::s_safeToSwap.load(acquire)) {
              // lastPage = RIGHT_STR;
              tsl::swapTo<PackageMenu>(packagePath, dropdownSection, RIGHT_STR,
                                       packageName, nestedLayer, pageHeader);
              resetSlideState();
              // triggerRumbleClick.store(true, std::memory_order_release);
              // triggerNavigationSound.store(true, std::memory_order_release);
              if (!wasSimulated)
                triggerNavigationFeedback();
              else
                triggerRumbleClick.store(true, release);
            }
            return true;
          }
        }
      } else if (currentPage == RIGHT_STR) {
        if (!isTouching && slideCondition && (keysDown & KEY_LEFT) &&
            (!onTrack ? !(keysHeld & ~KEY_LEFT & ALL_KEYS_MASK)
                      : !(keysHeld & ~KEY_R & ~KEY_LEFT & ALL_KEYS_MASK))) {
          {
            std::lock_guard<std::mutex> lock(tsl::elm::s_safeToSwapMutex);
            if (tsl::elm::s_safeToSwap.load(acquire)) {
              // lastPage = LEFT_STR;
              tsl::swapTo<PackageMenu>(packagePath, dropdownSection, LEFT_STR,
                                       packageName, nestedLayer, pageHeader);
              resetSlideState();
              // triggerRumbleClick.store(true, std::memory_order_release);
              // triggerNavigationSound.store(true, std::memory_order_release);
              if (!wasSimulated)
                triggerNavigationFeedback();
              else
                triggerRumbleClick.store(true, release);
            }
            return true;
          }
        }
      }
    }

    // Common back key condition
    const bool backKeyPressed =
        !isTouching &&
        (((keysDown & KEY_B) && !(keysHeld & ~KEY_B & ALL_KEYS_MASK)));

    // Helper lambda for common back key handling logic
    auto handleBackKeyCommon = [&]() {
      // handleForwarderFooter();
      allowSlide.exchange(false, std::memory_order_acq_rel);
      unlockedSlide.exchange(false, std::memory_order_acq_rel);

      // Check if we have return context items to process first
      if (!returnContextStack.empty()) {
        // Don't set returning flags yet - let the return context handle
        // navigation
        return false; // Let tryReturnContext() handle this
      }

      if (nestedMenuCount == 0) {
        inPackageMenu = false;
        if (!inHiddenMode.load(std::memory_order_acquire))
          returningToMain = true;
        else
          returningToHiddenMain = true;

        if (!selectedPackage.empty()) {
          while (!returnContextStack.empty()) {
            returnContextStack.pop();
          }
          ult::launchingOverlay.store(true, std::memory_order_release);
          tsl::setNextOverlay(OVERLAY_PATH + "ovlmenu.ovl");
          exitingUltrahand.store(true, release);
          tsl::Overlay::get()->close();
          return true;
        }
      }
      if (nestedMenuCount > 0) {
        nestedMenuCount--;
        if (lastPackageMenu == "subPackageMenu") {
          returningToSubPackage = true;
        } else {
          returningToPackage = true;
        }
      }
      return false;
    };

    // Helper lambda for main menu return handling
    auto handleMainMenuReturn = [&]() {
      if (returningToMain || returningToHiddenMain) {
        if (returningToHiddenMain) {
          setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
                          IN_HIDDEN_PACKAGE_STR, TRUE_STR);
        }
        //{
        //    //std::lock_guard<std::mutex> lock(jumpItemMutex);
        //    jumpItemName = (packageRootLayerIsStarred ? STAR_SYMBOL + "  " +
        //    packageRootLayerTitle : packageRootLayerTitle) + "?" +
        //    packageRootLayerName; jumpItemValue = hidePackageVersions ? "" :
        //    packageRootLayerVersion; jumpItemExactMatch.store(true, release);
        //    //g_overlayFilename = "";
        //    skipJumpReset.store(true, release);
        //}
        jumpItemName.clear();
        jumpItemValue.clear();

        // tsl::clearGlyphCacheNow.store(true, release);
        // tsl::pop();

        // if (returningToMain) {
        //     tsl::changeTo<MainMenu>(PACKAGES_STR);
        // } else {
        //     tsl::changeTo<MainMenu>();
        // }
        SwapToMainMenu();
      } else {
        // tsl::clearGlyphCacheNow.store(true, release);
        tsl::goBack();
      }
    };

    // Helper lambda for return context handling
    auto tryReturnContext = [&]() -> bool {
      if (!returnContextStack.empty()) {
        ReturnContext returnTo = returnContextStack.top();
        returnContextStack.pop();

        // Decrement nestedMenuCount when returning via context
        if (nestedMenuCount > 0)
          nestedMenuCount--;

        jumpItemName = returnTo.option;
        jumpItemValue = "";
        jumpItemExactMatch = false;
        skipJumpReset.store(true, std::memory_order_release);

        // Set appropriate states for return
        inSubPackageMenu = false;
        inPackageMenu = false;
        returningToPackage = true;
        lastMenu = "packageMenu";

        tsl::swapTo<PackageMenu>(SwapDepth(2),
                                 returnTo.packagePath, // Where we came FROM
                                 returnTo.sectionName, // Source section
                                 returnTo.currentPage, // Source page
                                 returnTo.packageName, // Source package
                                 returnTo.nestedLayer, // Source nesting level
                                 returnTo.pageHeader   // Source header
        );
        return true;
      }
      return false;
    };

    // Helper lambda for normal back navigation
    auto handleNormalBack = [&]() -> bool {
      if (handleBackKeyCommon())
        return true;

      // Try return context first (for forwarders)
      if (tryReturnContext())
        return true;

      // Normal back navigation
      if (nestedMenuCount == 0) {
        handleMainMenuReturn();
      } else {
        tsl::goBack();
      }
      return true;
    };

    // Handle main package menu (dropdownSection is empty)
    if (!returningToPackage && inPackageMenu &&
        nestedMenuCount == nestedLayer) {
      simulatedNextPage.exchange(false, std::memory_order_acq_rel);
      simulatedMenu.exchange(false, std::memory_order_acq_rel);

      if (!usingPages || (usingPages && currentPage == LEFT_STR)) {
        if (backKeyPressed) {
          return handleNormalBack();
        }
      } else if (usingPages && currentPage == RIGHT_STR) {
        if (backKeyPressed) {
          // lastPage = LEFT_STR;
          return handleNormalBack();
        }
      }
    }

    // Handle sub-package menu (dropdownSection is not empty)
    if (!returningToSubPackage && inSubPackageMenu) {
      simulatedNextPage.exchange(false, std::memory_order_acq_rel);
      simulatedMenu.exchange(false, std::memory_order_acq_rel);

      if (!usingPages || (usingPages && currentPage == LEFT_STR)) {
        if (backKeyPressed) {
          // handleForwarderFooter();
          allowSlide.exchange(false, std::memory_order_acq_rel);
          unlockedSlide.exchange(false, std::memory_order_acq_rel);

          // Try return context first, fallback to normal navigation
          if (tryReturnContext())
            return true;

          inSubPackageMenu = false;
          returningToPackage = true;
          lastMenu = "packageMenu";
          tsl::goBack();
          return true;
        }
      } else if (usingPages && currentPage == RIGHT_STR) {
        if (backKeyPressed) {
          // handleForwarderFooter();
          allowSlide.exchange(false, std::memory_order_acq_rel);
          unlockedSlide.exchange(false, std::memory_order_acq_rel);

          // Try return context first, fallback to normal navigation
          if (tryReturnContext())
            return true;

          inSubPackageMenu = false;
          returningToPackage = true;
          lastMenu = "packageMenu";
          tsl::goBack();
          return true;
        }
      }
    }

    if (returningToPackage && !returningToSubPackage && !(keysDown & KEY_B)) {
      lastPackageMenu = "";
      returningToPackage = false;
      returningToSubPackage = false;
      inPackageMenu = true;
      inSubPackageMenu = false;
      if (nestedMenuCount == 0 && nestedLayer == 0) {
        // lastPackagePath = packagePath;
        // lastPackageName = PACKAGE_FILENAME;
      }
    }

    if (returningToSubPackage && !(keysDown & KEY_B)) {
      lastPackageMenu = "";
      returningToPackage = false;
      returningToSubPackage = false;
      inPackageMenu = false;
      inSubPackageMenu = true;
      if (nestedMenuCount == 0 && nestedLayer == 0) {
        // lastPackagePath = packagePath;
        // lastPackageName = PACKAGE_FILENAME;
      }
    }

    if (triggerExit.exchange(false, std::memory_order_acq_rel)) {
      while (!returnContextStack.empty()) {
        returnContextStack.pop();
      }
      ult::launchingOverlay.store(true, std::memory_order_release);
      tsl::setNextOverlay(OVERLAY_PATH + "ovlmenu.ovl");
      tsl::Overlay::get()->close();
    }

    // Fallback for lost navigations
    if (backKeyPressed) {
      if (!selectedPackage.empty()) {
        ult::launchingOverlay.store(true, std::memory_order_release);
        exitingUltrahand.store(true, release);
        tsl::setNextOverlay(OVERLAY_PATH + "ovlmenu.ovl");
        tsl::Overlay::get()->close();
        return true;
      }

      allowSlide.exchange(false, std::memory_order_acq_rel);
      unlockedSlide.exchange(false, std::memory_order_acq_rel);

      // Try return context first for any lost navigation scenarios
      if (tryReturnContext())
        return true;

      // Fallback to normal navigation
      inSubPackageMenu = false;
      returningToPackage = true;
      lastMenu = "packageMenu";
      tsl::goBack();
      return true;
    }

    return false;
  };
};

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

// --- Memory Warning Menu ---

// Global flag to track if memory was adjusted
static std::atomic<bool> memoryWasAdjusted{false};

class MemoryWarningMenu : public tsl::Gui {
private:
  u32 requiredMB;
  u32 currentMB;
  bool userIncreasedMemory = false;

public:
  MemoryWarningMenu(u32 required) 
      : requiredMB(required), 
        currentMB(bytesToMB(static_cast<u64>(currentHeapSize))) {}

  virtual tsl::elm::Element *createUI() override {
    auto *list = new tsl::elm::List();

    // Warning header
    addHeader(list, WARNING_SYMBOL + " " + INSUFFICIENT_MEMORY);

    // Explanation table
    std::vector<std::vector<std::string>> tableData = {
        {REQUIRED_MEMORY, "", std::to_string(requiredMB) + " MB"},
        {CURRENT_MEMORY, "", std::to_string(currentMB) + " MB"}
    };
    addTable(list, tableData, "", 164, 20, 28, 4);

    addGap(list, 20);

    // Get system memory info for trackbar
    u64 RAM_Used_system_u, RAM_Total_system_u;
    svcGetSystemInfo(&RAM_Used_system_u, 1, INVALID_HANDLE, 2);
    svcGetSystemInfo(&RAM_Total_system_u, 0, INVALID_HANDLE, 2);

    const float freeRamMB =
        static_cast<float>(RAM_Total_system_u - RAM_Used_system_u) /
        (1024.0f * 1024.0f);

    // System RAM display
    char ramString[24];
    snprintf(ramString, sizeof(ramString), "%.2f MB %s", freeRamMB,
             FREE.c_str());

    const char *ramColor =
        freeRamMB >= 9.0f ? "healthy_ram"
                          : (freeRamMB >= 3.0f ? "neutral_ram" : "bad_ram");

    tableData.clear();
    tableData = {{SYSTEM_RAM, "", ramString}};
    addTable(list, tableData, "", 165 + 2, 19 - 2, 19 - 2, 0, "header",
             ramColor, DEFAULT_STR, RIGHT_STR, true, true);

    // Read custom overlay memory from INI
    const std::string customMemoryStr = parseValueFromIniSection(
        ULTRAHAND_CONFIG_INI_PATH, MEMORY_STR, "custom_overlay_memory_MB");

    u32 customMemoryMB = 0;
    bool hasIniEntry = false;

    if (!customMemoryStr.empty()) {
      int parsedValue = 0;
      bool isValid = true;

      for (char c : customMemoryStr) {
        if (c < '0' || c > '9') {
          isValid = false;
          break;
        }
      }

      if (isValid && !customMemoryStr.empty()) {
        parsedValue = std::atoi(customMemoryStr.c_str());
        if (parsedValue > 8 && parsedValue % 2 == 0) {
          customMemoryMB = static_cast<u32>(parsedValue);
          heapSizeCache.customSizeMB = customMemoryMB;
          hasIniEntry = true;
        }
      }
    }

    if (!hasIniEntry && currentMB > 8) {
      customMemoryMB = currentMB;
    }

    // Create step descriptions for the trackbar
    std::vector<std::string> heapSizeLabels = {"4 MB", "6 MB", "8 MB"};

    if (customMemoryMB > 8) {
      heapSizeLabels.push_back(std::to_string(customMemoryMB) + " MB");
    }

    // Create the trackbar
    auto *heapTrackbar = new tsl::elm::NamedStepTrackBarV2(
        OVERLAY_MEMORY,
        "", // Empty packagePath
        heapSizeLabels, nullptr, nullptr, {}, "",
        false, // unlockedTrackbar
        false  // executeOnEveryTick
    );

    // Set initial position
    u8 initialStep = 1; // Default to 6MB

    if (currentMB == 4) {
      initialStep = 0;
    } else if (currentMB == 6) {
      initialStep = 1;
    } else if (currentMB == 8) {
      initialStep = 2;
    } else if (customMemoryMB > 8 && currentMB == customMemoryMB) {
      initialStep = 3;
    }

    auto lastSliderMB = std::make_shared<u32>(currentMB);

    heapTrackbar->setSimpleCallback([this, freeRamMB, lastSliderMB,
                                      customMemoryMB,
                                      hasIniEntry](s16 value, s16 index) {
      u64 newHeapBytes;
      u32 newMB;

      switch (index) {
      case 0:
        newHeapBytes = 0x400000;
        newMB = 4;
        break;
      case 1:
        newHeapBytes = 0x600000;
        newMB = 6;
        break;
      case 2:
        newHeapBytes = 0x800000;
        newMB = 8;
        break;
      case 3:
        if (hasIniEntry && customMemoryMB > 8) {
          newHeapBytes = mbToBytes(customMemoryMB);
          newMB = customMemoryMB;
        } else {
          return;
        }
        break;
      default:
        return;
      }

      OverlayHeapSize newHeapSize = static_cast<OverlayHeapSize>(newHeapBytes);
      const u32 oldMB = bytesToMB(static_cast<u64>(currentHeapSize));
      const u32 previousSliderMB = *lastSliderMB;

      if (newMB == previousSliderMB) {
        return;
      }

      // Check if we have enough memory for growth
      if (newMB > oldMB) {
        const float totalAvailableMB = freeRamMB + static_cast<float>(oldMB);
        constexpr float SAFETY_MARGIN_MB = 5.3f;

        if (static_cast<float>(newMB) > (totalAvailableMB - SAFETY_MARGIN_MB)) {
          if (tsl::notification) {
            tsl::notification->showNow(NOTIFY_HEADER + NOT_ENOUGH_MEMORY);
          }
          setOverlayHeapSize(currentHeapSize);
          *lastSliderMB = newMB;
          return;
        }
      }

      // Apply the change
      setOverlayHeapSize(newHeapSize);
      
      // Track if memory was adjusted compared to ACTIVE size
      memoryWasAdjusted.store(newHeapSize != currentHeapSize, std::memory_order_release);

      // Track if user increased memory to meet requirement
      if (newMB >= this->requiredMB) {
        this->userIncreasedMemory = true;
      }

      *lastSliderMB = newMB;
    });

    heapTrackbar->setProgress(initialStep);
    heapTrackbar->disableClickAnimation();
    list->addItem(heapTrackbar);

    addGap(list, 12);

    auto *rootFrame = new tsl::elm::OverlayFrame("Breezehand", MEMORY_WARNING);
    rootFrame->setContent(list);
    return rootFrame;
  }

  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {
    if (keysDown & KEY_B) {
      if (memoryWasAdjusted.load(std::memory_order_acquire)) {
        ult::launchingOverlay.store(true, std::memory_order_release);
        tsl::Overlay::get()->close();
      } else {
        tsl::goBack();
      }
      return true;
    }
    return false;
  }

  bool didUserIncreaseMemory() const { return userIncreasedMemory; }
};

// Function to check overlay memory and show warning if insufficient
bool checkOverlayMemory(u32 requiredMB) {
  const u32 currentMB = bytesToMB(static_cast<u64>(currentHeapSize));
  
  if (currentMB >= requiredMB) {
    return true; // Sufficient memory
  }

  // Reset flag
  memoryWasAdjusted.store(false, std::memory_order_release);

  // Show warning menu
  tsl::changeTo<MemoryWarningMenu>(requiredMB);

  // Wait for menu to close and check if memory was adjusted
  // Note: This is a synchronous blocking approach
  // The actual check happens when the menu returns
  
  return false; // Insufficient memory, user needs to adjust
}

// --- Cheat / Combo Key Globals & Structs ---

// toggles/thread removed

// CheatUtils namespace moved before CheatMenu in previous edits, leaving this
// clean
class CheatMenu : public tsl::Gui {
private:
  u32 cheat_id;
  std::string cheat_name;

public:
  CheatMenu(u32 id = 0, const std::string &name = "")
      : cheat_id(id), cheat_name(name) {}

  virtual tsl::elm::Element *createUI() override;
  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override;
};

// --- Cheat Utils ---
namespace CheatUtils {
DmntCheatProcessMetadata metadata;
char build_id[0x20];
bool metadata_loaded = false;

void EnsureMetadata() {
  if (!metadata_loaded) {
    if (R_FAILED(dmntchtGetCheatProcessMetadata(&metadata))) {
      return;
    }
    std::memcpy(build_id, metadata.main_nso_build_id, 0x20);
    metadata_loaded = true;
  }
}

std::string GetBuildIdString() {
  EnsureMetadata();
  std::stringstream ss;
  ss << std::hex << std::uppercase << std::setfill('0');
  for (int i = 0; i < 8; i++)
    ss << std::setw(2) << (int)build_id[i];
  return ss.str();
}

std::string GetTitleIdString() {
  EnsureMetadata();
  std::stringstream ss;
  ss << std::hex << std::uppercase << std::setfill('0') << std::setw(16)
     << metadata.title_id;
  return ss.str();
}

void SaveToggles(const std::string &path) {
  FILE *file = fopen(path.c_str(), "w");
  if (!file)
    return;

  u64 cheatCount = 0;
  if (R_SUCCEEDED(dmntchtGetCheatCount(&cheatCount)) && cheatCount > 0) {
    std::vector<DmntCheatEntry> cheats(cheatCount);
    if (R_SUCCEEDED(
            dmntchtGetCheats(cheats.data(), cheatCount, 0, &cheatCount))) {
      for (const auto &cheat : cheats) {
        fprintf(file, "[%s]\n%s\n\n", cheat.definition.readable_name,
                cheat.enabled ? "true" : "false");
      }
    }
  }
  fclose(file);
}

void ClearCheats() {
  g_cheatFolderIndexStack.clear();
  g_cheatFolderNameStack.clear();
  u64 count = 0;
  if (R_SUCCEEDED(dmntchtGetCheatCount(&count)) && count > 0) {
    // dmntcht doesn't have a ClearAll, so we remove one by one.
    // Note: Since IDs might shift or re-index, simpler to just get list and
    // remove. Removing a cheat might invalidate others if index based, but we
    // use IDs.
    std::vector<DmntCheatEntry> cheats(count);
    if (R_SUCCEEDED(dmntchtGetCheats(cheats.data(), count, 0, &count))) {
      for (const auto &cheat : cheats) {
        dmntchtRemoveCheat(cheat.cheat_id);
      }
    }
  }
}

bool ParseCheats(const std::string &path) {
  // Clear existing cheats before loading new ones
  ClearCheats();
  FILE *pfile;
  pfile = fopen(path.c_str(), "rb");
  if (pfile != NULL) {
    fseek(pfile, 0, SEEK_END);
    size_t len = ftell(pfile);
    u8 *s = new u8[len];
    fseek(pfile, 0, SEEK_SET);
    fread(s, 1, len, pfile);
    // if (strcmp(m_cheatcode_path + (strlen(m_cheatcode_path) - 20),
    // logtext("%02X%02X%02X%02X%02X%02X%02X%02X.txt", build_id[0], build_id[1],
    // build_id[2], build_id[3], build_id[4], build_id[5], build_id[6],
    // build_id[7]).data) != 0) {
    //     for (size_t i = 0; i < len; i++) {
    //         if (s[i] == '{') s[i] = '[';
    //         if (s[i] == '}') s[i] = ']';
    //     }
    // }
    DmntCheatEntry cheatentry;
    cheatentry.definition.num_opcodes = 0;
    cheatentry.enabled = false;
    u8 label_len = 0;
    size_t i = 0;
    while (i < len) {
      if (std::isspace(static_cast<unsigned char>(s[i]))) {
        /* Just ignore whitespace. */
        i++;
      } else if (s[i] == '[') {
        if (cheatentry.definition.num_opcodes != 0) {
          if (cheatentry.enabled == true)
            dmntchtSetMasterCheat(&(cheatentry.definition));
          else
            dmntchtAddCheat(&(cheatentry.definition), cheatentry.enabled,
                            &(cheatentry.cheat_id));
        }
        /* Parse a normal cheat set to off */
        cheatentry.definition.num_opcodes = 0;
        cheatentry.enabled = false;
        /* Extract name bounds. */
        size_t j = i + 1;
        while (s[j] != ']') {
          j++;
          if (j >= len) {
            return false;
          }
        }
        /* s[i+1:j] is cheat name. */
        const size_t cheat_name_len =
            std::min(j - i - 1, sizeof(cheatentry.definition.readable_name));
        std::memcpy(cheatentry.definition.readable_name, &s[i + 1],
                    cheat_name_len);
        cheatentry.definition.readable_name[cheat_name_len] = 0;
        label_len = cheat_name_len;

        /* Skip onwards. */
        i = j + 1;
      } else if (s[i] == '(') {
        size_t j = i + 1;
        while (s[j] != ')') {
          j++;
          if (j >= len) {
            return false;
          }
        }
        i = j + 1;
      } else if (s[i] == '{') {
        if (cheatentry.definition.num_opcodes != 0) {
          dmntchtAddCheat(&(cheatentry.definition), cheatentry.enabled,
                          &(cheatentry.cheat_id));
        }
        /* We're parsing a master cheat. Turn it on */
        cheatentry.definition.num_opcodes = 0;
        cheatentry.enabled = true;
        /* Extract name bounds */
        size_t j = i + 1;
        while (s[j] != '}') {
          j++;
          if (j >= len) {
            return false;
          }
        }

        /* s[i+1:j] is cheat name. */
        const size_t cheat_name_len =
            std::min(j - i - 1, sizeof(cheatentry.definition.readable_name));
        memcpy(cheatentry.definition.readable_name, &s[i + 1], cheat_name_len);
        cheatentry.definition.readable_name[cheat_name_len] = 0;
        label_len = cheat_name_len;
        // strcpy(cheatentry.definition.readable_name,"master code");

        /* Skip onwards. */
        i = j + 1;
      } else if (std::isxdigit(static_cast<unsigned char>(s[i]))) {
        if (label_len == 0)
          return false;
        /* Bounds check the opcode count. */
        if (cheatentry.definition.num_opcodes >=
            sizeof(cheatentry.definition.opcodes) / 4) {
          if (cheatentry.definition.num_opcodes != 0) {
            dmntchtAddCheat(&(cheatentry.definition), cheatentry.enabled,
                            &(cheatentry.cheat_id));
          }
          return false;
        }

        /* We're parsing an instruction, so validate it's 8 hex digits. */
        for (size_t j = 1; j < 8; j++) {
          /* Validate 8 hex chars. */
          if (i + j >= len ||
              !std::isxdigit(static_cast<unsigned char>(s[i + j]))) {
            if (cheatentry.definition.num_opcodes != 0) {
              dmntchtAddCheat(&(cheatentry.definition), cheatentry.enabled,
                              &(cheatentry.cheat_id));
            }
            return false;
          }
        }

        /* Parse the new opcode. */
        char hex_str[9] = {0};
        std::memcpy(hex_str, &s[i], 8);
        cheatentry.definition.opcodes[cheatentry.definition.num_opcodes++] =
            std::strtoul(hex_str, NULL, 16);

        /* Skip onwards. */
        i += 8;
      } else {
        /* Unexpected character encountered. */
        if (cheatentry.definition.num_opcodes != 0) {
          dmntchtAddCheat(&(cheatentry.definition), cheatentry.enabled,
                          &(cheatentry.cheat_id));
        }
        return false;
      }
    }
    if (cheatentry.definition.num_opcodes != 0) {
      dmntchtAddCheat(&(cheatentry.definition), cheatentry.enabled,
                      &(cheatentry.cheat_id));
    }
    fclose(
        pfile); // take note that if any error occured above this isn't closed
    return true;
  }
  return false;
}

void SaveCheatsToDir(const std::string &directory, bool append = false) {
  ult::createDirectory(directory);
  std::string bid = GetBuildIdString();
  std::string tid = GetTitleIdString();
  std::string path = directory + bid + ".txt";
  std::string togglePath = directory + "toggles.txt";

  FILE *file = nullptr;
  if (append) {
    file = fopen("sdmc:/switch/breeze/cheats/log.txt", "a");
    if (file) {
      time_t now = time(nullptr);
      struct tm *local_time = localtime(&now);
      fprintf(file, "%02d-%02d-%04d %02d:%02d:%02d %s\n", local_time->tm_mday,
              local_time->tm_mon + 1, local_time->tm_year + 1900,
              local_time->tm_hour, local_time->tm_min, local_time->tm_sec,
              path.c_str());
    }
  } else {
    file = fopen(path.c_str(), "w");
  }

  if (file) {
    fprintf(file, "[Breezehand %s TID: %s BID: %s]\n\n", APP_VERSION,
            tid.c_str(), bid.c_str());

    u64 cheatCount = 0;
    if (R_SUCCEEDED(dmntchtGetCheatCount(&cheatCount)) && cheatCount > 0) {
      std::vector<DmntCheatEntry> cheats(cheatCount);
      if (R_SUCCEEDED(
              dmntchtGetCheats(cheats.data(), cheatCount, 0, &cheatCount))) {
        for (u32 i = 0; i < cheatCount; i++) {
          const auto &cheat = cheats[i];
          if (i == 0 && cheat.cheat_id == 0) {
            fprintf(file, "{%s}\n", cheat.definition.readable_name);
          } else {
            fprintf(file, "[%s]\n", cheat.definition.readable_name);
          }

          for (u32 j = 0; j < cheat.definition.num_opcodes; j++) {
            u32 op = cheat.definition.opcodes[j];
            u32 opcode = (op >> 28) & 0xF;
            u8 T = (op >> 24) & 0xF;

            if ((opcode == 9) && (((op >> 8) & 0xF) == 0)) {
              fprintf(file, "%08X\n", op);
              continue;
            }

            if (opcode == 0xC) {
              opcode = (op >> 24) & 0xFF;
              T = (op >> 20) & 0xF;
              u8 X = (op >> 8) & 0xF;
              if (opcode == 0xC0)
                opcode = opcode * 16 + X;
            }

            if (opcode == 10) {
              u8 O = (op >> 8) & 0xF;
              if (O == 2 || O == 4 || O == 5)
                T = 8;
              else
                T = 4;
            }

            switch (opcode) {
            case 0:
            case 1:
            case 0xC06:
              fprintf(file, "%08X ", cheat.definition.opcodes[j++]);
            case 9:
            case 0xC04:
              fprintf(file, "%08X ", cheat.definition.opcodes[j++]);
            case 3:
            case 10:
              fprintf(file, "%08X ", cheat.definition.opcodes[j]);
              if (T == 8 || (T == 0 && opcode == 3)) {
                j++;
                fprintf(file, "%08X ", cheat.definition.opcodes[j]);
              }
              break;
            case 4:
            case 6:
            case 0xC4:
              fprintf(file, "%08X ", cheat.definition.opcodes[j++]);
            case 5:
            case 7:
            case 0xC00:
            case 0xC02:
              fprintf(file, "%08X ", cheat.definition.opcodes[j++]);
            case 2:
            case 8:
            case 0xC1:
            case 0xC2:
              // Fallthrough to default
            default:
              fprintf(file, "%08X", cheat.definition.opcodes[j]);
              break;
            }
            fprintf(file, "\n");
          }
          fprintf(file, "\n");
        }
      }
    }
    fclose(file);
  }

  if (!append)
    SaveToggles(togglePath);
}

void SaveCheatsToFile() {
  SaveCheatsToDir("sdmc:/switch/breeze/cheats/" + GetTitleIdString() + "/");
}

void LoadToggles(const std::string &path) {
  if (!isFile(path))
    return;

  std::ifstream file(path);
  std::string line;
  std::string currentCheat;

  while (std::getline(file, line)) {
    if (line.empty())
      continue;
    if (line[0] == '[') {
      size_t end = line.find(']');
      if (end != std::string::npos) {
        currentCheat = line.substr(1, end - 1);
      }
    } else if (!currentCheat.empty()) {
      bool enabled = (line.find("true") != std::string::npos ||
                      line.find("on") != std::string::npos ||
                      line.find("1") != std::string::npos);

      u64 cheatCount = 0;
      if (R_SUCCEEDED(dmntchtGetCheatCount(&cheatCount)) && cheatCount > 0) {
        std::vector<DmntCheatEntry> cheats(cheatCount);
        if (R_SUCCEEDED(
                dmntchtGetCheats(cheats.data(), cheatCount, 0, &cheatCount))) {
          for (const auto &cheat : cheats) {
            if (currentCheat == cheat.definition.readable_name) {
              if (cheat.enabled != enabled) {
                dmntchtToggleCheat(cheat.cheat_id);
              }
              break;
            }
          }
        }
      }
    }
  }
}

bool TryDownloadCheats(bool notify = true) {
  EnsureMetadata();
  std::string tid = GetTitleIdString();
  std::string bid = GetBuildIdString();
  std::string bid_low = ult::stringToLowercase(bid);

  std::string title = tid;
  std::string titleFilePath =
      "sdmc:/switch/breeze/cheats/" + tid + "/title.txt";
  if (ult::isFile(titleFilePath)) {
    std::ifstream tfile(titleFilePath);
    if (std::getline(tfile, title))
      ult::trim(title);
  }

  // Check for cheat_url_txt and copy template if missing
  std::string configPath = "sdmc:/config/breezehand/cheat_url_txt";
  if (!ult::isFile(configPath)) {
    std::string templatePath = "sdmc:/config/breezehand/cheat_url_txt.template";
    if (ult::isFile(templatePath)) {
      ult::copyFileOrDirectory(templatePath, configPath);
    }
  }

  if (!ult::isFile(configPath)) {
    if (notify)
      tsl::notification->show("Config not found\ncheat_url_txt");
    return false;
  }

  std::vector<std::string> urls;
  std::ifstream file(configPath);
  std::string line;
  while (std::getline(file, line)) {
    ult::trim(line);
    if (!line.empty())
      urls.push_back(line);
  }

  if (urls.empty()) {
    if (notify)
      tsl::notification->show("URL list is empty");
    return false;
  }

  if (g_cheatDownloadIndex >= (int)urls.size()) {
    if (notify)
      tsl::notification->show("End of URL list\n(NotFound)");
    return false;
  }

  std::string targetDir = "sdmc:/switch/breeze/cheats/" + tid + "/";
  ult::createDirectory(targetDir);
  std::string dest = targetDir + bid + ".txt";

  static constexpr SocketInitConfig socketInitConfig = {
      .tcp_tx_buf_size = 16 * 1024,
      .tcp_rx_buf_size = 16 * 1024 * 2,
      .tcp_tx_buf_max_size = 64 * 1024,
      .tcp_rx_buf_max_size = 64 * 1024 * 2,
      .udp_tx_buf_size = 512,
      .udp_rx_buf_size = 512,
      .sb_efficiency = 1,
      .bsd_service_type = BsdServiceType_Auto};
  bool socketInitialized = R_SUCCEEDED(socketInitialize(&socketInitConfig));

  for (size_t i = g_cheatDownloadIndex; i < urls.size(); i++) {
    std::string rawUrl = urls[i];
    rawUrl = ReplaceAll(rawUrl, "{TID}", tid);
    rawUrl = ReplaceAll(rawUrl, "{BID}", bid);
    rawUrl = ReplaceAll(rawUrl, "{bid}", bid_low);
    rawUrl = ReplaceAll(rawUrl, "{bid_lowercase}", bid_low);
    rawUrl = ReplaceAll(rawUrl, "{TITLE}", title);

    bool found = false;

    // Check base URL first
    LogDownload(rawUrl);
    if (ult::downloadFile(rawUrl, dest, true, true)) {
      found = true;

      size_t lastSlash = rawUrl.find_last_of('/');
      if (lastSlash != std::string::npos) {
        std::string notesUrl = rawUrl.substr(0, lastSlash + 1) + "notes.txt";
        LogDownload(notesUrl);
        ult::downloadFile(notesUrl, targetDir + "notes.txt", true, true);
      }

      // Base exists - check for higher versions v1, v2, ... v15
      for (int v = 1; v <= 15; v++) {
        std::string vUrl = rawUrl;
        size_t lastDot = vUrl.find_last_of('.');
        if (lastDot != std::string::npos && vUrl.substr(lastDot) == ".txt") {
          vUrl = vUrl.substr(0, lastDot) + ".v" + ult::to_string(v) + ".txt";
        } else {
          break;
        }

        LogDownload(vUrl);
        if (!ult::downloadFile(vUrl, dest, true, true)) {
          // Version doesn't exist - stop, use last successful download
          break;
        }
      }
    }
    // If base doesn't exist, skip versions for this source

    if (found) {
      if (socketInitialized)
        socketExit();
      g_cheatDownloadIndex = i + 1;
      if (CheatUtils::ParseCheats(dest)) {
        if (notify)
          tsl::notification->show("Downloaded & Loaded!");
        return true;
      } else {
        if (notify)
          tsl::notification->show("Downloaded but empty");
        return false;
      }
    }
  }

  if (socketInitialized)
    socketExit();
  g_cheatDownloadIndex = urls.size();
  if (notify)
    tsl::notification->show("No cheats found\nat current sources");
  return false;
}

u32 ConvertTripleZeroCheatsToFolders() {
  u64 count = 0;
  if (!R_SUCCEEDED(dmntchtGetCheatCount(&count)) || count == 0) {
    return 0;
  }

  std::vector<DmntCheatEntry> cheats(count);
  if (!R_SUCCEEDED(dmntchtGetCheats(cheats.data(), count, 0, &count))) {
    return 0;
  }
  cheats.resize(count);

  u32 converted = 0;
  bool useFolderStart = true;
  for (auto &cheat : cheats) {
    if (cheat.definition.num_opcodes == 3 &&
        cheat.definition.opcodes[0] == 0x00000000 &&
        cheat.definition.opcodes[1] == 0x00000000 &&
        cheat.definition.opcodes[2] == 0x00000000) {
      cheat.definition.opcodes[0] = useFolderStart ? 0x20000000 : 0x20000001;
      cheat.definition.num_opcodes = 1;
      useFolderStart = !useFolderStart;
      converted++;
    }
  }

  if (converted == 0) {
    return 0;
  }

  ClearCheats();
  for (auto &cheat : cheats) {
    u32 outId = 0;
    dmntchtAddCheat(&cheat.definition, cheat.enabled, &outId);
  }

  SaveCheatsToFile();
  return converted;
}

void AddComboKeyToCheat(u32 cheat_id, u32 key_mask) {
  if (key_mask == 0)
    return; // No key?

  u64 count = 0;
  dmntchtGetCheatCount(&count);
  std::vector<DmntCheatEntry> cheats(count);
  dmntchtGetCheats(cheats.data(), count, 0, &count);

  for (auto &cheat : cheats) {
    if (cheat.cheat_id == cheat_id) {
      // Check if already has a conditional
      bool hasConditional = false;
      if (cheat.definition.num_opcodes >= 1) {
        u32 firstOp = cheat.definition.opcodes[0];
        if ((firstOp & 0xF0000000) == 0x80000000) {
          // Update existing mask
          cheat.definition.opcodes[0] = 0x80000000 | key_mask;
          hasConditional = true;
        }
      }

      if (!hasConditional) {
        // Shift opcodes to make room for start (0x80000xxx) and end
        // (0x20000000)
        if (cheat.definition.num_opcodes + 2 <= 0x40) { // Max opcodes check
          for (int i = cheat.definition.num_opcodes - 1; i >= 0; i--) {
            cheat.definition.opcodes[i + 1] = cheat.definition.opcodes[i];
          }
          cheat.definition.opcodes[0] = 0x80000000 | key_mask;
          cheat.definition.opcodes[cheat.definition.num_opcodes + 1] =
              0x20000000;
          cheat.definition.num_opcodes += 2;
        } else {
          tsl::notification->show("Too many opcodes to add combo!");
          return;
        }
      }

      // Refresh in dmntcht
      dmntchtRemoveCheat(cheat.cheat_id);
      u32 outId = 0;
      dmntchtAddCheat(&cheat.definition, cheat.enabled, &outId);

      // Persist
      SaveCheatsToFile();
      return;
    }
  }
}

void RemoveComboKeyFromCheat(u32 cheat_id) {
  u64 count = 0;
  dmntchtGetCheatCount(&count);
  std::vector<DmntCheatEntry> cheats(count);
  dmntchtGetCheats(cheats.data(), count, 0, &count);

  for (auto &cheat : cheats) {
    if (cheat.cheat_id == cheat_id) {
      if (cheat.definition.num_opcodes >= 2) {
        u32 first = cheat.definition.opcodes[0];
        u32 last = cheat.definition.opcodes[cheat.definition.num_opcodes - 1];

        if ((first & 0xF0000000) == 0x80000000 &&
            (last & 0xF0000000) == 0x20000000) {
          // Remove first (shift left)
          for (u32 i = 0; i < cheat.definition.num_opcodes - 1; i++) {
            cheat.definition.opcodes[i] = cheat.definition.opcodes[i + 1];
          }
          // Remove last (which is now at num-2 because of shift)
          cheat.definition.num_opcodes -= 2;

          // Refresh
          dmntchtRemoveCheat(cheat.cheat_id);
          u32 outId = 0;
          dmntchtAddCheat(&cheat.definition, cheat.enabled, &outId);

          SaveCheatsToFile();
          tsl::notification->show("Combo key removed");
        } else {
          tsl::notification->show("No combo key found");
        }
      }
      return;
    }
  }
}

class ComboSetItem : public tsl::elm::ListItem {
private:
  u32 cheat_id;
  u64 holdStartTick = 0;
  u64 capturedKeys = 0;
  bool capturing = false;
  std::string defaultText;

  // Helper to get string name for key mask (Reused from deleted overlay,
  // simplified)
  std::string getKeyNames(u64 key) {
    std::string names;
    if (key & KEY_A)
      names += "A+";
    if (key & KEY_B)
      names += "B+";
    if (key & KEY_X)
      names += "X+";
    if (key & KEY_Y)
      names += "Y+";
    if (key & KEY_L)
      names += "L+";
    if (key & KEY_R)
      names += "R+";
    if (key & KEY_ZL)
      names += "ZL+";
    if (key & KEY_ZR)
      names += "ZR+";
    if (key & KEY_PLUS)
      names += "+ +";
    if (key & KEY_MINUS)
      names += "- +";
    if (key & KEY_DLEFT)
      names += "DLeft+";
    if (key & KEY_DUP)
      names += "DUp+";
    if (key & KEY_DRIGHT)
      names += "DRight+";
    if (key & KEY_DDOWN)
      names += "DDown+";
    if (key & KEY_LSTICK)
      names += "LS+";
    if (key & KEY_RSTICK)
      names += "RS+";
    if (!names.empty())
      names.pop_back(); // Remove last +
    return names;
  }

public:
  ComboSetItem(const std::string &text, u32 id)
      : tsl::elm::ListItem(text), cheat_id(id), defaultText(text) {
    this->setNote("Press A to start capture");
    this->setAlwaysShowNote(true);
  }

  virtual void draw(tsl::gfx::Renderer *renderer) override {
    tsl::elm::ListItem::draw(renderer);
  }

  virtual bool handleInput(u64 keysDown, u64 keysHeld,
                           const HidTouchState &touchPos,
                           HidAnalogStickState joyStickPosLeft,
                           HidAnalogStickState joyStickPosRight) override {
    if (capturing) {
      u64 keys =
          keysHeld & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L | KEY_R | KEY_ZL |
                      KEY_ZR | KEY_PLUS | KEY_MINUS | KEY_DLEFT | KEY_DUP |
                      KEY_DRIGHT | KEY_DDOWN | KEY_LSTICK | KEY_RSTICK);

      if (keys != 0) {
        if (holdStartTick == 0) {
          holdStartTick = armGetSystemTick();
          capturedKeys = keys;
          this->setNote("Capture: " + getKeyNames(keys) + " (1s)");
        } else {
          if (keys == capturedKeys) {
            u64 diff = armGetSystemTick() - holdStartTick;
            if (armTicksToNs(diff) >= 500000000ULL) { // 0.5 second
              if (cheat_id != 0) {
                CheatUtils::AddComboKeyToCheat(cheat_id, capturedKeys);
                tsl::notification->show("Combo Key Set: " +
                                        getKeyNames(capturedKeys));
                capturing = false;
                // Auto-return & Refresh
                refreshPage.store(true, std::memory_order_release);
                tsl::goBack();
                return true;
              }
            } else {
              // Countdown/feedback
              float elapsed = armTicksToNs(diff) / 1000000000.0f;
              char buf[64];
              std::snprintf(buf, sizeof(buf), "Capture: %s (%.1fs)",
                            getKeyNames(capturedKeys).c_str(), 1.0f - elapsed);
              this->setNote(buf);
            }
          } else {
            // Keys changed, reset timer
            holdStartTick = armGetSystemTick();
            capturedKeys = keys;
            this->setNote("Capture: " + getKeyNames(keys) + " (1s)");
          }
        }
      } else {
        holdStartTick = 0;
        capturedKeys = 0;
        this->setNote("Hold keys for 0.5s");
      }
      // Consume EVERYTHING including B while capturing
      return true;
    }

    if (!this->hasFocus()) {
      holdStartTick = 0;
      capturedKeys = 0;
      capturing = false;
      this->setNote("Press A to start capture");
    }

    return tsl::elm::ListItem::handleInput(keysDown, keysHeld, touchPos,
                                           joyStickPosLeft, joyStickPosRight);
  }

  virtual bool onClick(u64 keys) override {
    if (keys & KEY_A) {
      if (!capturing) {
        capturing = true;
        holdStartTick = 0;
        capturedKeys = 0;
        this->setNote("Hold keys for 0.5s");
        return true;
      }
    }
    return tsl::elm::ListItem::onClick(keys);
  }

  // Override click listener to do nothing or show info
};
class CheatToggleItem : public tsl::elm::ToggleListItem {
public:
  u32 cheat_id;
  CheatToggleItem(const std::string &name, bool state, u32 id, u8 fontSize)
      : tsl::elm::ToggleListItem(name, state, "", "", true), cheat_id(id) {
    this->setUseLeftBox(true);
    this->setFontSize(fontSize);
  }
};
std::string GetComboKeyGlyphs(u32 key_mask) {
  if (key_mask == 0)
    return "";
  std::string glyphs;
  for (const auto &info : ult::KEYS_INFO) {
    if (key_mask & (u32)info.key) {
      glyphs += info.glyph;
    }
  }
  if (!glyphs.empty())
    glyphs += " ";
  return glyphs;
}
} // namespace CheatUtils
static const char *const condition_str[] = {"",     " > ",  " >= ", " < ",
                                            " <= ", " == ", " != "};
static const char *const math_str[] = {
    " + ",   " - ",   " * ",         " << ",   " >> ",   " & ",    " | ",
    " NOT ", " XOR ", " None/Move ", " fadd ", " fsub ", " fmul ", " fdiv "};
static const char *const heap_str[] = {"main+", "heap+", "alias+", "aslr+",
                                       "blank+"};
static const std::vector<u32> buttonCodes = {
    0x80000040, 0x80000080, 0x80000100, 0x80000200, 0x80000001, 0x80000002,
    0x80000004, 0x80000008, 0x80000010, 0x80000020, 0x80000400, 0x80000800,
    0x80001000, 0x80002000, 0x80004000, 0x80008000, 0x80100000, 0x80200000,
    0x80400000, 0x80800000, 0x80010000, 0x80020000, 0x80040000, 0x80080000,
    0x81000000, 0x82000000};
static const std::vector<std::string> buttonNames = {
    "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "",
    "SL", "SR"};

static bool s_noteMinimalMode = false;

namespace {
std::string WrapNote(const std::string &note, size_t limit = 60) {
  if (note.length() <= limit)
    return note;
  std::string result = "";
  for (size_t i = 0; i < note.length(); i += limit) {
    if (i > 0)
      result += "\n";
    result += note.substr(i, limit);
  }
  return result;
}

std::string DisassembleARM64(u32 code, u64 address = 0) {
  return air::DisassembleARM64(code, address);
}

std::string FormatValueNote(u64 val, u32 width, u64 address = 0) {
  if (s_noteMinimalMode) {
    if (width == 4) {
      std::string asmStr = DisassembleARM64((u32)val, address);
      if (!asmStr.empty())
        return " asm=" + asmStr;

      searchValue_t sv{};
      sv._u32 = static_cast<u32>(val);
      const float absf = std::fabs(sv._f32);
      if (std::isfinite(sv._f32) && absf >= 0.01f && absf <= 1000000.0f) {
        char fbuf[64];
        snprintf(fbuf, sizeof(fbuf), " f=%.6g", sv._f32);
        return fbuf;
      }

      char ibuf[64];
      snprintf(ibuf, sizeof(ibuf), " s32=%d", static_cast<s32>(sv._u32));
      return ibuf;
    }
    if (width == 8) {
      searchValue_t sv{};
      sv._u64 = val;
      const double absd = std::fabs(sv._f64);
      if (std::isfinite(sv._f64) && absd >= 0.01 && absd <= 1000000.0) {
        char dbuf[64];
        snprintf(dbuf, sizeof(dbuf), " d=%.6g", sv._f64);
        return dbuf;
      }
      char ibuf[64];
      snprintf(ibuf, sizeof(ibuf), " s64=%lld", (long long)(s64)val);
      return ibuf;
    }
    if (width == 2) {
      char ibuf[64];
      snprintf(ibuf, sizeof(ibuf), " s16=%d", (int)(s16)val);
      return ibuf;
    }
    if (width == 1) {
      char ibuf[64];
      snprintf(ibuf, sizeof(ibuf), " u8=%u", (unsigned int)(u8)val);
      return ibuf;
    }
    return "";
  }

  char buf[128];
  std::string result = " (";

  // Integer representation (show both unsigned and signed views)
  if (width == 1)
    snprintf(buf, sizeof(buf), "u=%u, s=%d", (u8)val, (s8)val);
  else if (width == 2)
    snprintf(buf, sizeof(buf), "u=%u, s=%d", (u16)val, (s16)val);
  else if (width == 4)
    snprintf(buf, sizeof(buf), "u=%u, s=%d", (u32)val, (s32)val);
  else if (width == 8)
    snprintf(buf, sizeof(buf), "u=%llu, s=%lld",
             (unsigned long long)val, (long long)(s64)val);
  else
    snprintf(buf, sizeof(buf), "u=%llu, s=%lld",
             (unsigned long long)val, (long long)(s64)val);

  result += buf;

  // Float / Double representation
  searchValue_t sv;
  sv._u64 = val;
  if (width == 4) {
    snprintf(buf, sizeof(buf), ", f=%.6g", sv._f32);
    result += buf;

    // ASM Disassembly check for 4-byte width (32-bit instruction)
    std::string asmStr = DisassembleARM64((u32)val, address);
    if (!asmStr.empty()) {
      result += ", asm=" + asmStr;
    }
  } else if (width == 8) {
    snprintf(buf, sizeof(buf), ", d=%.6g", sv._f64);
    result += buf;
  }

  result += ")";
  return result;
}
} // namespace

std::string GetOpcodeNote(const std::vector<u32> &opcodes, size_t &index) {
  if (index >= opcodes.size())
    return "";
  u32 first_dword = opcodes[index++];
  u32 type = (first_dword >> 28) & 0xF;

  if (type >= 0xC)
    type = (type << 4) | ((first_dword >> 24) & 0xF);
  if (type >= 0xF0)
    type = (type << 4) | ((first_dword >> 20) & 0xF);

  char buffer[256];
  buffer[0] = 0;

  auto GetNextDword = [&]() -> u32 {
    return (index < opcodes.size()) ? opcodes[index++] : 0;
  };

  auto GetNextVmInt = [&](u32 bit_width) -> u64 {
    u32 first = GetNextDword();
    if (bit_width == 1)
      return (u8)first;
    if (bit_width == 2)
      return (u16)first;
    if (bit_width == 4)
      return first;
    if (bit_width == 8)
      return (((u64)first) << 32) | (u64)GetNextDword();
    return 0;
  };

  auto HeapText = [&](u8 memType) -> std::string {
    if (memType < 5)
      return heap_str[memType];
    return "ERROR_HEAP(" + std::to_string(memType) + ")+";
  };

  auto ConditionText = [&](u8 cond) -> std::string {
    if (cond < 7)
      return condition_str[cond];
    return " [ERROR_COND(" + std::to_string(cond) + ")] ";
  };

  auto MathText = [&](u8 op, u8 maxValid) -> std::string {
    if (op <= maxValid)
      return math_str[op];
    return " [ERROR_MATH(" + std::to_string(op) + ")] ";
  };

  switch (type) {
  case 0: // Store Static
  {
    u8 width = (first_dword >> 24) & 0xF;
    u8 memType = (first_dword >> 20) & 0xF;
    u8 regIdx = (first_dword >> 16) & 0xF;
    u32 second_dword = GetNextDword();
    u64 addr = ((u64)(first_dword & 0xFF) << 32) | second_dword;
    u64 val = GetNextVmInt(width);
    if (s_noteMinimalMode) {
      std::string asmStr = DisassembleARM64((u32)val, addr);
      if (!asmStr.empty()) {
        snprintf(buffer, sizeof(buffer), "0x%010lX: %s", addr, asmStr.c_str());
      } else {
        std::string fallback = FormatValueNote(val, width, addr);
        snprintf(buffer, sizeof(buffer), "0x%010lX:%s", addr, fallback.c_str());
      }
    } else {
      std::string heapText = HeapText(memType);
      snprintf(buffer, sizeof(buffer), "[%sR%d+0x%010lX] = 0x%lX%s (W=%d)",
               heapText.c_str(), regIdx, addr, val,
               FormatValueNote(val, width, addr).c_str(), width);
    }
    break;
  }
  case 1: // Begin Conditional Block
  {
    u8 width = (first_dword >> 24) & 0xF;
    u8 memType = (first_dword >> 20) & 0xF;
    u8 cond = (first_dword >> 16) & 0xF;
    bool useOfs = (first_dword >> 12) & 0xF;
    u8 ofsReg = (first_dword >> 8) & 0xF;
    u32 second_dword = GetNextDword();
    u64 addr = ((u64)(first_dword & 0xFF) << 32) | second_dword;
    u64 val = GetNextVmInt(width);
    if (s_noteMinimalMode) {
      snprintf(buffer, sizeof(buffer), "If [0x%010lX]%s", addr,
               FormatValueNote(val, width, addr).c_str());
    } else {
      std::string heapText = HeapText(memType);
      std::string condText = ConditionText(cond);
      std::string ofsText = useOfs ? ("R" + std::to_string(ofsReg) + "+") : "";
      snprintf(buffer, sizeof(buffer), "If [%s%s0x%010lX] %s 0x%lX%s",
               heapText.c_str(), ofsText.c_str(), addr, condText.c_str(), val,
               FormatValueNote(val, width, addr).c_str());
    }
    break;
  }
  case 2: // End Conditional Block
  {
    u8 endType = (first_dword >> 24) & 0xF;
    snprintf(buffer, sizeof(buffer), endType ? "Else" : "Endif");
    break;
  }
  case 3: // Control Loop
  {
    bool start = ((first_dword >> 24) & 0xF) == 0;
    u8 regIdx = (first_dword >> 16) & 0xF;
    if (start) {
      u32 iters = GetNextDword();
      snprintf(buffer, sizeof(buffer), "Loop Start R%d = %d", regIdx, iters);
    } else {
      snprintf(buffer, sizeof(buffer), "Loop End R%d", regIdx);
    }
    break;
  }
  case 4: // Load Register Static
  {
    u8 regIdx = (first_dword >> 16) & 0xF;
    u64 val = (((u64)GetNextDword()) << 32) | GetNextDword();
    snprintf(buffer, sizeof(buffer), "R%d = 0x%016lX%s", regIdx, val,
             FormatValueNote(val, 8).c_str());
    break;
  }
  case 5: // Load Register Memory
  {
    u8 width = (first_dword >> 24) & 0xF;
    u8 memType = (first_dword >> 20) & 0xF;
    u8 regIdx = (first_dword >> 16) & 0xF;
    u8 loadFrom = (first_dword >> 12) & 0xF;
    u8 offReg = (first_dword >> 8) & 0xF;
    u32 second_dword = GetNextDword();
    u64 addr = ((u64)(first_dword & 0xFF) << 32) | second_dword;
    if (s_noteMinimalMode) {
      snprintf(buffer, sizeof(buffer), "R%d = [0x%010lX]%s", regIdx, addr,
               (width == 4) ? FormatValueNote(0, 0).c_str()
                            : ""); // Placeholder for ASM if applicable
    } else if (loadFrom == 3)
      {
      std::string heapText = HeapText(memType);
      snprintf(buffer, sizeof(buffer), "R%d = [%sR%d+0x%010lX] (W=%d)", regIdx,
               heapText.c_str(), offReg, addr, width);
      }
    else if (loadFrom)
      snprintf(buffer, sizeof(buffer), "R%d = [R%d+0x%010lX] (W=%d)", regIdx,
               (loadFrom == 1 ? regIdx : offReg), addr, width);
    else
      {
      std::string heapText = HeapText(memType);
      snprintf(buffer, sizeof(buffer), "R%d = [%s0x%010lX] (W=%d)", regIdx,
               heapText.c_str(), addr, width);
      }
    break;
  }
  case 6: // Store Static to Address
  {
    u8 width = (first_dword >> 24) & 0xF;
    u8 regIdx = (first_dword >> 16) & 0xF;
    bool inc = (first_dword >> 12) & 0xF;
    bool useOffSet = (first_dword >> 8) & 0xF;
    u8 offReg = (first_dword >> 4) & 0xF;
    u64 val = (((u64)GetNextDword()) << 32) | GetNextDword();
    if (s_noteMinimalMode) {
      snprintf(buffer, sizeof(buffer), "[R%d] = 0x%lX%s", regIdx, val,
               FormatValueNote(val, width).c_str());
    } else {
      snprintf(buffer, sizeof(buffer), "[R%d%s] = 0x%lX%s%s", regIdx,
               useOffSet ? (std::string("+R") + std::to_string(offReg)).c_str()
                         : "",
               val, inc ? " (Inc)" : "", FormatValueNote(val, width).c_str());
    }
    break;
  }
  case 7: // Perform Arithmetic Static
  {
    u8 regIdx = (first_dword >> 16) & 0xF;
    u8 mathOp = (first_dword >> 12) & 0xF;
    u32 val = GetNextDword();
    u8 width = (first_dword >> 24) & 0xF;
    if (s_noteMinimalMode) {
      snprintf(buffer, sizeof(buffer), "R%d = R%d...%s", regIdx, regIdx,
               FormatValueNote(val, 4).c_str());
    } else {
      std::string mathText = MathText(mathOp, 4);
      snprintf(buffer, sizeof(buffer), "R%d = R%d%s0x%08X%s (W=%d)", regIdx, regIdx,
               mathText.c_str(), val,
               FormatValueNote(val, 4).c_str(), width);
    }
    break;
  }
  case 8: // Begin Keypress Conditional Block
  {
    const u32 keyMask = first_dword & 0x0FFFFFFF;
    std::string keys = "If keys(";
    bool any = false;
    for (size_t i = 0; i < buttonCodes.size(); i++) {
      const u32 code = buttonCodes[i] & 0x0FFFFFFF;
      if ((keyMask & code) != 0) {
        if (any) {
          keys += "+";
        }
        keys += buttonNames[i];
        any = true;
      }
    }
    if (!any) {
      keys += "none";
    }
    keys += ") Auto-repeat";
    snprintf(buffer, sizeof(buffer), "%s", keys.c_str());
    break;
  }
  case 9: // Perform Arithmetic Register
  {
    u8 width = (first_dword >> 24) & 0xF;
    u8 mathOp = (first_dword >> 20) & 0xF;
    u8 dstReg = (first_dword >> 16) & 0xF;
    u8 srcReg1 = (first_dword >> 12) & 0xF;
    bool hasImm = (first_dword >> 8) & 0xF;
    if (hasImm) {
      u64 val = GetNextVmInt(width);
      if (s_noteMinimalMode) {
        snprintf(buffer, sizeof(buffer), "R%d = R%d...%s", dstReg, srcReg1,
                 FormatValueNote(val, width).c_str());
      } else {
        std::string mathText = MathText(mathOp, 13);
        snprintf(buffer, sizeof(buffer), "R%d = R%d%s0x%lX%s", dstReg, srcReg1,
                 mathText.c_str(), val,
                 FormatValueNote(val, width).c_str());
      }
    } else {
      u8 srcReg2 = (first_dword >> 4) & 0xF;
      std::string mathText = MathText(mathOp, 13);
      snprintf(buffer, sizeof(buffer), "R%d = R%d%sR%d", dstReg, srcReg1,
               mathText.c_str(), srcReg2);
    }
    break;
  }
  case 0xA: // Store Register to Address
  {
    u8 width = (first_dword >> 24) & 0xF;
    u8 srcReg = (first_dword >> 20) & 0xF;
    u8 addrReg = (first_dword >> 16) & 0xF;
    bool incReg = (first_dword >> 12) & 0xF;
    u8 offType = (first_dword >> 8) & 0xF;
    u8 offReg = (first_dword >> 4) & 0xF;
    
    std::string incNote = "";
    if (incReg) {
      char incBuf[32];
      snprintf(incBuf, sizeof(incBuf), " R%d+=%d", addrReg, width);
      incNote = incBuf;
    }
    
    if (s_noteMinimalMode) {
      if (offType == 0)
        snprintf(buffer, sizeof(buffer), "[R%d] = R%d%s W=%d", addrReg, srcReg, incNote.c_str(), width);
      else if (offType == 1)
        snprintf(buffer, sizeof(buffer), "[R%d+R%d] = R%d%s W=%d", addrReg, offReg,
                 srcReg, incNote.c_str(), width);
      else if (offType == 3)
        snprintf(buffer, sizeof(buffer), "[R%d] = R%d%s W=%d", addrReg, srcReg, incNote.c_str(), width);
      else {
        u64 addr = (((u64)(first_dword & 0xF) << 32) | GetNextDword());
        snprintf(buffer, sizeof(buffer), "[0x%lX] = R%d%s W=%d", addr, srcReg, incNote.c_str(), width);
      }
    } else if (offType == 0)
      snprintf(buffer, sizeof(buffer), "[R%d] = R%d%s (W=%d)", addrReg, srcReg, incNote.c_str(), width);
    else if (offType == 1)
      snprintf(buffer, sizeof(buffer), "[R%d+R%d] = R%d%s (W=%d)", addrReg, offReg,
               srcReg, incNote.c_str(), width);
    else if (offType == 2) {
      u64 addr = (((u64)(first_dword & 0xF) << 32) | GetNextDword());
      snprintf(buffer, sizeof(buffer), "[R%d+0x%lX] = R%d%s (W=%d)", addrReg, addr,
               srcReg, incNote.c_str(), width);
    } else if (offType == 3) {
      u8 mType = offReg;
      std::string heapText = HeapText(mType);
      snprintf(buffer, sizeof(buffer), "[%sR%d] = R%d%s (W=%d)",
               heapText.c_str(), addrReg, srcReg, incNote.c_str(), width);
    } else if (offType >= 4) {
      u8 mType = offReg;
      u64 addr = (((u64)(first_dword & 0xF) << 32) | GetNextDword());
      std::string heapText = HeapText(mType);
      if (offType == 4)
        snprintf(buffer, sizeof(buffer), "[%s0x%lX] = R%d%s (W=%d)",
                 heapText.c_str(), addr, srcReg, incNote.c_str(), width);
      else
        snprintf(buffer, sizeof(buffer), "[%sR%d+0x%lX] = R%d%s (W=%d)",
                 heapText.c_str(), addrReg, addr, srcReg, incNote.c_str(), width);
    }
    break;
  }
  case 0xC0: // Begin Register Conditional Block
  {
    u8 width = (first_dword >> 20) & 0xF;
    u8 cond = (first_dword >> 16) & 0xF;
    u8 valReg = (first_dword >> 12) & 0xF;
    u8 compType = (first_dword >> 8) & 0xF;
    u8 n6 = (first_dword >> 4) & 0xF;
    u8 n7 = first_dword & 0xF;
    
    char condBuf[256];
    if (compType == 0) {
      // Memory Base + Relative Offset
      u64 addr = (((u64)n7 << 32) | GetNextDword());
      std::string condText = ConditionText(cond);
      std::string heapText = HeapText(n6);
      snprintf(condBuf, sizeof(condBuf), "If R%d %s [%s0x%010lX]", valReg,
               condText.c_str(), heapText.c_str(), addr);
    } else if (compType == 1) {
      // Memory Base + Offset Register
      std::string condText = ConditionText(cond);
      std::string heapText = HeapText(n6);
      snprintf(condBuf, sizeof(condBuf), "If R%d %s [%sR%d]", valReg,
               condText.c_str(), heapText.c_str(), n7);
    } else if (compType == 2) {
      // Register + Relative Offset
      u64 addr = (((u64)n7 << 32) | GetNextDword());
      std::string condText = ConditionText(cond);
      snprintf(condBuf, sizeof(condBuf), "If R%d %s [R%d+0x%010lX]", valReg,
               condText.c_str(), n6, addr);
    } else if (compType == 3) {
      // Register + Offset Register
      std::string condText = ConditionText(cond);
      snprintf(condBuf, sizeof(condBuf), "If R%d %s [R%d+R%d]", valReg,
               condText.c_str(), n6, n7);
    } else if (compType == 4) {
      // Static Value
      u64 val = GetNextVmInt(width);
      if (s_noteMinimalMode) {
        snprintf(condBuf, sizeof(condBuf), "If R%d%s", valReg,
                 FormatValueNote(val, width).c_str());
      } else {
        std::string condText = ConditionText(cond);
        snprintf(condBuf, sizeof(condBuf), "If R%d %s 0x%lX%s", valReg,
                 condText.c_str(), val,
                 FormatValueNote(val, width).c_str());
      }
    } else if (compType == 5) {
      // Other Register
      std::string condText = ConditionText(cond);
      snprintf(condBuf, sizeof(condBuf), "If R%d %s R%d", valReg,
               condText.c_str(), n6);
    } else {
      std::string condText = ConditionText(cond);
      snprintf(condBuf, sizeof(condBuf), "If R%d %s [Invalid Compare Type %u]",
               valReg, condText.c_str(), compType);
    }
    snprintf(buffer, sizeof(buffer), "%s (W=%d)", condBuf, width);
    break;
  }
  case 0xC1: // Save/Restore Register
  case 0xC2: // Save/Restore Register Mask
    snprintf(buffer, sizeof(buffer), "Save/Restore Regs");
    break;
  case 0xC3: // Read/Write Static Register
  {
    u8 staticIdx = (first_dword >> 20) & 0xF;
    u8 regIdx = (first_dword >> 16) & 0xF;
    snprintf(buffer, sizeof(buffer), "Static[%d] %s R%d", staticIdx,
             (first_dword & 1) ? "<-" : "->", regIdx);
    break;
  }
  case 0xC4: // Begin Extended Keypress Conditional
  {
    bool autoRepeat = (first_dword >> 20) & 0xF;
    u64 mask = (((u64)GetNextDword()) << 32) | GetNextDword();
    std::string keys = "If keys(";
    bool any = false;
    for (size_t i = 0; i < buttonCodes.size(); i++) {
      const u64 code = static_cast<u64>(buttonCodes[i] & 0x0FFFFFFF);
      if ((mask & code) != 0) {
        if (any) {
          keys += "+";
        }
        keys += buttonNames[i];
        any = true;
      }
    }
    if (!any) {
      keys += "none";
    }
    keys += ")";
    keys += autoRepeat ? " Auto-repeat" : " do once";
    snprintf(buffer, sizeof(buffer), "%s", keys.c_str());
    break;
  }
  case 0xFF0:
    snprintf(buffer, sizeof(buffer), "Pause Process");
    break;
  case 0xFF1:
    snprintf(buffer, sizeof(buffer), "Resume Process");
    break;
  case 0xFFF: // Debug Log
  {
    u8 width = (first_dword >> 16) & 0xF;
    u8 logId = (first_dword >> 12) & 0xF;
    u8 compType = (first_dword >> 8) & 0xF;
    u8 n6 = (first_dword >> 4) & 0xF;
    u8 n7 = first_dword & 0xF;

    char logBuf[256];
    if (compType == 0) {
      u64 addr = (((u64)n7 << 32) | GetNextDword());
      std::string heapText = HeapText(n6);
      snprintf(logBuf, sizeof(logBuf), "Log[%d] Main [%s0x%010lX]", logId,
               heapText.c_str(), addr);
    } else if (compType == 1) {
      std::string heapText = HeapText(n6);
      snprintf(logBuf, sizeof(logBuf), "Log[%d] Main [%sR%d]", logId,
               heapText.c_str(), n7);
    } else if (compType == 2) {
      u64 addr = (((u64)n7 << 32) | GetNextDword());
      snprintf(logBuf, sizeof(logBuf), "Log[%d] Main [R%d+0x%010lX]", logId,
               n6, addr);
    } else if (compType == 3) {
      snprintf(logBuf, sizeof(logBuf), "Log[%d] Main [R%d+R%d]", logId,
               n6, n7);
    } else if (compType == 4) {
      snprintf(logBuf, sizeof(logBuf), "Log[%d] Main R%d", logId, n6);
    } else {
      snprintf(logBuf, sizeof(logBuf), "Log[%d] Main [Invalid Source Type %u]",
               logId, compType);
    }
    snprintf(buffer, sizeof(buffer), "%s (W=%d)", logBuf, width);
    break;
  }

  default:
    snprintf(buffer, sizeof(buffer), "Opcode Type %X", type);
    break;
  }

  if (s_noteMinimalMode && type != 0 && type != 8 && type != 0xC4) {
    return "";
  }
  return buffer;
}

class CheatFormatManager {
private:
  u64 m_storedAddress = 0;        // 40 bits (AAAAA...)
  u64 m_storedRelativeOffset = 0; // 40 bits (aaaaa...)
  u64 m_storedValue = 0;          // 64 bits (VVVVVVVVVVVVVVVV)
  
  char m_nibbleBackup[32] = {0}; 
  u32 m_lastType = 0xFFFFFFFF;
  u8 m_lastWidth = 0;
  bool m_initialized = false;

  u8 vWidth = 4;        // T (0, 1, 5, 6, 7, 9, A, C0, FFF)
  u8 vMemRegion = 0;    // M (0, 1, 5, C0, FFF)
  u8 vRegDest = 15;     // R/D (3, 4, 5, 7, 9, C1)
  u8 vRegSource = 0;    // S (5, 9, A, C0, C1, C3)
  u8 vRegOffset = 1;    // r/s/X (1, 6, 9, A)
  u8 vRegBase = 15;     // R (0, 6, A)
  u8 vCondition = 5;    // C/c (1, C0)
  u8 vArithOp = 0;      // C (7, 9)
  u8 vOpType = 0;       // X/O/x (1, A, C0, C1, C2, FFF)
  u8 vEndType = 0;      // X (2)
  u8 vIncFlag = 0;      // I (6, A)
  u8 vLogID = 0;        // I (FFF)
  u8 vImmFlag = 0;      // X (bit for 9)
  u64 vBitMask = 0;     // key mask (8, C2, C4)
  u8 vStaticIdx = 0;    // XX (C3)
  u8 vCode5Type = 0;    // type 5 sub (5)
  u8 vOffsetEnable = 0; // o (6)
  u8 vAddressNib = 0;   // AA (0, 1, 5)
  u8 vOffsetNib = 0;    // aa (A, C0, FFF)

  static u8 n_nibble(u32 dword, int i) { return (dword >> (28 - i * 4)) & 0xF; }

  void updateStored(const std::vector<u32> &dwords, u32 type) {
    if (dwords.size() < 2) return;
    // 1. Address Tracking
    switch (type) {
      case 0x0: case 0x4: case 0x5:
        m_storedAddress = ((u64)(dwords[0] & 0xFF) << 32) | dwords[1];
        m_storedAddress &= 0xFFFFFFFFFFULL;
        vAddressNib = (u8)(m_storedAddress >> 32);
        break;
      case 0x1:
        m_storedAddress = ((u64)(dwords[0] & 0xFF) << 32) | dwords[1];
        m_storedAddress &= 0xFFFFFFFFFFULL;
        vAddressNib = (u8)(m_storedAddress >> 32);
        break;
    }
    // 2. Relative Offset Tracking
    switch (type) {
      case 0xA:
        if (vOpType == 2 || vOpType == 4 || vOpType == 5) {
          m_storedRelativeOffset = ((u64)(dwords[0] & 0xF) << 32) | dwords[1];
          m_storedRelativeOffset &= 0xFFFFFFFFFFULL;
          vOffsetNib = (u8)(m_storedRelativeOffset >> 32);
        }
        break;
      case 0xC0: case 0xFFF:
        m_storedRelativeOffset = ((u64)(dwords[0] & 0xF) << 32) | dwords[1];
        m_storedRelativeOffset &= 0xFFFFFFFFFFULL;
        vOffsetNib = (u8)(m_storedRelativeOffset >> 32);
        break;
    }
    // 3. Value Tracking (VVVVV...)
    switch (type) {
      case 0x0: case 0x1:
        if (vWidth == 8 && dwords.size() >= 4) m_storedValue = (((u64)dwords[2]) << 32) | dwords[3];
        else if (vWidth == 4 && dwords.size() >= 3) m_storedValue = (m_storedValue & 0xFFFFFFFF00000000ULL) | dwords[2];
        else if (vWidth == 2 && dwords.size() >= 3) m_storedValue = (m_storedValue & 0xFFFFFFFFFFFF0000ULL) | (dwords[2] & 0xFFFF);
        else if (vWidth == 1 && dwords.size() >= 3) m_storedValue = (m_storedValue & 0xFFFFFFFFFFFFFF00ULL) | (dwords[2] & 0xFF);
        break;
      case 0xC0:
        if (vOpType == 4) {
          if (vWidth == 8 && dwords.size() >= 3) m_storedValue = (((u64)dwords[1]) << 32) | dwords[2];
          else if (dwords.size() >= 2) m_storedValue = (m_storedValue & 0xFFFFFFFF00000000ULL) | dwords[1];
        }
        break;
      case 0x4: case 0x6:
        if (dwords.size() >= 3) m_storedValue = (((u64)dwords[1]) << 32) | dwords[2];
        break;
      case 0x3: case 0x7: case 0x9:
        if (vWidth == 8 && dwords.size() >= 3) m_storedValue = (((u64)dwords[1]) << 32) | dwords[2];
        else if (dwords.size() >= 2) m_storedValue = (m_storedValue & 0xFFFFFFFF00000000ULL) | dwords[1];
        break;
    }
  }

  void applyStored(std::vector<u32> &dwords, u32 type) {
    if (dwords.size() < 1) return;
    // 1. Address Apply
    switch (type) {
      case 0x0: case 0x4: case 0x5:
        vAddressNib = (u8)(m_storedAddress >> 32);
        dwords[0] = (dwords[0] & 0xFFFFFF00) | (u32)vAddressNib;
        if (dwords.size() > 1) dwords[1] = (u32)(m_storedAddress & 0xFFFFFFFF);
        break;
      case 0x1:
        vAddressNib = (u8)(m_storedAddress >> 32);
        dwords[0] = (dwords[0] & 0xFFFFFF00) | (u32)vAddressNib;
        if (dwords.size() > 1) dwords[1] = (u32)(m_storedAddress & 0xFFFFFFFF);
        break;
    }
    // 2. Relative Offset Apply
    switch (type) {
      case 0xA:
        if (vOpType == 2 || vOpType == 4 || vOpType == 5) {
          vOffsetNib = (u8)(m_storedRelativeOffset >> 32) & 0xF;
          dwords[0] = (dwords[0] & 0xFFFFFFF0) | (u32)vOffsetNib;
          if (dwords.size() > 1) dwords[1] = (u32)(m_storedRelativeOffset & 0xFFFFFFFF);
        }
        break;
      case 0xC0: case 0xFFF:
        if (vOpType == 0 || vOpType == 2) {
          vOffsetNib = (u8)(m_storedRelativeOffset >> 32) & 0xF;
          dwords[0] = (dwords[0] & 0xFFFFFFF0) | (u32)vOffsetNib;
          if (dwords.size() > 1) dwords[1] = (u32)(m_storedRelativeOffset & 0xFFFFFFFF);
        }
        break;
    }
    // 3. Value Apply
    switch (type) {
      case 0x0: case 0x1:
        if (vWidth == 8 && dwords.size() >= 4) { dwords[2] = (u32)(m_storedValue >> 32); dwords[3] = (u32)(m_storedValue & 0xFFFFFFFF); }
        else if (dwords.size() >= 3) {
            if (vWidth == 4) dwords[2] = (u32)(m_storedValue & 0xFFFFFFFF);
            else if (vWidth == 2) dwords[2] = (u32)(m_storedValue & 0xFFFF);
            else if (vWidth == 1) dwords[2] = (u32)(m_storedValue & 0xFF);
        }
        break;
      case 0xC0:
        if (vOpType == 4) {
          if (vWidth == 8 && dwords.size() >= 3) { dwords[1] = (u32)(m_storedValue >> 32); dwords[2] = (u32)(m_storedValue & 0xFFFFFFFF); }
          else if (dwords.size() >= 2) { dwords[1] = (u32)(m_storedValue & 0xFFFFFFFF); }
        }
        break;
      case 0x4: case 0x6:
        if (dwords.size() >= 3) { dwords[1] = (u32)(m_storedValue >> 32); dwords[2] = (u32)(m_storedValue & 0xFFFFFFFF); }
        break;
      case 0x3: case 0x7: case 0x9:
        if (vWidth == 8 && dwords.size() >= 3) { dwords[1] = (u32)(m_storedValue >> 32); dwords[2] = (u32)(m_storedValue & 0xFFFFFFFF); }
        else if (dwords.size() >= 2) { dwords[1] = (u32)(m_storedValue & 0xFFFFFFFF); }
        break;
    }
  }

  void syncVariables(u32 dword, u32 type) {
    auto n = [&](int i) -> u8 { return n_nibble(dword, i); };
    if (type < 0xC) {
      if (type == 0x0) { vWidth = n(1); vMemRegion = n(2); vRegBase = n(3); vAddressNib = (n(6) << 4) | n(7); }
      else if (type == 0x1) { vWidth = n(1); vMemRegion = n(2); vCondition = n(3); vOpType = n(4); vRegOffset = n(5); vAddressNib = (n(6) << 4) | n(7); }
      else if (type == 0x2) { vEndType = n(1); }
      else if (type == 0x3) { vOpType = n(1); vRegDest = n(3); } // OpType 0=Start, 1=End
      else if (type == 0x4) { vRegDest = n(3); }
      else if (type == 0x5) { 
          vWidth = n(1);
          vRegDest = n(3); vCode5Type = n(4);
          if (vCode5Type == 0 || vCode5Type == 3) vMemRegion = n(2);
          if (vCode5Type == 2 || vCode5Type == 3) vRegSource = n(5);
          vAddressNib = (n(6) << 4) | n(7);
      }
      else if (type == 0x6) { vWidth = n(1); vRegBase = n(3); vIncFlag = n(4); vOffsetEnable = n(5); vRegOffset = n(6); }
      else if (type == 0x7) { vWidth = n(1); vRegDest = n(3); vArithOp = n(4); }
      else if (type == 0x8) { vBitMask = static_cast<u64>(dword & 0x0FFFFFFF); }
      else if (type == 0x9) { vWidth = n(1); vArithOp = n(2); vRegDest = n(3); vRegSource = n(4); vImmFlag = n(5); vRegOffset = n(6); }
      else if (type == 0xA) { vWidth = n(1); vRegSource = n(2); vRegBase = n(3); vIncFlag = n(4); vOpType = n(5); vRegOffset = n(6); vOffsetNib = n(7); }
    } else if (type == 0xC0) {
      vWidth = n(2); vCondition = n(3); vRegSource = n(4); vOpType = n(5);
      if (vOpType == 2 || vOpType == 3 || vOpType == 5) vRegBase = n(6); else vMemRegion = n(6);
      if (vOpType == 1 || vOpType == 3) vRegOffset = n(7); else vOffsetNib = n(7);
    } else if (type == 0xC1) {
      vRegDest = n(3); vRegSource = n(5); vOpType = n(6);
    } else if (type == 0xC2) {
      vOpType = n(1); vBitMask = static_cast<u64>(dword & 0xFFFF);
    } else if (type == 0xC4) {
      vOpType = n(2);
    } else if (type == 0xC3) {
      vStaticIdx = (u8)((dword >> 4) & 0xFF); vRegSource = n(7);
    } else if (type == 0xFFF) {
      vWidth = n(3); vLogID = n(4); vOpType = n(5);
      if (vOpType == 2 || vOpType == 3 || vOpType == 4) vRegBase = n(6); else vMemRegion = n(6);
      if (vOpType == 1 || vOpType == 3) vRegOffset = n(7); else vOffsetNib = n(7);
    }
  }

  void applyVariables(u32 &dword, u32 type) {
    auto set = [&](int i, u8 val) { dword = (dword & ~(0xF << (28 - i * 4))) | ((u32)(val & 0xF) << (28 - i * 4)); };
    if (type < 0xC) {
      set(1, vWidth);
      if (type == 0x0) { set(2, vMemRegion); set(3, vRegBase); set(4, 0); set(5, 0); set(6, vAddressNib >> 4); set(7, vAddressNib); }
      else if (type == 0x1) { set(2, vMemRegion); set(3, vCondition); set(4, vOpType); set(5, vRegOffset); set(6, vAddressNib >> 4); set(7, vAddressNib); }
      else if (type == 0x2) { set(1, vEndType); for(int i=2; i<8; i++) set(i, 0); }
      else if (type == 0x3) { set(1, vOpType); set(2, 0); set(3, vRegDest); for(int i=4; i<8; i++) set(i, 0); }
      else if (type == 0x4) { set(1, 0); set(2, 0); set(3, vRegDest); for(int i=4; i<8; i++) set(i, 0); }
      else if (type == 0x5) { 
          set(2, (vCode5Type == 0 || vCode5Type == 3) ? vMemRegion : 0);
          set(3, vRegDest); set(4, vCode5Type);
          set(5, (vCode5Type == 2 || vCode5Type == 3) ? vRegSource : 0);
          set(6, vAddressNib >> 4); set(7, vAddressNib);
      }
      else if (type == 0x6) { set(2, 0); set(3, vRegBase); set(4, vIncFlag); set(5, vOffsetEnable); set(6, vRegOffset); set(7, 0); }
      else if (type == 0x7) { set(2, 0); set(3, vRegDest); set(4, vArithOp); set(5, 0); set(6, 0); set(7, 0); }
      else if (type == 0x8) {
        dword = (0x80000000 | static_cast<u32>(vBitMask & 0x0FFFFFFF));
      }
      else if (type == 0x9) { set(2, vArithOp); set(3, vRegDest); set(4, vRegSource); set(5, vImmFlag); set(6, vRegOffset); set(7, 0); }
      else if (type == 0xA) { set(2, vRegSource); set(3, vRegBase); set(4, vIncFlag); set(5, vOpType); set(6, vRegOffset); set(7, vOffsetNib); }
    } else if (type == 0xC0) {
      set(2, vWidth); set(3, vCondition); set(4, vRegSource); set(5, vOpType);
      set(6, (vOpType == 2 || vOpType == 3 || vOpType == 5) ? vRegBase : vMemRegion);
      set(7, (vOpType == 1 || vOpType == 3) ? vRegOffset : vOffsetNib);
    } else if (type == 0xC1) {
      set(2, 0); set(3, vRegDest); set(4, 0); set(5, vRegSource); set(6, vOpType); set(7, 0);
    } else if (type == 0xC2) {
      dword = (0xC2000000 | ((u32)(vOpType & 0xF) << 20) |
               static_cast<u32>(vBitMask & 0xFFFF));
    } else if (type == 0xC4) {
      dword = (0xC4000000 | ((u32)(vOpType & 0xF) << 20));
    } else if (type == 0xC3) {
      dword = (0xC3000000 | ((u32)vStaticIdx << 4) | (vRegSource & 0xF));
    } else if (type == 0xFFF) {
      set(3, vWidth); set(4, vLogID); set(5, vOpType);
      set(6, (vOpType == 2 || vOpType == 3 || vOpType == 4) ? vRegBase : vMemRegion);
      set(7, (vOpType == 1 || vOpType == 3) ? vRegOffset : vOffsetNib);
    }
  }

  static std::string trimCopy(const std::string &s) {
    size_t b = 0;
    while (b < s.size() && std::isspace((unsigned char)s[b])) b++;
    size_t e = s.size();
    while (e > b && std::isspace((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
  }

  static std::string normalizeAsmText(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    bool prevSpace = false;
    for (char c : s) {
      const unsigned char uc = static_cast<unsigned char>(c);
      if (std::isspace(uc)) {
        if (!out.empty() && !prevSpace) {
          out.push_back(' ');
          prevSpace = true;
        }
        continue;
      }
      const char lc = static_cast<char>(std::tolower(uc));
      if (lc == ',') {
        if (!out.empty() && out.back() == ' ')
          out.pop_back();
        out.push_back(',');
        prevSpace = false;
        continue;
      }
      out.push_back(lc);
      prevSpace = false;
    }
    while (!out.empty() && out.back() == ' ')
      out.pop_back();
    return out;
  }

  static bool tryParseRegisterToken(const std::string &tok, u8 &reg) {
    if (tok.size() < 2 || tok[0] != 'x')
      return false;
    char *end = nullptr;
    errno = 0;
    long n = std::strtol(tok.c_str() + 1, &end, 10);
    if (errno != 0 || !end || *end != '\0' || n < 0 || n > 30)
      return false;
    reg = static_cast<u8>(n);
    return true;
  }

  static bool tryParseGprToken(const std::string &tok, char cls, u8 &reg, bool allow31 = false) {
    if (tok.size() < 2 || tok[0] != cls)
      return false;
    char *end = nullptr;
    errno = 0;
    long n = std::strtol(tok.c_str() + 1, &end, 10);
    const long maxReg = allow31 ? 31 : 30;
    if (errno != 0 || !end || *end != '\0' || n < 0 || n > maxReg)
      return false;
    reg = static_cast<u8>(n);
    return true;
  }

  static bool tryParseBaseRegToken(const std::string &tok, u8 &reg) {
    if (tok == "sp") {
      reg = 31;
      return true;
    }
    return tryParseGprToken(tok, 'x', reg, true);
  }

  static bool tryParseFpToken(const std::string &tok, char cls, u8 &reg) {
    if (tok.size() < 2 || tok[0] != cls)
      return false;
    char *end = nullptr;
    errno = 0;
    long n = std::strtol(tok.c_str() + 1, &end, 10);
    if (errno != 0 || !end || *end != '\0' || n < 0 || n > 31)
      return false;
    reg = static_cast<u8>(n);
    return true;
  }

  static bool tryParseSImm(const std::string &tok, long long &imm) {
    std::string t = tok;
    if (!t.empty() && t[0] == '#')
      t.erase(0, 1);
    if (t.empty())
      return false;
    char *end = nullptr;
    errno = 0;
    long long v = std::strtoll(t.c_str(), &end, 0);
    if (errno != 0 || !end || *end != '\0')
      return false;
    imm = v;
    return true;
  }

  static bool tryParseFloatLiteral(const std::string &tok, double &val) {
    std::string t = tok;
    if (!t.empty() && t[0] == '#')
      t.erase(0, 1);
    t = trimCopy(t);
    if (t.empty())
      return false;
    char *end = nullptr;
    errno = 0;
    double v = std::strtod(t.c_str(), &end);
    if (errno != 0 || !end || *end != '\0' || !std::isfinite(v))
      return false;
    val = v;
    return true;
  }

  static bool tryParseUImm(const std::string &tok, unsigned long &imm, unsigned long maxVal) {
    std::string t = tok;
    if (!t.empty() && t[0] == '#')
      t.erase(0, 1);
    if (t.empty())
      return false;
    char *end = nullptr;
    errno = 0;
    unsigned long v = std::strtoul(t.c_str(), &end, 0);
    if (errno != 0 || !end || *end != '\0' || v > maxVal)
      return false;
    imm = v;
    return true;
  }

  static bool tryParseHexOpcode(const std::string &input, u32 &opcodeOut) {
    std::string t = trimCopy(input);
    if (t.empty())
      return false;

    std::string clean;
    clean.reserve(t.size());
    for (char c : t) {
      if (!std::isspace(static_cast<unsigned char>(c)) && c != '_')
        clean.push_back(c);
    }

    if (clean.size() > 2 && clean[0] == '0' &&
        (clean[1] == 'x' || clean[1] == 'X')) {
      clean.erase(0, 2);
    }

    if (clean.empty() || clean.size() > 8)
      return false;
    for (char c : clean) {
      if (!std::isxdigit(static_cast<unsigned char>(c)))
        return false;
    }

    char *end = nullptr;
    errno = 0;
    unsigned long v = std::strtoul(clean.c_str(), &end, 16);
    if (errno != 0 || !end || *end != '\0')
      return false;
    opcodeOut = static_cast<u32>(v);
    return true;
  }

public:
  static bool tryAssembleWithKeystone(const std::string &asmText, u64 address, u32 &opcodeOut) {
#if HAVE_KEYSTONE_ASM
    // Keep memory footprint low:
    // - lazy one-time engine creation
    // - single-instruction assembly only
    // - free encoded buffer immediately
    static ks_engine *ks = nullptr;
    static bool initAttempted = false;
    static bool initOk = false;

    if (asmText == "__CLEANUP__") {
      if (ks) {
        ks_close(ks);
        ks = nullptr;
      }
      initAttempted = false;
      initOk = false;
      return true;
    }

    if (!initAttempted) {
      initAttempted = true;
      if (ks_open(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN, &ks) == KS_ERR_OK) {
        initOk = true;
      }
    }
    if (!initOk || !ks || asmText.empty() || asmText.size() > 160)
      return false;

    unsigned char *encode = nullptr;
    size_t size = 0;
    size_t count = 0;
    if (ks_asm(ks, asmText.c_str(), address, &encode, &size, &count) != KS_ERR_OK)
      return false;

    const bool ok = (count == 1 && size == 4 && encode != nullptr);
    if (ok) {
      opcodeOut = static_cast<u32>(encode[0]) |
                  (static_cast<u32>(encode[1]) << 8) |
                  (static_cast<u32>(encode[2]) << 16) |
                  (static_cast<u32>(encode[3]) << 24);
    }
    ks_free(encode);
    return ok;
#else
    (void)asmText;
    (void)address;
    (void)opcodeOut;
    return false;
#endif
  }

  bool tryAssembleArm64(const std::string &input, u32 &opcodeOut) const {
    // Always allow raw opcode entry as a fallback assembly path.
    if (tryParseHexOpcode(input, opcodeOut))
      return true;

    const std::string normInput = normalizeAsmText(input);
    if (normInput.empty())
      return false;

    // If user kept the disassembled text (possibly with spacing/case edits), keep the same opcode.
    const std::string dis = DisassembleARM64(static_cast<u32>(m_storedValue), m_storedAddress);
    if (!dis.empty() && normalizeAsmText(dis) == normInput) {
      opcodeOut = static_cast<u32>(m_storedValue);
      return true;
    }

    // Optional broad-coverage assembler backend.
    // Falls back to the built-in subset if unavailable or failing.
    if (tryAssembleWithKeystone(input, m_storedAddress, opcodeOut))
      return true;

    // Minimal built-in assembler for common instruction forms.
    if (normInput == "nop")   { opcodeOut = 0xD503201F; return true; }
    if (normInput == "yield") { opcodeOut = 0xD503203F; return true; }
    if (normInput == "wfe")   { opcodeOut = 0xD503205F; return true; }
    if (normInput == "wfi")   { opcodeOut = 0xD503207F; return true; }
    if (normInput == "sev")   { opcodeOut = 0xD503209F; return true; }
    if (normInput == "sevl")  { opcodeOut = 0xD50320BF; return true; }
    if (normInput == "ret")   { opcodeOut = 0xD65F03C0; return true; }

    auto parseSingleRegOp = [&](const char *mnemonic, u32 base) -> bool {
      const std::string prefix = std::string(mnemonic) + " ";
      if (normInput.rfind(prefix, 0) != 0)
        return false;
      const std::string tok = trimCopy(normInput.substr(prefix.size()));
      u8 reg = 0;
      if (!tryParseRegisterToken(tok, reg))
        return false;
      opcodeOut = base | (static_cast<u32>(reg) << 5);
      return true;
    };

    if (parseSingleRegOp("ret", 0xD65F0000)) return true;
    if (parseSingleRegOp("br",  0xD61F0000)) return true;
    if (parseSingleRegOp("blr", 0xD63F0000)) return true;

    auto parseTrapOp = [&](const char *mnemonic, u32 base) -> bool {
      const std::string prefix = std::string(mnemonic) + " ";
      if (normInput.rfind(prefix, 0) != 0)
        return false;
      unsigned long imm = 0;
      if (!tryParseUImm(trimCopy(normInput.substr(prefix.size())), imm, 0xFFFF))
        return false;
      opcodeOut = base | (static_cast<u32>(imm) << 5);
      return true;
    };

    if (parseTrapOp("svc", 0xD4000001)) return true;
    if (parseTrapOp("hvc", 0xD4000002)) return true;
    if (parseTrapOp("smc", 0xD4000003)) return true;
    if (parseTrapOp("brk", 0xD4200000)) return true;
    if (parseTrapOp("hlt", 0xD4400000)) return true;

    auto splitOperands = [](const std::string &ops) -> std::vector<std::string> {
      std::vector<std::string> out;
      std::string cur;
      for (char c : ops) {
        if (c == ',') {
          out.push_back(trimCopy(cur));
          cur.clear();
        } else {
          cur.push_back(c);
        }
      }
      if (!cur.empty())
        out.push_back(trimCopy(cur));
      return out;
    };

    auto stripSpaces = [](const std::string &s) -> std::string {
      std::string out;
      out.reserve(s.size());
      for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c)))
          out.push_back(c);
      }
      return out;
    };

    auto parseShiftSpec = [&](const std::string &tok, u32 &shiftType, u32 &shiftAmount, u32 maxShift) -> bool {
      std::string s;
      s.reserve(tok.size());
      for (char c : tok) {
        if (!std::isspace(static_cast<unsigned char>(c)))
          s.push_back(c);
      }
      if (s.rfind("lsl#", 0) == 0) shiftType = 0;
      else if (s.rfind("lsr#", 0) == 0) shiftType = 1;
      else if (s.rfind("asr#", 0) == 0) shiftType = 2;
      else return false;

      long long sh = 0;
      if (!tryParseSImm(s.substr(3), sh))
        return false;
      if (sh < 0 || sh > static_cast<long long>(maxShift))
        return false;
      shiftAmount = static_cast<u32>(sh);
      return true;
    };

    auto parseAddSubCommon = [&](bool isSub, bool isCmp, bool setFlags) -> bool {
      const std::string mnemonic =
          isCmp ? "cmp " : (isSub ? (setFlags ? "subs " : "sub ")
                                  : (setFlags ? "adds " : "add "));
      if (normInput.rfind(mnemonic, 0) != 0)
        return false;

      const std::vector<std::string> ops = splitOperands(trimCopy(normInput.substr(mnemonic.size())));
      if ((!isCmp && (ops.size() < 3 || ops.size() > 4)) || (isCmp && (ops.size() < 2 || ops.size() > 3)))
        return false;

      const size_t idxRn = isCmp ? 0 : 1;
      const size_t idxOp2 = isCmp ? 1 : 2;
      const size_t idxShift = isCmp ? 2 : 3;

      u8 rd = 31; // cmp aliases to xzr/wzr
      u8 rn = 0;
      bool isX = false;

      if (!isCmp) {
        if (tryParseGprToken(ops[0], 'x', rd, false)) isX = true;
        else if (tryParseGprToken(ops[0], 'w', rd, false)) isX = false;
        else return false;
      }

      u8 rnX = 0, rnW = 0;
      const bool rnIsX = tryParseGprToken(ops[idxRn], 'x', rnX, false);
      const bool rnIsW = tryParseGprToken(ops[idxRn], 'w', rnW, false);
      if (!rnIsX && !rnIsW) return false;
      rn = rnIsX ? rnX : rnW;
      if (isCmp) isX = rnIsX;
      else if ((isX && !rnIsX) || (!isX && !rnIsW)) return false;

      const std::string op2 = ops[idxOp2];
      const bool isImm = !op2.empty() && op2[0] == '#';
      if (isImm) {
        unsigned long imm = 0;
        if (!tryParseUImm(op2, imm, 0xFFFFFF))
          return false;

        u32 sh = 0;
        if ((isCmp && ops.size() == 3) || (!isCmp && ops.size() == 4)) {
          std::string shTok = stripSpaces(ops[idxShift]);
          if (shTok != "lsl#12")
            return false;
          sh = 1;
        }

        if (sh == 0 && imm > 0xFFFul) return false;
        if (sh == 1 && (imm > 0xFFF000ul || (imm & 0xFFFul) != 0)) return false;
        const u32 imm12 = static_cast<u32>(sh ? (imm >> 12) : imm);

        u32 base = 0;
        if (isCmp) base = isX ? 0xF100001Fu : 0x7100001Fu;
        else if (!isSub) base = isX ? (setFlags ? 0xB1000000u : 0x91000000u)
                                    : (setFlags ? 0x31000000u : 0x11000000u);
        else base = isX ? (setFlags ? 0xF1000000u : 0xD1000000u)
                        : (setFlags ? 0x71000000u : 0x51000000u);

        opcodeOut = base | (sh << 22) | (imm12 << 10) | (static_cast<u32>(rn) << 5) | rd;
        return true;
      }

      u8 rm = 0;
      if (isX) {
        if (!tryParseGprToken(op2, 'x', rm, false)) return false;
      } else {
        if (!tryParseGprToken(op2, 'w', rm, false)) return false;
      }

      u32 shiftType = 0, shiftAmount = 0;
      if ((isCmp && ops.size() == 3) || (!isCmp && ops.size() == 4)) {
        if (!parseShiftSpec(ops[idxShift], shiftType, shiftAmount, isX ? 63 : 31))
          return false;
      }

      u32 base = 0;
      if (isCmp) base = isX ? 0xEB00001Fu : 0x6B00001Fu;
      else if (!isSub) base = isX ? (setFlags ? 0xAB000000u : 0x8B000000u)
                                  : (setFlags ? 0x2B000000u : 0x0B000000u);
      else base = isX ? (setFlags ? 0xEB000000u : 0xCB000000u)
                      : (setFlags ? 0x6B000000u : 0x4B000000u);

      opcodeOut = base | (shiftType << 22) | (static_cast<u32>(rm) << 16) |
                  (shiftAmount << 10) | (static_cast<u32>(rn) << 5) | rd;
      return true;
    };

    auto parseCondBranch = [&]() -> bool {
      if (normInput.rfind("b.", 0) != 0)
        return false;
      const size_t sp = normInput.find(' ');
      if (sp == std::string::npos)
        return false;
      const std::string cond = normInput.substr(2, sp - 2);
      const std::string op = trimCopy(normInput.substr(sp + 1));
      if (op.empty())
        return false;

      int condCode = -1;
      if (cond == "eq") condCode = 0;
      else if (cond == "ne") condCode = 1;
      else if (cond == "lo") condCode = 3;
      else if (cond == "hi") condCode = 8;
      else if (cond == "lt") condCode = 11;
      else if (cond == "gt") condCode = 12;
      else return false;

      long long offset = 0;
      const bool looksAbsolute =
          (op.rfind("0x", 0) == 0) || (op.rfind("#0x", 0) == 0);
      if (looksAbsolute) {
        long long target = 0;
        if (!tryParseSImm(op, target))
          return false;
        offset = target - static_cast<long long>(m_storedAddress);
      } else {
        if (!tryParseSImm(op, offset))
          return false;
      }

      if ((offset & 0x3) != 0)
        return false;
      if (offset < -(1LL << 20) || offset > ((1LL << 20) - 4))
        return false;

      const s32 imm19 = static_cast<s32>(offset >> 2);
      opcodeOut = 0x54000000u | ((static_cast<u32>(imm19) & 0x7FFFFu) << 5) |
                  static_cast<u32>(condCode);
      return true;
    };

    if (parseAddSubCommon(false, false, false)) return true; // add
    if (parseAddSubCommon(false, false, true)) return true;  // adds
    if (parseAddSubCommon(true, false, false)) return true;  // sub
    if (parseAddSubCommon(true, false, true)) return true;   // subs
    if (parseAddSubCommon(true, true, true)) return true;    // cmp
    if (parseCondBranch()) return true;

    auto parseBranchImm = [&](const char *mnemonic, u32 base) -> bool {
      const std::string prefix = std::string(mnemonic) + " ";
      if (normInput.rfind(prefix, 0) != 0)
        return false;
      const std::string op = trimCopy(normInput.substr(prefix.size()));
      if (op.empty())
        return false;

      long long offset = 0;
      const bool looksAbsolute =
          (op.rfind("0x", 0) == 0) || (op.rfind("#0x", 0) == 0);
      if (looksAbsolute) {
        long long target = 0;
        if (!tryParseSImm(op, target))
          return false;
        offset = target - static_cast<long long>(m_storedAddress);
      } else {
        if (!tryParseSImm(op, offset))
          return false;
      }

      if ((offset & 0x3) != 0)
        return false;
      if (offset < -(1LL << 27) || offset > ((1LL << 27) - 4))
        return false;

      const s32 imm26 = static_cast<s32>(offset >> 2);
      opcodeOut = base | (static_cast<u32>(imm26) & 0x03FFFFFFu);
      return true;
    };

    if (parseBranchImm("b", 0x14000000)) return true;
    if (parseBranchImm("bl", 0x94000000)) return true;

    auto parseLoadStore = [&](const std::string &mnemonic, bool isLoad) -> bool {
      const std::string prefix = mnemonic + " ";
      if (normInput.rfind(prefix, 0) != 0)
        return false;

      const std::string rest = trimCopy(normInput.substr(prefix.size()));
      const size_t commaPos = rest.find(',');
      if (commaPos == std::string::npos)
        return false;

      const std::string rtTok = trimCopy(rest.substr(0, commaPos));
      std::string memTok = trimCopy(rest.substr(commaPos + 1));

      // LDR literal form: ldr wN/xN, #imm_or_abs
      if (mnemonic == "ldr" && isLoad && !memTok.empty() && memTok.front() == '#') {
        u8 rt = 0;
        u32 base = 0;
        if (tryParseGprToken(rtTok, 'w', rt, false)) {
          base = 0x18000000u; // LDR (literal, 32-bit)
        } else if (tryParseGprToken(rtTok, 'x', rt, false)) {
          base = 0x58000000u; // LDR (literal, 64-bit)
        } else {
          return false;
        }

        long long offset = 0;
        const bool looksAbsolute =
            (memTok.rfind("#0x", 0) == 0) || (memTok.rfind("#0X", 0) == 0);
        if (looksAbsolute) {
          long long target = 0;
          if (!tryParseSImm(memTok, target))
            return false;
          offset = target - static_cast<long long>(m_storedAddress);
        } else {
          if (!tryParseSImm(memTok, offset))
            return false;
        }

        if ((offset & 0x3) != 0)
          return false;
        if (offset < -(1LL << 20) || offset > ((1LL << 20) - 4))
          return false;

        const s32 imm19 = static_cast<s32>(offset >> 2);
        opcodeOut = base | ((static_cast<u32>(imm19) & 0x7FFFFu) << 5) | rt;
        return true;
      }

      memTok = stripSpaces(memTok);

      if (memTok.size() < 3 || memTok.front() != '[' || memTok.back() != ']')
        return false;

      const std::string inner = memTok.substr(1, memTok.size() - 2);
      const size_t innerComma = inner.find(',');
      std::string rnTok = inner;
      std::string immTok;
      if (innerComma != std::string::npos) {
        rnTok = inner.substr(0, innerComma);
        immTok = inner.substr(innerComma + 1);
      }

      u8 rt = 0, rn = 0;
      u32 base = 0;
      u32 scale = 0;
      const bool wantByte = (mnemonic == "ldrb");
      const bool wantHalf = (mnemonic == "ldrh");

      if (wantByte) {
        if (!isLoad || !tryParseGprToken(rtTok, 'w', rt, false))
          return false;
        base = 0x39400000u; // LDRB (unsigned immediate)
        scale = 1;
      } else if (wantHalf) {
        if (!isLoad || !tryParseGprToken(rtTok, 'w', rt, false))
          return false;
        base = 0x79400000u; // LDRH (unsigned immediate)
        scale = 2;
      } else {
        if (tryParseGprToken(rtTok, 'x', rt, false)) {
          base = isLoad ? 0xF9400000u : 0xF9000000u;
          scale = 8;
        } else if (tryParseGprToken(rtTok, 'w', rt, false)) {
          base = isLoad ? 0xB9400000u : 0xB9000000u;
          scale = 4;
        } else {
          return false;
        }
      }

      if (!tryParseBaseRegToken(rnTok, rn))
        return false;

      unsigned long immBytes = 0;
      if (!immTok.empty()) {
        if (!tryParseUImm(immTok, immBytes, 4095ul * scale))
          return false;
      }
      if ((immBytes % scale) != 0)
        return false;

      const u32 imm12 = static_cast<u32>(immBytes / scale);
      opcodeOut = base | (imm12 << 10) | (static_cast<u32>(rn) << 5) | rt;
      return true;
    };

    if (parseLoadStore("ldr", true)) return true;
    if (parseLoadStore("ldrb", true)) return true;
    if (parseLoadStore("ldrh", true)) return true;
    if (parseLoadStore("str", false)) return true;
    if (parseLoadStore("ld", true)) return true;
    if (parseLoadStore("st", false)) return true;

    auto parsePairLoadStore = [&](const std::string &mnemonic, bool isLoad) -> bool {
      const std::string prefix = mnemonic + " ";
      if (normInput.rfind(prefix, 0) != 0)
        return false;

      const std::string rest = trimCopy(normInput.substr(prefix.size()));
      const size_t c1 = rest.find(',');
      if (c1 == std::string::npos)
        return false;
      const size_t c2 = rest.find(',', c1 + 1);
      if (c2 == std::string::npos)
        return false;

      const std::string rt1Tok = trimCopy(rest.substr(0, c1));
      const std::string rt2Tok = trimCopy(rest.substr(c1 + 1, c2 - c1 - 1));
      const std::string addrTok = trimCopy(rest.substr(c2 + 1));
      if (addrTok.empty() || addrTok.front() != '[')
        return false;

      const size_t rb = addrTok.find(']');
      if (rb == std::string::npos)
        return false;

      const std::string inside = addrTok.substr(1, rb - 1);
      std::string tail = trimCopy(addrTok.substr(rb + 1));
      const bool preIndex = (!tail.empty() && tail == "!");
      const bool postIndex = (!tail.empty() && !preIndex);

      std::string baseTok;
      std::string immInsideTok;
      {
        const size_t ic = inside.find(',');
        if (ic == std::string::npos) {
          baseTok = trimCopy(inside);
        } else {
          baseTok = trimCopy(inside.substr(0, ic));
          immInsideTok = trimCopy(inside.substr(ic + 1));
        }
      }
      if (baseTok.empty())
        return false;

      u8 rt1 = 0, rt2 = 0, rn = 0;
      bool isX = false;
      u32 scale = 0;
      if (tryParseGprToken(rt1Tok, 'x', rt1, false) &&
          tryParseGprToken(rt2Tok, 'x', rt2, false)) {
        isX = true;
        scale = 8;
      } else if (tryParseGprToken(rt1Tok, 'w', rt1, false) &&
                 tryParseGprToken(rt2Tok, 'w', rt2, false)) {
        isX = false;
        scale = 4;
      } else {
        return false;
      }

      if (!tryParseBaseRegToken(baseTok, rn))
        return false;

      long long immBytes = 0;
      if (postIndex) {
        // Post-index form: [xN], #imm
        if (!immInsideTok.empty())
          return false;
        if (tail.front() != ',')
          return false;
        std::string postTok = trimCopy(tail.substr(1));
        if (!tryParseSImm(postTok, immBytes))
          return false;
      } else {
        // Offset/pre-index forms: [xN] / [xN,#imm] / [xN,#imm]!
        if (!immInsideTok.empty()) {
          if (!tryParseSImm(immInsideTok, immBytes))
            return false;
        }
      }

      if ((immBytes % static_cast<long long>(scale)) != 0)
        return false;
      const long long scaled = immBytes / static_cast<long long>(scale);
      if (scaled < -64 || scaled > 63)
        return false;

      const u32 imm7 = static_cast<u32>(scaled) & 0x7Fu;
      u32 base = 0;
      if (isX) {
        if (isLoad) {
          if (postIndex) base = 0xA8C00000u;
          else if (preIndex) base = 0xA9C00000u;
          else base = 0xA9400000u;
        } else {
          if (postIndex) base = 0xA8800000u;
          else if (preIndex) base = 0xA9800000u;
          else base = 0xA9000000u;
        }
      } else {
        if (isLoad) {
          if (postIndex) base = 0x28C00000u;
          else if (preIndex) base = 0x29C00000u;
          else base = 0x29400000u;
        } else {
          if (postIndex) base = 0x28800000u;
          else if (preIndex) base = 0x29800000u;
          else base = 0x29000000u;
        }
      }

      opcodeOut = base | (imm7 << 15) | (static_cast<u32>(rt2) << 10) |
                  (static_cast<u32>(rn) << 5) | static_cast<u32>(rt1);
      return true;
    };

    if (parsePairLoadStore("ldp", true)) return true;
    if (parsePairLoadStore("stp", false)) return true;

    if (normInput.rfind("mov ", 0) == 0) {
      const std::vector<std::string> ops = splitOperands(trimCopy(normInput.substr(4)));
      if (ops.size() == 2 || ops.size() == 3) {
        u8 rd = 0, rm = 0;
        if (tryParseGprToken(ops[0], 'x', rd, false) &&
            tryParseGprToken(ops[1], 'x', rm, false) &&
            ops.size() == 2) {
          opcodeOut = 0xAA0003E0u | (static_cast<u32>(rm) << 16) | rd;
          return true;
        }
        if (tryParseGprToken(ops[0], 'w', rd, false) &&
            tryParseGprToken(ops[1], 'w', rm, false) &&
            ops.size() == 2) {
          opcodeOut = 0x2A0003E0u | (static_cast<u32>(rm) << 16) | rd;
          return true;
        }

        long long imm64 = 0;
        if ((tryParseGprToken(ops[0], 'x', rd, false) ||
             tryParseGprToken(ops[0], 'w', rd, false)) &&
            tryParseSImm(ops[1], imm64)) {
          if (imm64 < 0 || imm64 > 0xFFFF)
            return false;

          u32 shift = 0;
          if (ops.size() == 3) {
            std::string sh = stripSpaces(ops[2]);
            if (sh.rfind("lsl#", 0) != 0)
              return false;
            long long shVal = 0;
            if (!tryParseSImm(sh.substr(3), shVal))
              return false;
            if (shVal < 0 || (shVal % 16) != 0)
              return false;
            shift = static_cast<u32>(shVal);
          }

          const bool isX = (ops[0][0] == 'x');
          if (isX) {
            if (!(shift == 0 || shift == 16 || shift == 32 || shift == 48))
              return false;
            opcodeOut = 0xD2800000u | ((shift / 16) << 21) |
                        ((static_cast<u32>(imm64) & 0xFFFFu) << 5) | rd;
          } else {
            if (!(shift == 0 || shift == 16))
              return false;
            opcodeOut = 0x52800000u | ((shift / 16) << 21) |
                        ((static_cast<u32>(imm64) & 0xFFFFu) << 5) | rd;
          }
          return true;
        }
      }
    }

    if (normInput.rfind("fmov ", 0) == 0) {
      const std::vector<std::string> ops = splitOperands(trimCopy(normInput.substr(5)));
      if (ops.size() == 2) {
        u8 rd = 0, rn = 0;
        // FMOV scalar immediate form: fmov sN/dN, #imm
        auto tryAssembleFmovImm = [&](bool isDouble, u8 dst, const std::string &immTok) -> bool {
          double wanted = 0.0;
          if (!tryParseFloatLiteral(immTok, wanted))
            return false;

          const u32 base = isDouble ? 0x1E601000u : 0x1E201000u;
          const std::string prefix = isDouble ? "fmov d0, #" : "fmov s0, #";
          const double relTol = isDouble ? 1e-12 : 1e-6;

          for (u32 imm8 = 0; imm8 <= 0xFF; ++imm8) {
            const u32 candidate = base | (imm8 << 13); // d0/s0 destination
            const std::string dis = DisassembleARM64(candidate, 0);
            if (dis.rfind(prefix, 0) != 0)
              continue;

            const std::string lit = trimCopy(dis.substr(prefix.size()));
            char *end = nullptr;
            errno = 0;
            const double decoded = std::strtod(lit.c_str(), &end);
            if (errno != 0 || !end || *end != '\0' || !std::isfinite(decoded))
              continue;

            const double scale = std::max(1.0, std::fabs(wanted));
            if (std::fabs(decoded - wanted) <= relTol * scale) {
              opcodeOut = candidate | static_cast<u32>(dst);
              return true;
            }
          }
          return false;
        };

        if (tryParseFpToken(ops[0], 's', rd) && tryAssembleFmovImm(false, rd, ops[1])) {
          return true;
        }
        if (tryParseFpToken(ops[0], 'd', rd) && tryAssembleFmovImm(true, rd, ops[1])) {
          return true;
        }

        if (tryParseFpToken(ops[0], 's', rd) && tryParseFpToken(ops[1], 's', rn)) {
          opcodeOut = 0x1E204000u | (static_cast<u32>(rn) << 5) | rd;
          return true;
        }
        if (tryParseFpToken(ops[0], 'd', rd) && tryParseFpToken(ops[1], 'd', rn)) {
          opcodeOut = 0x1E604000u | (static_cast<u32>(rn) << 5) | rd;
          return true;
        }
        // FMOV between GPR and FP scalar register.
        // w<->s
        if (tryParseGprToken(ops[0], 'w', rd, true) && tryParseFpToken(ops[1], 's', rn)) {
          opcodeOut = 0x1E260000u | (static_cast<u32>(rn) << 5) | rd; // fmov wD, sN
          return true;
        }
        if (tryParseFpToken(ops[0], 's', rd) && tryParseGprToken(ops[1], 'w', rn, true)) {
          opcodeOut = 0x1E270000u | (static_cast<u32>(rn) << 5) | rd; // fmov sD, wN
          return true;
        }
        // x<->d
        if (tryParseGprToken(ops[0], 'x', rd, true) && tryParseFpToken(ops[1], 'd', rn)) {
          opcodeOut = 0x9E660000u | (static_cast<u32>(rn) << 5) | rd; // fmov xD, dN
          return true;
        }
        if (tryParseFpToken(ops[0], 'd', rd) && tryParseGprToken(ops[1], 'x', rn, true)) {
          opcodeOut = 0x9E670000u | (static_cast<u32>(rn) << 5) | rd; // fmov dD, xN
          return true;
        }
      }
    }

    auto parseFp3Op = [&](const char *mnemonic, u32 baseS, u32 baseD) -> bool {
      const std::string prefix = std::string(mnemonic) + " ";
      if (normInput.rfind(prefix, 0) != 0)
        return false;
      const std::vector<std::string> ops = splitOperands(trimCopy(normInput.substr(prefix.size())));
      if (ops.size() != 3)
        return false;

      u8 rd = 0, rn = 0, rm = 0;
      if (tryParseFpToken(ops[0], 's', rd) &&
          tryParseFpToken(ops[1], 's', rn) &&
          tryParseFpToken(ops[2], 's', rm)) {
        opcodeOut = baseS | (static_cast<u32>(rm) << 16) |
                    (static_cast<u32>(rn) << 5) | rd;
        return true;
      }
      if (tryParseFpToken(ops[0], 'd', rd) &&
          tryParseFpToken(ops[1], 'd', rn) &&
          tryParseFpToken(ops[2], 'd', rm)) {
        opcodeOut = baseD | (static_cast<u32>(rm) << 16) |
                    (static_cast<u32>(rn) << 5) | rd;
        return true;
      }
      return false;
    };

    if (parseFp3Op("fadd", 0x1E202800u, 0x1E602800u)) return true;
    if (parseFp3Op("fsub", 0x1E203800u, 0x1E603800u)) return true;
    if (parseFp3Op("fmul", 0x1E200800u, 0x1E600800u)) return true;
    if (parseFp3Op("fdiv", 0x1E201800u, 0x1E601800u)) return true;

    return false;
  }

  static std::vector<u32> parseDwords(const std::string &hex) {
    std::vector<u32> out;
    std::string token;
    for (char c : hex) {
      if (std::isxdigit((unsigned char)c)) {
        token += (char)std::toupper((unsigned char)c);
      } else if (!token.empty()) {
        out.push_back((u32)std::strtoul(token.c_str(), nullptr, 16));
        token.clear();
      }
    }
    if (!token.empty()) out.push_back((u32)std::strtoul(token.c_str(), nullptr, 16));
    return out;
  }

  static u32 decodeType(u32 firstRaw) {
    u32 type = (firstRaw >> 28) & 0xF;
    if (type >= 0xC) type = (type << 4) | ((firstRaw >> 24) & 0xF);
    if (type >= 0xF0) type = (type << 4) | ((firstRaw >> 20) & 0xF);
    return type;
  }

  void rebuildHex(std::string &hex, size_t &cursor, const std::vector<u32> &dwords) {
    std::string result;
    for (size_t i = 0; i < dwords.size(); i++) {
      char buf[10];
      snprintf(buf, sizeof(buf), "%08X", dwords[i]);
      result += buf;
      if (i + 1 < dwords.size()) result += " ";
    }
    hex = result;
    if (cursor > hex.length()) cursor = hex.length();
    std::string finalClean;
    for (char c : hex) if (isxdigit(c)) finalClean += c;
    for (size_t i = 0; i < finalClean.length() && i < 32; i++) m_nibbleBackup[i] = finalClean[i];
  }

  bool applyStoredValueToHex(std::string &hex, size_t &cursor) {
    std::vector<u32> dwords = parseDwords(hex);
    if (dwords.empty()) return false;
    u32 type = decodeType(dwords[0]);
    syncVariables(dwords[0], type);
    applyStored(dwords, type);
    applyVariables(dwords[0], type);
    rebuildHex(hex, cursor, dwords);
    return true;
  }

  u64 widthMask() const {
    if (vWidth == 1) return 0xFFULL;
    if (vWidth == 2) return 0xFFFFULL;
    if (vWidth == 4) return 0xFFFFFFFFULL;
    return 0xFFFFFFFFFFFFFFFFULL;
  }

public:
  CheatFormatManager() {
    memset(m_nibbleBackup, '0', 32);
  }

  std::string getStoredValueSignedText(std::string &hex, size_t &cursor) {
    processEdit(hex, cursor);
    if (vWidth == 1) return std::to_string((s8)m_storedValue);
    if (vWidth == 2) return std::to_string((s16)m_storedValue);
    if (vWidth == 4) return std::to_string((s32)m_storedValue);
    return std::to_string((s64)m_storedValue);
  }

  std::string getStoredValueUnsignedText(std::string &hex, size_t &cursor) {
    processEdit(hex, cursor);
    if (vWidth == 1) return std::to_string((u8)m_storedValue);
    if (vWidth == 2) return std::to_string((u16)m_storedValue);
    if (vWidth == 4) return std::to_string((u32)m_storedValue);
    return std::to_string((u64)m_storedValue);
  }

  std::string getStoredValueFloatText(std::string &hex, size_t &cursor) {
    processEdit(hex, cursor);
    std::ostringstream oss;
    if (vWidth == 8) {
      union { u64 u; double d; } cvt;
      cvt.u = m_storedValue;
      oss << std::setprecision(17) << cvt.d;
    } else {
      union { u32 u; float f; } cvt;
      cvt.u = (u32)(m_storedValue & 0xFFFFFFFFULL);
      oss << std::setprecision(9) << cvt.f;
    }
    return oss.str();
  }

  std::string getStoredValueAsmText(std::string &hex, size_t &cursor) {
    processEdit(hex, cursor);
    if (vWidth != 4) return "";
    const u32 opcode = static_cast<u32>(m_storedValue);
    std::string dis = DisassembleARM64(opcode, m_storedAddress);
    if (!dis.empty()) return dis;
    char raw[11];
    std::snprintf(raw, sizeof(raw), "0x%08X", opcode);
    return std::string(raw);
  }

  u32 getCurrentCodeType(std::string &hex, size_t &cursor) {
    processEdit(hex, cursor);
    std::vector<u32> dwords = parseDwords(hex);
    if (dwords.empty()) return 0xFFFFFFFF;
    return decodeType(dwords[0]);
  }

  bool setComboKeyForCodeType(std::string &hex, size_t &cursor, u32 targetType,
                              u64 keyMask) {
    processEdit(hex, cursor);
    std::vector<u32> dwords = parseDwords(hex);
    if (dwords.empty()) return false;

    const u32 type = decodeType(dwords[0]);
    if (type != 0x8 && type != 0xC4) return false;

    syncVariables(dwords[0], type);
    if (targetType == 0x8) {
      vBitMask = keyMask & 0x0FFFFFFFULL;
      dwords[0] = 0x80000000;
      applyVariables(dwords[0], 0x8);
      dwords.resize(1);
    } else if (targetType == 0xC4) {
      vBitMask = keyMask;
      dwords[0] = 0xC4000000;
      applyVariables(dwords[0], 0xC4);
      if (dwords.size() < 3) dwords.resize(3, 0);
      dwords[1] = static_cast<u32>(vBitMask >> 32);
      dwords[2] = static_cast<u32>(vBitMask & 0xFFFFFFFFULL);
    } else {
      return false;
    }

    rebuildHex(hex, cursor, dwords);
    processEdit(hex, cursor);
    return true;
  }

  bool setComboCodeType(std::string &hex, size_t &cursor, u32 targetType) {
    processEdit(hex, cursor);
    std::vector<u32> dwords = parseDwords(hex);
    if (dwords.empty()) return false;

    const u32 type = decodeType(dwords[0]);
    if (type != 0x8 && type != 0xC4) return false;

    u64 existingMask = 0;
    if (type == 0x8) {
      existingMask = static_cast<u64>(dwords[0] & 0x0FFFFFFF);
    } else {
      if (dwords.size() < 3) dwords.resize(3, 0);
      existingMask = (static_cast<u64>(dwords[1]) << 32) |
                     static_cast<u64>(dwords[2]);
    }
    return setComboKeyForCodeType(hex, cursor, targetType, existingMask);
  }

  bool toggleC4AutoRepeat(std::string &hex, size_t &cursor) {
    processEdit(hex, cursor);
    std::vector<u32> dwords = parseDwords(hex);
    if (dwords.empty()) return false;
    if (decodeType(dwords[0]) != 0xC4) return false;

    if (dwords.size() < 3) dwords.resize(3, 0);
    const u64 mask = (static_cast<u64>(dwords[1]) << 32) |
                     static_cast<u64>(dwords[2]);

    syncVariables(dwords[0], 0xC4);
    vOpType = (vOpType == 0) ? 1 : 0;
    vBitMask = mask;
    applyVariables(dwords[0], 0xC4);
    dwords[1] = static_cast<u32>(mask >> 32);
    dwords[2] = static_cast<u32>(mask & 0xFFFFFFFFULL);
    rebuildHex(hex, cursor, dwords);
    processEdit(hex, cursor);
    return true;
  }

  bool setStoredValueFromIntegerText(std::string &hex, size_t &cursor, const std::string &input) {
    processEdit(hex, cursor);
    std::string t = trimCopy(input);
    if (t.empty()) return false;
    char *end = nullptr;
    errno = 0;
    long long s = std::strtoll(t.c_str(), &end, 10);
    if (end && *end == '\0' && errno == 0) {
      m_storedValue = ((u64)s) & widthMask();
    } else {
      end = nullptr;
      errno = 0;
      unsigned long long u = std::strtoull(t.c_str(), &end, 10);
      if (!(end && *end == '\0' && errno == 0)) return false;
      m_storedValue = ((u64)u) & widthMask();
    }
    if (!applyStoredValueToHex(hex, cursor)) return false;
    processEdit(hex, cursor);
    return true;
  }

  bool setStoredValueFromUnsignedText(std::string &hex, size_t &cursor, const std::string &input) {
    processEdit(hex, cursor);
    std::string t = trimCopy(input);
    if (t.empty()) return false;
    char *end = nullptr;
    errno = 0;
    unsigned long long u = std::strtoull(t.c_str(), &end, 10);
    if (!(end && *end == '\0' && errno == 0)) return false;
    m_storedValue = ((u64)u) & widthMask();
    if (!applyStoredValueToHex(hex, cursor)) return false;
    processEdit(hex, cursor);
    return true;
  }

  bool setStoredValueFromFloatText(std::string &hex, size_t &cursor, const std::string &input) {
    processEdit(hex, cursor);
    std::string t = trimCopy(input);
    if (t.empty()) return false;
    char *end = nullptr;
    errno = 0;
    if (vWidth == 8) {
      union { u64 u; double d; } cvt;
      cvt.d = std::strtod(t.c_str(), &end);
      if (!(end && *end == '\0' && errno == 0)) return false;
      m_storedValue = cvt.u;
    } else {
      union { u32 u; float f; } cvt;
      cvt.f = std::strtof(t.c_str(), &end);
      if (!(end && *end == '\0' && errno == 0)) return false;
      m_storedValue = (m_storedValue & 0xFFFFFFFF00000000ULL) | (u64)cvt.u;
    }
    if (!applyStoredValueToHex(hex, cursor)) return false;
    processEdit(hex, cursor);
    return true;
  }

  bool clearStoredValue(std::string &hex, size_t &cursor) {
    processEdit(hex, cursor);
    m_storedValue = 0;
    if (!applyStoredValueToHex(hex, cursor)) return false;
    processEdit(hex, cursor);
    return true;
  }

  bool setStoredValueFromAsmText(std::string &hex, size_t &cursor, const std::string &input) {
    processEdit(hex, cursor);
    if (vWidth != 4) return false;
    u32 opcode = 0;
    if (!tryAssembleArm64(input, opcode)) return false;
    m_storedValue = (m_storedValue & 0xFFFFFFFF00000000ULL) | static_cast<u64>(opcode);
    if (!applyStoredValueToHex(hex, cursor)) return false;
    processEdit(hex, cursor);
    return true;
  }

  std::string processEdit(std::string &hex, size_t &cursor) {
    std::string inputDigits;
    for (char c : hex) if (isxdigit(c)) inputDigits += toupper(c);
    if (inputDigits.empty()) return "";

    for (size_t i = 0; i < inputDigits.length() && i < 32; i++) m_nibbleBackup[i] = inputDigits[i];

    u32 firstRaw = std::strtoul(std::string(m_nibbleBackup, 8).c_str(), nullptr, 16);
    u32 type = (firstRaw >> 28) & 0xF;
    if (type >= 0xC) type = (type << 4) | ((firstRaw >> 24) & 0xF);

    // Build current dwords from input
    std::vector<u32> dwords;
    for (size_t i = 0; i < 32; i += 8) dwords.push_back(std::strtoul(std::string(m_nibbleBackup + i, 8).c_str(), nullptr, 16));

    if (!m_initialized) {
        syncVariables(firstRaw, type);
        updateStored(dwords, type);
        m_initialized = true;
    } else {
        u8 inputT = 0;
        bool hasWidth = false;
        if (type < 0xC) {
            if (type != 2 && type != 3 && type != 4 && type != 8) { inputT = n_nibble(firstRaw, 1); hasWidth = true; }
        } else if (type == 0xC0 || type == 0xFFF) {
            inputT = n_nibble(firstRaw, 2); hasWidth = true;
        }

        bool typeChanged = (type != m_lastType);
        bool widthChanged = !typeChanged && hasWidth && (inputT != m_lastWidth && (inputT == 1 || inputT == 2 || inputT == 4 || inputT == 8));
        
        if (typeChanged || widthChanged) {
            if (widthChanged) vWidth = inputT;
            applyStored(dwords, type); 
        } else {
            syncVariables(firstRaw, type);
            updateStored(dwords, type);
        }
    }

    // Strict Enforcement of variable ranges
    if (vWidth != 1 && vWidth != 2 && vWidth != 4 && vWidth != 8) vWidth = 4;
    if (vMemRegion > 4) vMemRegion = 4;
    if (vCondition < 1 || vCondition > 6) vCondition = 5;
    if (vArithOp > 13) vArithOp = 0;
    if (type == 0x7 && vArithOp > 4) vArithOp = 0;
    if (vEndType > 1) vEndType = 0;
    if (vIncFlag > 1) vIncFlag = 0;
    if (vImmFlag > 1) vImmFlag = 0;
    if (vCode5Type > 3) vCode5Type = 0;
    if (vOffsetEnable > 1) vOffsetEnable = 0;
    
    // OpType limits based on code type
    if (type == 0x1 && vOpType > 1) vOpType = 0;
    if (type == 0x3 && vOpType > 1) vOpType = 0;
    if (type == 0xA && vOpType > 5) vOpType = 0;
    if (type == 0xC0 && vOpType > 5) vOpType = 0;
    if (type == 0xC1 && vOpType > 3) vOpType = 0;
    if (type == 0xC2 && vOpType > 3) vOpType = 0;
    if (type == 0xC4 && vOpType > 1) vOpType = 0;
    if (type == 0xFFF && vOpType > 4) vOpType = 0;

    // Reg limits (already 4-bit via applying, but for logic safety)
    if (vRegDest > 15) vRegDest = 15;
    if (vRegSource > 15) vRegSource = 15;
    if (vRegOffset > 15) vRegOffset = 15;
    if (vRegBase > 15) vRegBase = 15;

    // Special: Code A vRegOffset used as MemRegion if vOpType >= 3
    if (type == 0xA && vOpType >= 3 && vRegOffset > 4) vRegOffset = 4;

    // Apply owner variables back to the opcode
    applyVariables(dwords[0], type);
    m_lastType = type; m_lastWidth = vWidth;

    // Resize and apply consistent variables
    size_t targetCount = 1;
    if (type == 0x0 || type == 0x1) targetCount = (vWidth == 8) ? 4 : 3;
    else if (type == 0x2) targetCount = 1;
    else if (type == 0x3) targetCount = (dwords[0] & 0x01000000) ? 1 : 2;
    else if (type == 0x4 || type == 0x6) targetCount = 3;
    else if (type == 0x5) targetCount = 2; 
    else if (type == 0x7) targetCount = 2;
    else if (type == 0x8) targetCount = 1;
    else if (type == 0x9) targetCount = ((vImmFlag == 1) ? ((vWidth == 8) ? 3 : 2) : 1);
    else if (type == 0xA) targetCount = (vOpType == 2 || vOpType == 4 || vOpType == 5) ? 2 : 1;
    else if (type == 0xC4) targetCount = 3;
    else if (type == 0xC0) {
        if (vOpType == 1 || vOpType == 3 || vOpType == 5) targetCount = 1;
        else if (vOpType == 4) targetCount = (vWidth == 8) ? 3 : 2;
        else targetCount = 2; // types 0 and 2
    }
    else if (type == 0xFFF) targetCount = (vOpType == 1 || vOpType == 3 || vOpType == 4) ? 1 : 2;

    if (type == 0xC4 && dwords.size() >= 3) {
        vBitMask = (static_cast<u64>(dwords[1]) << 32) |
                   static_cast<u64>(dwords[2]);
    }

    if (dwords.size() > targetCount) dwords.resize(targetCount);
    while (dwords.size() < targetCount) dwords.push_back(0);

    if (type == 0xC4 && dwords.size() >= 3) {
        dwords[1] = static_cast<u32>(vBitMask >> 32);
        dwords[2] = static_cast<u32>(vBitMask & 0xFFFFFFFFULL);
    }


    // 8. Finalize hex string and update cursor if needed
    std::string result;
    for (size_t i = 0; i < dwords.size(); i++) {
        char buf[10]; snprintf(buf, sizeof(buf), "%08X", dwords[i]);
        result += buf; if (i < dwords.size() - 1) result += " ";
    }
    hex = result;
    if (cursor > hex.length()) cursor = hex.length();

    // Re-sync backup with validated dwords to ensure any "un-backuppable" zeroes (reserved fields) are reflected
    std::string finalClean;
    for (char c : hex) if (isxdigit(c)) finalClean += c;
    for (size_t i = 0; i < finalClean.length() && i < 32; i++) m_nibbleBackup[i] = finalClean[i];

    size_t idx = 0;
    return WrapNote(GetOpcodeNote(dwords, idx), 45);
  }
};

class CheatEditMenu : public tsl::Gui {
  u32 m_cheatId;
  std::string m_cheatName;
  bool m_enabled;
  int m_fontSize;
  tsl::elm::List *m_list;
  std::vector<u32> m_cachedOpcodes;
  std::string m_notesPath;

  bool m_dirty;
  int m_focusIndex;

public:
  CheatEditMenu(u32 cheatId, const std::string &name, bool enabled,
                const std::vector<u32> &opcodes = {}, int focusIndex = -1,
                bool dirty = false)
      : m_cheatId(cheatId), m_cheatName(name), m_enabled(enabled),
        m_fontSize(g_cheatFontSize), m_list(nullptr), m_cachedOpcodes(opcodes),
        m_dirty(dirty), m_focusIndex(focusIndex) {

    // Fetch full name from ID if possible to avoid truncation from arguments
    if (m_cheatId != 0) {
      DmntCheatEntry entry;
      if (R_SUCCEEDED(dmntchtGetCheatById(&entry, m_cheatId))) {
        m_cheatName = entry.definition.readable_name;
        m_enabled = entry.enabled;
      }
    }

    DmntCheatProcessMetadata metadata;
    if (R_SUCCEEDED(dmntchtGetCheatProcessMetadata(&metadata))) {
      std::stringstream ss;
      ss << "sdmc:/switch/breeze/cheats/" << std::setfill('0') << std::setw(16)
         << std::uppercase << std::hex << metadata.title_id << "/notes.txt";
      m_notesPath = ss.str();
    }

    if (!m_notesPath.empty()) {
      std::string fontSizeStr = ult::parseValueFromIniSection(
          m_notesPath, "Breeze", "editor_font_size");
      if (!fontSizeStr.empty()) {
        m_fontSize =
            std::clamp(static_cast<int>(ult::stoi(fontSizeStr)), 10, 30);
      }
      // std::string showNotesStr =
      //     ult::parseValueFromIniSection(m_notesPath, "Breeze", "show_notes");
      // if (!showNotesStr.empty()) {
      //   ult::showCheatNotes = (showNotesStr == "true");
      // }
    }

    if (m_cachedOpcodes.empty()) {
      u64 count = 0;
      if (R_SUCCEEDED(dmntchtGetCheatCount(&count)) && count > 0) {
        std::vector<DmntCheatEntry> cheats(count);
        if (R_SUCCEEDED(dmntchtGetCheats(cheats.data(), count, 0, &count))) {
          for (const auto &cheat : cheats) {
            if (cheat.cheat_id == m_cheatId) {
              m_cachedOpcodes.assign(cheat.definition.opcodes,
                                     cheat.definition.opcodes +
                                         cheat.definition.num_opcodes);
              break;
            }
          }
        }
      }
    }
  }

  virtual ~CheatEditMenu() {
#if HAVE_KEYSTONE_ASM
    u32 dummy;
    CheatFormatManager::tryAssembleWithKeystone("__CLEANUP__", 0, dummy);
#endif
  }

  void refreshList() {
    m_list->clear();

    // Cheat Info Header
    addHeader(m_list, "Cheat Info");

    auto *nameItem = new tsl::elm::ListItem(m_cheatName);
    nameItem->setUseWrapping(true);
    nameItem->setClickListener([this, nameItem](u64 keys) {
      if (keys & KEY_A) {
        tsl::changeTo<tsl::KeyboardGui>(SEARCH_TYPE_TEXT, m_cheatName,
                                        "Edit Name",
                                        [this, nameItem](std::string newVal) {
                                          // Update local state
                                          this->m_cheatName = newVal;
                                          this->m_dirty = true;

                                          // Update UI immediately
                                          nameItem->setText(newVal);

                                          // Explicitly navigate back since
                                          // handleConfirm skipping it when
                                          // callback exists
                                          tsl::goBack();
                                        },
                                        nullptr,
                                        false);
        return true;
      }
      return false;
    });
    m_list->addItem(nameItem);

    // Hex Codes Header
    addHeader(m_list, "Hex Codes", std::string("\uE0E3 ") + ult::NOTES);

    if (!m_cachedOpcodes.empty()) {
      size_t i = 0;
      while (i < m_cachedOpcodes.size()) {
        size_t startIdx = i;
        std::string note = GetOpcodeNote(m_cachedOpcodes, i);
        size_t numDwords = i - startIdx;
        if (numDwords ==
            0) { // Safety catch for unknown opcodes that don't advance index
          i++;
          numDwords = 1;
        }

        std::string lineStr = "";
        for (size_t j = 0; j < numDwords; j++) {
          char hex[16];
          snprintf(hex, sizeof(hex), "%08X ", m_cachedOpcodes[startIdx + j]);
          lineStr += hex;
        }
        if (!lineStr.empty())
          lineStr.pop_back();

        auto *item = new tsl::elm::ListItem(lineStr);
        item->setFontSize(m_fontSize);
        item->setUseWrapping(true);

        if (!note.empty()) {
          item->setNote(note);
        }

        m_list->addItem(item);
      }
    } else {
      m_list->addItem(new tsl::elm::ListItem("No opcodes found"));
    }

    if (m_focusIndex != -1) {
      m_list->setFocusedIndex(m_focusIndex);
    }
  }

  virtual void update() override {
    if (this->m_dirty && this->m_list != nullptr) {
      auto &items = this->m_list->getItems();
      for (size_t i = 3; i < items.size(); i++) {
        auto *element = items[i];
        if (element != nullptr && element->isItem() && !element->isTable()) {
          auto *item = static_cast<tsl::elm::ListItem *>(element);
          std::string text = item->getText();
          if (!text.empty() && text != "No opcodes found") {
            std::vector<u32> dwords;
            std::string hex;
            for (char c : text) {
              if (isxdigit(c))
                hex += c;
              else if (!hex.empty()) {
                dwords.push_back(std::strtoul(hex.c_str(), nullptr, 16));
                hex.clear();
              }
            }
            if (!hex.empty())
              dwords.push_back(std::strtoul(hex.c_str(), nullptr, 16));

            if (!dwords.empty()) {
              size_t idx = 0;
              std::string note = GetOpcodeNote(dwords, idx);
              if (item->getNote() != note) {
                item->setNote(note);
              }
            }
          }
        }
      }
    }
  }

  virtual tsl::elm::Element *createUI() override {
    auto *frame = new tsl::elm::OverlayFrame("Cheat Editor", "");
    m_list = new tsl::elm::List();

    // Load cache if empty (first run)
    if (m_cachedOpcodes.empty() && !m_dirty) {
      DmntCheatEntry cheat;
      if (R_SUCCEEDED(dmntchtGetCheatById(&cheat, m_cheatId))) {
        for (u32 i = 0; i < cheat.definition.num_opcodes; i++) {
          m_cachedOpcodes.push_back(cheat.definition.opcodes[i]);
        }
      }
    }

    refreshList();

    frame->setContent(m_list);
    return frame;
  }

  virtual bool handleInput(u64 keysDown, u64 keysHeld,
                           const HidTouchState &touchPos,
                           HidAnalogStickState joyStickPosLeft,
                           HidAnalogStickState joyStickPosRight) override {
    if (keysDown & KEY_B) {
      if (m_dirty) {
        const auto &items = m_list->getItems();

        std::vector<u32> finalOps;
        finalOps.reserve(0x100);

        // Index 3+: Hex items (skip headers and name)
        for (size_t i = 3; i < items.size(); i++) {
          struct ListItemProxy : public tsl::elm::ListItem {
            using tsl::elm::ListItem::m_text;
          };
          auto *listItem = static_cast<ListItemProxy *>(items[i]);
          std::string line = listItem->m_text;

          std::stringstream ss(line);
          std::string word;
          while (ss >> word) {
            finalOps.push_back(std::strtoul(word.c_str(), nullptr, 16));
          }
        }

        if (!finalOps.empty()) {
          dmntchtRemoveCheat(m_cheatId);

          DmntCheatDefinition def;
          memset(&def, 0, sizeof(def));
          strncpy(def.readable_name, m_cheatName.c_str(),
                  sizeof(def.readable_name) - 1);
          def.num_opcodes = std::min((size_t)0x100, finalOps.size());
          for (u32 i = 0; i < def.num_opcodes; i++) {
            def.opcodes[i] = finalOps[i];
          }

          u32 newId = 0;
          dmntchtAddCheat(&def, m_enabled, &newId);

          // Trigger refresh of parent menu to show new name
          refreshPage.store(true, release);

          // Preserve focus on the renamed cheat
          extern std::string jumpItemName;
          extern std::atomic<bool> jumpItemExactMatch;

          jumpItemName = m_cheatName;
          jumpItemExactMatch.store(true, release);
          skipJumpReset.store(true, release);
        }
      }
#ifdef EDITCHEAT_OVL
      std::string path = "sdmc:/switch/.overlays/breezehand.ovl";
      std::string args = "";
      if (!g_focusCheatName.empty()) {
        args = "--focus_cheat_name \"" + g_focusCheatName + "\"";
        
        // Pass folder stack (both indices and names)
        if (!g_focusFolderIndices.empty()) {
          args += " --folder_indices";
          for (u32 idx : g_focusFolderIndices) {
            args += " " + std::to_string(idx);
          }
          args += " --folder_names";
          for (const auto& name : g_focusFolderNames) {
            args += " \"" + name + "\"";
          }
        }
      }

      std::lock_guard<std::mutex> lock(ult::overlayLaunchMutex);
      ult::requestedOverlayPath = path;
      ult::requestedOverlayArgs = args;
      ult::setIniFileValue(ult::ULTRAHAND_CONFIG_INI_PATH,
                           ult::ULTRAHAND_PROJECT_NAME, ult::IN_OVERLAY_STR,
                           ult::TRUE_STR);
      ult::overlayLaunchRequested.store(true, std::memory_order_release);
#else
      tsl::goBack();
#endif
      return true;
    }

    if (keysDown & KEY_Y) {
      s_noteMinimalMode = !s_noteMinimalMode;

      // In-place update of notes without destroying list items
      const auto &items = m_list->getItems();

      // Re-evaluate notes for all hex items (skip header/name at indices 0-2)
      for (size_t i = 3; i < items.size(); i++) {
        auto *element = items[i];
        if (element && element->isItem() && !element->isTable()) {
          auto *item = static_cast<tsl::elm::ListItem *>(element);
          std::string text = item->getText();
          if (!text.empty() && text != "No opcodes found") {
            // Parse hex from text to get opcodes
            std::vector<u32> dwords;
            std::string hex;
            for (char c : text) {
              if (isxdigit(c))
                hex += c;
              else if (!hex.empty()) {
                dwords.push_back(std::strtoul(hex.c_str(), nullptr, 16));
                hex.clear();
              }
            }
            if (!hex.empty())
              dwords.push_back(std::strtoul(hex.c_str(), nullptr, 16));

            if (!dwords.empty()) {
              size_t idx = 0;
              // Regenerate note with new mode
              std::string note = GetOpcodeNote(dwords, idx);
              item->setNote(note); // This handles item height recalc
            }
          }
        }
      }

      // Force container layout update
      m_list->recalculateLayout();

      this->m_dirty = true;
      return true;
    }

    if (keysDown & KEY_A) {
      // Access protected accessors via proxy
      struct ListProxy : public tsl::elm::List {
        using tsl::elm::List::m_focusedIndex;
        using tsl::elm::List::m_items;
      };

      auto *proxy = static_cast<ListProxy *>(m_list);
      if (!proxy->m_items.empty() &&
          proxy->m_focusedIndex < proxy->m_items.size()) {
        // Safeguard headers
        if (proxy->m_focusedIndex == 0 || proxy->m_focusedIndex == 2)
          return false;

        auto *item = proxy->m_items[proxy->m_focusedIndex];
        if (item) {
          struct ListItemProxy : public tsl::elm::ListItem {
            using tsl::elm::ListItem::m_text;
          };
          auto *listItem = static_cast<ListItemProxy *>(item);
          if (listItem) {
            std::string val = listItem->m_text;
            u32 fIdx = proxy->m_focusedIndex;

            auto fmtMgr = std::make_shared<CheatFormatManager>();

            tsl::changeTo<tsl::KeyboardGui>(
                SEARCH_TYPE_HEX, val, "Edit Hex",
                [this, proxy, fIdx](std::string result) {
                  this->m_dirty = true;

                  if (!proxy->m_items.empty() && fIdx < proxy->m_items.size()) {
                    auto *item = proxy->m_items[fIdx];
                    if (item) {
                      static_cast<tsl::elm::ListItem *>(item)->setText(result);
                    }
                  }
                  tsl::goBack();
                },
                [fmtMgr](std::string &currentVal, size_t &cursor) -> std::string {
                  return fmtMgr->processEdit(currentVal, cursor);
                },
                true,
                [fmtMgr](std::string &currentVal, size_t &cursor) -> std::string {
                  return fmtMgr->getStoredValueSignedText(currentVal, cursor);
                },
                [fmtMgr](std::string &currentVal, size_t &cursor) -> std::string {
                  return fmtMgr->getStoredValueUnsignedText(currentVal, cursor);
                },
                [fmtMgr](std::string &currentVal, size_t &cursor) -> std::string {
                  return fmtMgr->getStoredValueFloatText(currentVal, cursor);
                },
                [fmtMgr](std::string &currentVal, size_t &cursor, const std::string &input) -> bool {
                  return fmtMgr->setStoredValueFromIntegerText(currentVal, cursor, input);
                },
                [fmtMgr](std::string &currentVal, size_t &cursor, const std::string &input) -> bool {
                  return fmtMgr->setStoredValueFromUnsignedText(currentVal, cursor, input);
                },
                [fmtMgr](std::string &currentVal, size_t &cursor, const std::string &input) -> bool {
                  return fmtMgr->setStoredValueFromFloatText(currentVal, cursor, input);
                },
                [fmtMgr](std::string &currentVal, size_t &cursor) -> std::string {
                  return fmtMgr->getStoredValueAsmText(currentVal, cursor);
                },
                [fmtMgr](std::string &currentVal, size_t &cursor, const std::string &input) -> bool {
                  return fmtMgr->setStoredValueFromAsmText(currentVal, cursor, input);
                },
                [fmtMgr](std::string &currentVal, size_t &cursor) -> bool {
                  return fmtMgr->clearStoredValue(currentVal, cursor);
                },
                [fmtMgr](std::string &currentVal, size_t &cursor) -> u32 {
                  return fmtMgr->getCurrentCodeType(currentVal, cursor);
                },
                [fmtMgr](std::string &currentVal, size_t &cursor, u32 targetType, u64 keyMask) -> bool {
                  return fmtMgr->setComboKeyForCodeType(currentVal, cursor, targetType, keyMask);
                },
                [fmtMgr](std::string &currentVal, size_t &cursor, u32 targetType) -> bool {
                  return fmtMgr->setComboCodeType(currentVal, cursor, targetType);
                },
                [fmtMgr](std::string &currentVal, size_t &cursor) -> bool {
                  return fmtMgr->toggleC4AutoRepeat(currentVal, cursor);
                });
            return true;
          }
        }
      }
    }

    // Manual Font Size Adjustment
    if ((keysHeld & KEY_ZL)) {
      bool changed = false;
      if (keysDown & KEY_R) {
        m_fontSize = std::min(m_fontSize + 1, 30);
        changed = true;
      }
      if (keysDown & KEY_L) {
        m_fontSize = std::max(m_fontSize - 1, 10);
        changed = true;
      }

      if (changed) {
        if (!m_notesPath.empty()) {
          setIniFileValue(m_notesPath, "Breeze", "editor_font_size",
                          std::to_string(m_fontSize));
        }
      }

      if (changed && m_list) {
        // Access protected m_items via proxy hack
        struct ListProxy : public tsl::elm::List {
          using tsl::elm::List::m_items;
        };

        // Skip the first few items (Header, Name, Header)
        // Index 0: CategoryHeader "Cheat Info"
        // Index 1: ListItem Name
        // Index 2: CategoryHeader "Hex Codes"
        // Index 3+: Hex items

        auto &items = static_cast<ListProxy *>(m_list)->m_items;
        for (size_t i = 3; i < items.size(); i++) {
          auto *item = items[i];
          if (item) {
            // Assuming all items after index 2 are ListItems for hex
            // We should ideally check type but standard usage here implies it.
            // Cast to ListItem (base class Element doesn't have setFontSize)
            // Accessing private/protected setFontSize might require cast to
            // ListItem* ListItem inherits from Element. Element has no
            // setFontSize. We created them as ListItems.
            static_cast<tsl::elm::ListItem *>(item)->setFontSize(m_fontSize);
          }
        }
        return true;
      }
    }

    return false;
  }
};

namespace {

const char *SearchTypeLabel(searchType_t type) {
  switch (type) {
  case SEARCH_TYPE_UNSIGNED_8BIT:
    return "u8";
  case SEARCH_TYPE_SIGNED_8BIT:
    return "s8";
  case SEARCH_TYPE_UNSIGNED_16BIT:
    return "u16";
  case SEARCH_TYPE_SIGNED_16BIT:
    return "s16";
  case SEARCH_TYPE_UNSIGNED_32BIT:
    return "u32";
  case SEARCH_TYPE_SIGNED_32BIT:
    return "s32";
  case SEARCH_TYPE_UNSIGNED_64BIT:
    return "u64";
  case SEARCH_TYPE_SIGNED_64BIT:
    return "s64";
  case SEARCH_TYPE_FLOAT_32BIT:
    return "flt";
  case SEARCH_TYPE_FLOAT_64BIT:
    return "dbl";
  case SEARCH_TYPE_POINTER:
    return "ptr";
  case SEARCH_TYPE_UNSIGNED_40BIT:
    return "u40";
  default:
    return "unknown";
  }
}

const char *SearchModeLabel(searchMode_t mode) {
  static const char *const modeNames[] = {
      "==A",       "!=A",   ">A",      "<A",      ">=A",   "<=A",
      "[A..B]",    "&B=A",  "<A..B>",  "++",      "--",    "DIFF",
      "SAME",      "[A,B]", "[A,,B]",  "STRING",  "++Val", "--Val",
      "==*A",      "==**A", "NONE",    "DIFFB",   "SAMEB", "B++",
      "B--",       "NotAB", "[A.B.C]", "[A bflip B]", "Advance", "GAP",
      "{GAP}",     "PTR",   "~PTR",    "[A..B]f.0", "Gen2 data",
      "Gen2 code", "GETB",  "REBASE",  "Target",  "ptr and offset",
      "skip",      "Aborted Target Search", "Branch code", "LDRx code",
      "ADRP code", "EOR code", "GETB==A"};

  const int idx = static_cast<int>(mode);
  if (idx < 0 ||
      idx >= static_cast<int>(sizeof(modeNames) / sizeof(modeNames[0]))) {
    return "unknown";
  }
  return modeNames[idx];
}

std::string TrimCopy(std::string text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front()))) {
    text.erase(text.begin());
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back()))) {
    text.pop_back();
  }
  return text;
}

std::string ValueToDisplay(searchValue_t value, searchType_t type) {
  char buf[64] = {};
  switch (type) {
  case SEARCH_TYPE_UNSIGNED_8BIT:
    std::snprintf(buf, sizeof(buf), "%u", value._u8);
    break;
  case SEARCH_TYPE_SIGNED_8BIT:
    std::snprintf(buf, sizeof(buf), "%d", value._s8);
    break;
  case SEARCH_TYPE_UNSIGNED_16BIT:
    std::snprintf(buf, sizeof(buf), "%u", value._u16);
    break;
  case SEARCH_TYPE_SIGNED_16BIT:
    std::snprintf(buf, sizeof(buf), "%d", value._s16);
    break;
  case SEARCH_TYPE_UNSIGNED_32BIT:
    std::snprintf(buf, sizeof(buf), "%u", value._u32);
    break;
  case SEARCH_TYPE_SIGNED_32BIT:
    std::snprintf(buf, sizeof(buf), "%d", value._s32);
    break;
  case SEARCH_TYPE_UNSIGNED_64BIT:
    std::snprintf(buf, sizeof(buf), "%llu",
                  static_cast<unsigned long long>(value._u64));
    break;
  case SEARCH_TYPE_SIGNED_64BIT:
    std::snprintf(buf, sizeof(buf), "%lld",
                  static_cast<long long>(value._s64));
    break;
  case SEARCH_TYPE_FLOAT_32BIT:
    std::snprintf(buf, sizeof(buf), "%.8g", value._f32);
    break;
  case SEARCH_TYPE_FLOAT_64BIT:
    std::snprintf(buf, sizeof(buf), "%.16g", value._f64);
    break;
  case SEARCH_TYPE_POINTER:
    std::snprintf(buf, sizeof(buf), "0x%016llX",
                  static_cast<unsigned long long>(value._u64));
    break;
  case SEARCH_TYPE_UNSIGNED_40BIT:
    std::snprintf(buf, sizeof(buf), "0x%010llX",
                  static_cast<unsigned long long>(value._u64 & 0xFFFFFFFFFFULL));
    break;
  default:
    std::snprintf(buf, sizeof(buf), "0");
    break;
  }
  return buf;
}

std::string ValueToEditable(searchValue_t value, searchType_t type) {
  switch (type) {
  case SEARCH_TYPE_POINTER:
  case SEARCH_TYPE_UNSIGNED_40BIT:
    return ValueToDisplay(value, type);
  default:
    return ValueToDisplay(value, type);
  }
}

std::string ValueToHexDisplay(searchValue_t value, searchType_t type) {
  char buf[32] = {};
  switch (type) {
  case SEARCH_TYPE_UNSIGNED_8BIT:
  case SEARCH_TYPE_SIGNED_8BIT:
    std::snprintf(buf, sizeof(buf), "0x%02X", static_cast<unsigned>(value._u8));
    break;
  case SEARCH_TYPE_UNSIGNED_16BIT:
  case SEARCH_TYPE_SIGNED_16BIT:
    std::snprintf(buf, sizeof(buf), "0x%04X", static_cast<unsigned>(value._u16));
    break;
  case SEARCH_TYPE_UNSIGNED_32BIT:
  case SEARCH_TYPE_SIGNED_32BIT:
  case SEARCH_TYPE_FLOAT_32BIT:
    std::snprintf(buf, sizeof(buf), "0x%08X", value._u32);
    break;
  case SEARCH_TYPE_UNSIGNED_40BIT:
    std::snprintf(buf, sizeof(buf), "0x%010llX",
                  static_cast<unsigned long long>(value._u64 & 0xFFFFFFFFFFULL));
    break;
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_SIGNED_64BIT:
  case SEARCH_TYPE_FLOAT_64BIT:
  case SEARCH_TYPE_POINTER:
    std::snprintf(buf, sizeof(buf), "0x%016llX",
                  static_cast<unsigned long long>(value._u64));
    break;
  default:
    std::snprintf(buf, sizeof(buf), "0x0");
    break;
  }
  return buf;
}

bool ParseHexValueForType(const std::string &text, searchType_t type,
                          searchValue_t &outValue) {
  std::string trimmed = TrimCopy(text);
  if (trimmed.empty()) {
    return false;
  }
  if (trimmed.rfind("0x", 0) == 0 || trimmed.rfind("0X", 0) == 0) {
    trimmed = trimmed.substr(2);
  }
  if (trimmed.empty()) {
    return false;
  }
  for (char c : trimmed) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }

  errno = 0;
  char *end = nullptr;
  const unsigned long long parsed = std::strtoull(trimmed.c_str(), &end, 16);
  if (errno != 0 || end == trimmed.c_str() || *end != '\0') {
    return false;
  }
  const u64 v = static_cast<u64>(parsed);

  switch (type) {
  case SEARCH_TYPE_UNSIGNED_8BIT:
  case SEARCH_TYPE_SIGNED_8BIT:
    outValue._u8 = static_cast<u8>(v & 0xFFULL);
    return true;
  case SEARCH_TYPE_UNSIGNED_16BIT:
  case SEARCH_TYPE_SIGNED_16BIT:
    outValue._u16 = static_cast<u16>(v & 0xFFFFULL);
    return true;
  case SEARCH_TYPE_UNSIGNED_32BIT:
  case SEARCH_TYPE_SIGNED_32BIT:
  case SEARCH_TYPE_FLOAT_32BIT:
    outValue._u32 = static_cast<u32>(v & 0xFFFFFFFFULL);
    return true;
  case SEARCH_TYPE_UNSIGNED_40BIT:
    outValue._u64 = v & 0xFFFFFFFFFFULL;
    return true;
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_SIGNED_64BIT:
  case SEARCH_TYPE_FLOAT_64BIT:
  case SEARCH_TYPE_POINTER:
    outValue._u64 = v;
    return true;
  default:
    return false;
  }
}

long double ValueToNumeric(searchValue_t value, searchType_t type) {
  switch (type) {
  case SEARCH_TYPE_UNSIGNED_8BIT:
    return static_cast<long double>(value._u8);
  case SEARCH_TYPE_SIGNED_8BIT:
    return static_cast<long double>(value._s8);
  case SEARCH_TYPE_UNSIGNED_16BIT:
    return static_cast<long double>(value._u16);
  case SEARCH_TYPE_SIGNED_16BIT:
    return static_cast<long double>(value._s16);
  case SEARCH_TYPE_UNSIGNED_32BIT:
    return static_cast<long double>(value._u32);
  case SEARCH_TYPE_SIGNED_32BIT:
    return static_cast<long double>(value._s32);
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_POINTER:
  case SEARCH_TYPE_UNSIGNED_40BIT:
    return static_cast<long double>(value._u64);
  case SEARCH_TYPE_SIGNED_64BIT:
    return static_cast<long double>(value._s64);
  case SEARCH_TYPE_FLOAT_32BIT:
    return static_cast<long double>(value._f32);
  case SEARCH_TYPE_FLOAT_64BIT:
    return static_cast<long double>(value._f64);
  default:
    return 0.0L;
  }
}

long double ClampToRange(long double v, long double minV, long double maxV) {
  if (std::isnan(static_cast<double>(v))) {
    return 0.0L;
  }
  if (v < minV)
    return minV;
  if (v > maxV)
    return maxV;
  return v;
}

long double RoundNearest(long double v) { return std::nearbyintl(v); }

searchValue_t ConvertValueType(searchValue_t input, searchType_t fromType,
                               searchType_t toType) {
  searchValue_t out{};
  const long double n = ValueToNumeric(input, fromType);

  switch (toType) {
  case SEARCH_TYPE_UNSIGNED_8BIT: {
    const long double r = ClampToRange(RoundNearest(n), 0.0L, 255.0L);
    out._u8 = static_cast<u8>(r);
    break;
  }
  case SEARCH_TYPE_SIGNED_8BIT: {
    const long double r = ClampToRange(RoundNearest(n), -128.0L, 127.0L);
    out._s8 = static_cast<s8>(r);
    break;
  }
  case SEARCH_TYPE_UNSIGNED_16BIT: {
    const long double r = ClampToRange(RoundNearest(n), 0.0L, 65535.0L);
    out._u16 = static_cast<u16>(r);
    break;
  }
  case SEARCH_TYPE_SIGNED_16BIT: {
    const long double r = ClampToRange(RoundNearest(n), -32768.0L, 32767.0L);
    out._s16 = static_cast<s16>(r);
    break;
  }
  case SEARCH_TYPE_UNSIGNED_32BIT: {
    const long double r =
        ClampToRange(RoundNearest(n), 0.0L, 4294967295.0L);
    out._u32 = static_cast<u32>(r);
    break;
  }
  case SEARCH_TYPE_SIGNED_32BIT: {
    const long double r =
        ClampToRange(RoundNearest(n), -2147483648.0L, 2147483647.0L);
    out._s32 = static_cast<s32>(r);
    break;
  }
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_POINTER: {
    const long double r = ClampToRange(
        RoundNearest(n), 0.0L,
        static_cast<long double>(std::numeric_limits<u64>::max()));
    out._u64 = static_cast<u64>(r);
    break;
  }
  case SEARCH_TYPE_SIGNED_64BIT: {
    const long double r = ClampToRange(
        RoundNearest(n), static_cast<long double>(std::numeric_limits<s64>::min()),
        static_cast<long double>(std::numeric_limits<s64>::max()));
    out._s64 = static_cast<s64>(r);
    break;
  }
  case SEARCH_TYPE_FLOAT_32BIT: {
    const long double r = ClampToRange(
        n, -static_cast<long double>(std::numeric_limits<float>::max()),
        static_cast<long double>(std::numeric_limits<float>::max()));
    out._f32 = static_cast<float>(r);
    break;
  }
  case SEARCH_TYPE_FLOAT_64BIT: {
    const long double r = ClampToRange(
        n, -static_cast<long double>(std::numeric_limits<double>::max()),
        static_cast<long double>(std::numeric_limits<double>::max()));
    out._f64 = static_cast<double>(r);
    break;
  }
  case SEARCH_TYPE_UNSIGNED_40BIT: {
    const long double r = ClampToRange(
        RoundNearest(n), 0.0L, static_cast<long double>(0xFFFFFFFFFFULL));
    out._u64 = static_cast<u64>(r) & 0xFFFFFFFFFFULL;
    break;
  }
  default:
    break;
  }
  return out;
}

std::string SearchDataNote(const Search_condition &condition, int slot,
                           bool hexMode = false);

bool ParseValueFromText(const std::string &text, searchType_t type,
                        searchValue_t &outValue) {
  const std::string trimmed = TrimCopy(text);
  if (trimmed.empty()) {
    return false;
  }

  auto parseUnsigned = [&](u64 &value) -> bool {
    if (!trimmed.empty() && trimmed[0] == '-') {
      return false;
    }
    errno = 0;
    char *end = nullptr;
    const unsigned long long parsed =
        std::strtoull(trimmed.c_str(), &end, 0);
    if (errno != 0 || end == trimmed.c_str() || *end != '\0') {
      return false;
    }
    value = static_cast<u64>(parsed);
    return true;
  };

  auto parseSigned = [&](s64 &value) -> bool {
    errno = 0;
    char *end = nullptr;
    const long long parsed = std::strtoll(trimmed.c_str(), &end, 0);
    if (errno != 0 || end == trimmed.c_str() || *end != '\0') {
      return false;
    }
    value = static_cast<s64>(parsed);
    return true;
  };

  auto parseDouble = [&](double &value) -> bool {
    errno = 0;
    char *end = nullptr;
    const double parsed = std::strtod(trimmed.c_str(), &end);
    if (errno != 0 || end == trimmed.c_str() || *end != '\0') {
      return false;
    }
    value = parsed;
    return true;
  };

  switch (type) {
  case SEARCH_TYPE_UNSIGNED_8BIT: {
    u64 parsed = 0;
    if (!parseUnsigned(parsed) || parsed > 0xFFULL)
      return false;
    outValue._u8 = static_cast<u8>(parsed);
    return true;
  }
  case SEARCH_TYPE_SIGNED_8BIT: {
    s64 parsed = 0;
    if (!parseSigned(parsed) || parsed < -128 || parsed > 127)
      return false;
    outValue._s8 = static_cast<s8>(parsed);
    return true;
  }
  case SEARCH_TYPE_UNSIGNED_16BIT: {
    u64 parsed = 0;
    if (!parseUnsigned(parsed) || parsed > 0xFFFFULL)
      return false;
    outValue._u16 = static_cast<u16>(parsed);
    return true;
  }
  case SEARCH_TYPE_SIGNED_16BIT: {
    s64 parsed = 0;
    if (!parseSigned(parsed) || parsed < -32768 || parsed > 32767)
      return false;
    outValue._s16 = static_cast<s16>(parsed);
    return true;
  }
  case SEARCH_TYPE_UNSIGNED_32BIT: {
    u64 parsed = 0;
    if (!parseUnsigned(parsed) || parsed > 0xFFFFFFFFULL)
      return false;
    outValue._u32 = static_cast<u32>(parsed);
    return true;
  }
  case SEARCH_TYPE_SIGNED_32BIT: {
    s64 parsed = 0;
    if (!parseSigned(parsed) || parsed < -2147483648LL ||
        parsed > 2147483647LL)
      return false;
    outValue._s32 = static_cast<s32>(parsed);
    return true;
  }
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_POINTER: {
    u64 parsed = 0;
    if (!parseUnsigned(parsed))
      return false;
    outValue._u64 = parsed;
    return true;
  }
  case SEARCH_TYPE_SIGNED_64BIT: {
    s64 parsed = 0;
    if (!parseSigned(parsed))
      return false;
    outValue._s64 = parsed;
    return true;
  }
  case SEARCH_TYPE_FLOAT_32BIT: {
    double parsed = 0.0;
    if (!parseDouble(parsed))
      return false;
    outValue._f32 = static_cast<float>(parsed);
    return true;
  }
  case SEARCH_TYPE_FLOAT_64BIT: {
    double parsed = 0.0;
    if (!parseDouble(parsed))
      return false;
    outValue._f64 = parsed;
    return true;
  }
  case SEARCH_TYPE_UNSIGNED_40BIT: {
    u64 parsed = 0;
    if (!parseUnsigned(parsed))
      return false;
    outValue._u64 = parsed & 0xFFFFFFFFFFULL;
    return true;
  }
  default:
    return false;
  }
}

std::string SearchConditionSummary(const Search_condition &condition) {
  auto isTokenBoundary = [](char c) -> bool {
    return !std::isalnum(static_cast<unsigned char>(c)) && c != '_';
  };
  auto replaceStandaloneToken = [&](std::string text, char token,
                                    const std::string &value) -> std::string {
    size_t i = 0;
    while (i < text.size()) {
      if (text[i] != token) {
        ++i;
        continue;
      }
      const bool leftOk = (i == 0) || isTokenBoundary(text[i - 1]);
      const bool rightOk = (i + 1 >= text.size()) || isTokenBoundary(text[i + 1]);
      if (!leftOk || !rightOk) {
        ++i;
        continue;
      }
      text.replace(i, 1, value);
      i += value.size();
    }
    return text;
  };

  std::string modeText = SearchModeLabel(condition.searchMode);
  modeText = replaceStandaloneToken(modeText, 'A', SearchDataNote(condition, 0));
  modeText = replaceStandaloneToken(modeText, 'B', SearchDataNote(condition, 1));
  modeText = replaceStandaloneToken(modeText, 'C', SearchDataNote(condition, 2));
  return std::string(SearchTypeLabel(condition.searchType)) + " " + modeText;
}

std::string CandidateStemFromPath(const std::string &path) {
  size_t slash = path.find_last_of("/\\");
  const std::string file = (slash == std::string::npos) ? path : path.substr(slash + 1);
  if (file.size() > 4 && file.substr(file.size() - 4) == ".dat") {
    return file.substr(0, file.size() - 4);
  }
  return file;
}

std::string CandidatePathFromStem(std::string stem) {
  if (stem.size() > 4 && stem.substr(stem.size() - 4) == ".dat") {
    stem.erase(stem.size() - 4);
  }
  if (stem.empty()) {
    stem = "1";
  }
  return std::string("sdmc:/switch/Breeze/") + stem + ".dat";
}

bool CandidateFileExistsForStem(const std::string &stem) {
  if (stem.empty()) {
    return false;
  }
  const std::string path = CandidatePathFromStem(stem);
  struct stat st {};
  return stat(path.c_str(), &st) == 0;
}

std::string CandidateStatusFromHeader(const breeze::BreezeFileHeader_t &header) {
  auto modeCode = [](searchMode_t mode) -> const char * {
    switch (mode) {
    case SM_EQ:
      return "==A";
    case SM_NE:
      return "!=A";
    case SM_GT:
      return ">A";
    case SM_LT:
      return "<A";
    case SM_GE:
      return ">=A";
    case SM_LE:
      return "<=A";
    case SM_RANGE_EQ:
      return "[A..B]";
    case SM_BMEQ:
      return "&B=A";
    case SM_RANGE_LT:
      return "<A..B>";
    case SM_MORE:
      return "++";
    case SM_LESS:
      return "--";
    case SM_DIFF:
      return "DIFF";
    case SM_SAME:
      return "SAME";
    case SM_TWO_VALUE:
      return "[A,B]";
    case SM_TWO_VALUE_PLUS:
      return "[A,,B]";
    case SM_STRING:
      return "STRING";
    case SM_INC_BY:
      return "++Val";
    case SM_DEC_BY:
      return "--Val";
    case SM_EQ_plus:
      return "==*A";
    case SM_EQ_plus_plus:
      return "==**A";
    case SM_NONE:
      return "NONE";
    case SM_DIFFB:
      return "DIFFB";
    case SM_SAMEB:
      return "SAMEB";
    case SM_MOREB:
      return "B++";
    case SM_LESSB:
      return "B--";
    case SM_NOTAB:
      return "NotAB";
    case SM_THREE_VALUE:
      return "[A.B.C]";
    case SM_BIT_FLIP:
      return "[A bflip B]";
    case SM_ADV:
      return "Advance";
    case SM_GAP:
      return "GAP";
    case SM_GAP_ALLOWANCE:
      return "{GAP}";
    case SM_PTR:
      return "PTR";
    case SM_NPTR:
      return "~PTR";
    case SM_NoDecimal:
      return "[A..B]f.0";
    case SM_Gen2_data:
      return "Gen2 data";
    case SM_Gen2_code:
      return "Gen2 code";
    case SM_GETB:
      return "GETB";
    case SM_REBASE:
      return "REBASE";
    case SM_Target:
      return "Target";
    case SM_Pointer_and_OFFSET:
      return "ptr+off";
    case SM_SKIP:
      return "skip";
    case SM_Aborted_Target:
      return "Aborted Target";
    case SM_Branch:
      return "Branch";
    case SM_LDRx:
      return "LDRx";
    case SM_ADRP:
      return "ADRP";
    case SM_EOR:
      return "EOR";
    case SM_GETBZ:
      return "GETB==A";
    default:
      return "?";
    }
  };

  auto stepCode = [](search_step_t step) -> const char * {
    switch (step) {
    case search_step_primary:
      return "Primary";
    case search_step_secondary:
      return "Secondary";
    case search_step_dump:
      return "Dump";
    case search_step_dump_compare:
      return "DumpCmp";
    case search_step_none:
      return "None";
    case search_target:
      return "Target";
    case search_step_dump_segment:
      return "DumpSeg";
    case search_step_save_memory_edit:
      return "SaveEdit";
    default:
      return "Unknown";
    }
  };

  auto isTokenBoundary = [](char c) -> bool {
    return !std::isalnum(static_cast<unsigned char>(c)) && c != '_';
  };
  auto replaceStandaloneToken = [&](std::string text, char token,
                                    const std::string &value) -> std::string {
    size_t i = 0;
    while (i < text.size()) {
      if (text[i] != token) {
        ++i;
        continue;
      }
      const bool leftOk = (i == 0) || isTokenBoundary(text[i - 1]);
      const bool rightOk = (i + 1 >= text.size()) || isTokenBoundary(text[i + 1]);
      if (!leftOk || !rightOk) {
        ++i;
        continue;
      }
      text.replace(i, 1, value);
      i += value.size();
    }
    return text;
  };

  const u64 entryCount = header.dataSize / 16;
  std::string modeText = modeCode(header.search_condition.searchMode);
  modeText = replaceStandaloneToken(
      modeText, 'A', SearchDataNote(header.search_condition, 0));
  modeText = replaceStandaloneToken(
      modeText, 'B', SearchDataNote(header.search_condition, 1));
  modeText = replaceStandaloneToken(
      modeText, 'C', SearchDataNote(header.search_condition, 2));
  const std::string previousFile =
      (header.prefilename[0] != '\0')
          ? (std::string(" p=") + std::string(header.prefilename))
          : std::string();
  char compact[512] = {};
  std::snprintf(compact, sizeof(compact), "%llu %s Search %s %s%s",
                static_cast<unsigned long long>(entryCount),
                stepCode(header.search_condition.search_step),
                SearchTypeLabel(header.search_condition.searchType),
                modeText.c_str(),
                previousFile.c_str());

  if (header.search_condition.searchMode == SM_Target ||
      header.search_condition.searchMode == SM_Aborted_Target) {
    char buf[192] = {};
    std::snprintf(
        buf, sizeof(buf), "%sSource=%dK Target=%dK New=%dK TimeTaken=%ds bm=%d sr=%04X",
        (header.search_condition.searchMode == SM_Aborted_Target) ? "Aborted " : "",
        static_cast<int>(header.from_to_size / 16 / 1000),
        static_cast<int>((header.dataSize - header.from_to_size) / 16 / 1000),
        static_cast<int>(header.new_targets / 1000),
        static_cast<int>(header.timetaken),
        static_cast<int>(header.bit_mask),
        static_cast<unsigned>(header.ptr_search_range));
    return std::string(compact) + " " + buf;
  }
  return compact;
}

std::string CandidateStatusFromPath(const std::string &path) {
  breeze::BreezeFileHeader_t header{};
  std::string err;
  if (!breeze::ReadCandidateHeader(path, header, &err)) {
    return "Invalid candidate file";
  }
  const std::string targetStatus = CandidateStatusFromHeader(header);
  if (!targetStatus.empty()) {
    return targetStatus;
  }
  return std::string("Mode=") + SearchModeLabel(header.search_condition.searchMode) +
         " Type=" + SearchTypeLabel(header.search_condition.searchType) +
         " Step=" + std::to_string(static_cast<int>(header.search_condition.search_step));
}

std::string ContinueSearchNoteFromPath(const std::string &path) {
  if (path.empty()) {
    return "No candidate file";
  }
  const std::string status = CandidateStatusFromPath(path);
  const std::string stem = CandidateStemFromPath(path);
  if (stem.empty()) {
    return status;
  }
  return status + " file=" + stem;
}

std::string AutoGenerateContinueOutputName(const std::string &sourcePath);
std::string NormalizeSeriesStartStem(const std::string &stem);
void QueueSearchAction(SearchQueuedAction action);

bool TryQueueContinueSearchFromUi() {
  if (!checkOverlayMemory(8)) {
    return true;
  }
  if (g_searchContinueSourcePath.empty()) {
    if (tsl::notification) {
      tsl::notification->show("No source candidate selected");
    }
    return true;
  }
  g_continueSearchOutputName =
      AutoGenerateContinueOutputName(g_searchContinueSourcePath);
  if (CandidateFileExistsForStem(g_continueSearchOutputName)) {
    tsl::changeTo<tsl::KeyboardGui>(
        SEARCH_TYPE_TEXT, g_continueSearchOutputName, "Output exists, rename",
        [](std::string result) {
          const std::string trimmed = TrimCopy(result);
          if (trimmed.empty()) {
            if (tsl::notification) {
              tsl::notification->show("File name cannot be empty");
            }
            return;
          }
          g_continueSearchOutputName = trimmed;
          if (g_continueSearchOutputName.size() > 4 &&
              g_continueSearchOutputName.substr(g_continueSearchOutputName.size() - 4) ==
                  ".dat") {
            g_continueSearchOutputName.erase(g_continueSearchOutputName.size() - 4);
          }
          g_continueSearchOutputName =
              NormalizeSeriesStartStem(g_continueSearchOutputName);
          if (CandidateFileExistsForStem(g_continueSearchOutputName)) {
            if (tsl::notification) {
              tsl::notification->show("Output file already exists");
            }
            return;
          }
          tsl::goBack();
          QueueSearchAction(SEARCH_QUEUED_CONTINUE);
        },
        nullptr, false);
    return true;
  }
  QueueSearchAction(SEARCH_QUEUED_CONTINUE);
  return true;
}

std::string LastSearchBufferNote() {
  auto formatBytes = [](size_t bytes) -> std::string {
    const unsigned long long kb = static_cast<unsigned long long>(bytes / 1024ULL);
    char part[64] = {};
    std::snprintf(part, sizeof(part), "%llu KB", kb);
    return part;
  };

  const bool inProgress = g_searchInProgress;
  const u8 bufferCount = inProgress ? g_searchProgressBufferCount
                                    : g_lastSearchBufferCount;
  const size_t primaryBytes = inProgress ? g_searchProgressPrimaryBufferBytes
                                         : g_lastSearchPrimaryBufferBytes;
  const size_t secondaryBytes = inProgress ? g_searchProgressSecondaryBufferBytes
                                           : g_lastSearchSecondaryBufferBytes;
  const size_t outputBytes = inProgress ? g_searchProgressOutputBufferBytes
                                        : g_lastSearchOutputBufferBytes;

  if (!inProgress && !g_lastSearchStatsValid) {
    return "N/A";
  }

  if (bufferCount >= 3 && primaryBytes > 0 && secondaryBytes > 0 && outputBytes > 0) {
    return "In=" + formatBytes(primaryBytes) + " Mem=" + formatBytes(secondaryBytes) +
           " Out=" + formatBytes(outputBytes);
  }
  if (bufferCount >= 2 && primaryBytes > 0 && outputBytes > 0) {
    return "Scan=" + formatBytes(primaryBytes) + " Out=" + formatBytes(outputBytes);
  }
  if (primaryBytes > 0) {
    return formatBytes(primaryBytes);
  }
  return "N/A";
}

std::string LastSearchTimeNote() {
  if (g_searchInProgress) {
    if (g_searchWorkerPaused.load(std::memory_order_acquire)) {
      return "Paused (A=Resume B=Abort)";
    }
    if (g_searchActiveAction == SEARCH_QUEUED_START) {
      const unsigned long long mb = static_cast<unsigned long long>(
          g_searchProgressCurrent.load(std::memory_order_acquire) / 1024ULL / 1024ULL);
      char buf[64] = {};
      std::snprintf(buf, sizeof(buf), "%llu MB scanned", mb);
      return buf;
    }
    if (g_searchActiveAction == SEARCH_QUEUED_CONTINUE) {
      const u64 total = g_searchProgressTotal.load(std::memory_order_acquire);
      const u64 current = g_searchProgressCurrent.load(std::memory_order_acquire);
      if (total > 0) {
        const unsigned pct = static_cast<unsigned>(
            std::min<u64>(100, (current * 100ULL) / total));
        char buf[64] = {};
        std::snprintf(buf, sizeof(buf), "%u%% processed", pct);
        return buf;
      }
      return "Processing...";
    }
    return "Search in progress";
  }
  if (!g_lastSearchStatsValid) {
    return "N/A";
  }
  char buf[64] = {};
  std::snprintf(buf, sizeof(buf), "%u s", static_cast<unsigned>(g_lastSearchSeconds));
  return buf;
}

const char *SearchTimeItemTitle() {
  return g_searchInProgress ? "Search in progress" : "Time taken";
}

std::string AutoGenerateContinueOutputName(const std::string &sourcePath);
std::string AutoGenerateStartOutputName();
std::string FormatSeriesStem(const std::string &base, int index);
std::string NormalizeSeriesStartStem(const std::string &stem);
bool ParseSeriesSuffix(const std::string &stem, std::string &baseOut,
                       int &indexOut);

void ClearLastSearchStatsForUi() {
  g_lastSearchStatsValid = false;
  g_lastSearchPrimaryBufferBytes = 0;
  g_lastSearchSecondaryBufferBytes = 0;
  g_lastSearchOutputBufferBytes = 0;
  g_lastSearchBufferCount = 0;
  g_lastSearchSeconds = 0;
}

void SetSearchInProgressBufferPreview(SearchQueuedAction action) {
  // Keep in sync with common/breeze_search_exec.cpp constants.
  static constexpr size_t kFixedScanBuffer = 2 * 1024 * 1024;
  static constexpr size_t kOutputBuffer = 512 * 1024;
  static constexpr size_t kContinueInputBuffer = kFixedScanBuffer / 2;
  static constexpr size_t kContinueMemoryBuffer = kFixedScanBuffer / 2;

  g_searchInProgress = true;
  if (action == SEARCH_QUEUED_START) {
    g_searchProgressPrimaryBufferBytes = kFixedScanBuffer;
    g_searchProgressSecondaryBufferBytes = 0;
    g_searchProgressOutputBufferBytes = kOutputBuffer;
    g_searchProgressBufferCount = 2;
    return;
  }
  if (action == SEARCH_QUEUED_CONTINUE) {
    g_searchProgressPrimaryBufferBytes = kContinueInputBuffer;
    g_searchProgressSecondaryBufferBytes = kContinueMemoryBuffer;
    g_searchProgressOutputBufferBytes = kOutputBuffer;
    g_searchProgressBufferCount = 3;
    return;
  }
  g_searchProgressPrimaryBufferBytes = 0;
  g_searchProgressSecondaryBufferBytes = 0;
  g_searchProgressOutputBufferBytes = 0;
  g_searchProgressBufferCount = 0;
}

void QueueSearchAction(SearchQueuedAction action) {
  ClearLastSearchStatsForUi();
  SetSearchInProgressBufferPreview(action);
  g_searchProgressCurrent.store(0, std::memory_order_release);
  g_searchProgressTotal.store(0, std::memory_order_release);
  g_searchPauseRequested.store(false, std::memory_order_release);
  g_searchAbortRequested.store(false, std::memory_order_release);
  g_searchWorkerPaused.store(false, std::memory_order_release);
  g_searchQueuedAction = action;
  g_searchQueuedDelayTicks = 1;
}

void StopSearchWorker() {
  g_searchQueuedAction = SEARCH_QUEUED_NONE;
  g_searchQueuedDelayTicks = 0;
  if (g_searchWorkerRunning.load(std::memory_order_acquire)) {
    g_searchAbortRequested.store(true, std::memory_order_release);
    g_searchPauseRequested.store(false, std::memory_order_release);
    while (g_searchWorkerRunning.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  if (g_searchWorkerThread.joinable()) {
    g_searchWorkerThread.join();
  }
  g_searchWorkerDone.store(false, std::memory_order_release);
  g_searchInProgress = false;
  g_searchActiveAction = SEARCH_QUEUED_NONE;
}

void BeginQueuedSearchWorker(SearchQueuedAction action) {
  if (g_searchWorkerThread.joinable()) {
    g_searchWorkerThread.join();
  }

  g_searchActiveAction = action;
  g_searchWorkerStats = breeze::SearchRunStats{};
  g_searchWorkerError.clear();
  g_searchWorkerSuccess = false;

  const Search_condition condition = g_searchCondition;
  const std::string sourcePath = g_searchContinueSourcePath;
  const std::string outputStem =
      (action == SEARCH_QUEUED_START)
          ? NormalizeSeriesStartStem(g_startSearchOutputName)
          : g_continueSearchOutputName;
  g_searchWorkerOutputStem = outputStem;

  g_searchWorkerDone.store(false, std::memory_order_release);
  g_searchWorkerRunning.store(true, std::memory_order_release);
  g_searchWorkerThread = std::thread([condition, sourcePath, outputStem, action]() {
    breeze::SearchRunStats stats{};
    std::string err;
    breeze::SearchRunControl control{};
    control.pauseRequested = &g_searchPauseRequested;
    control.abortRequested = &g_searchAbortRequested;
    control.progressCurrent = &g_searchProgressCurrent;
    control.progressTotal = &g_searchProgressTotal;
    control.isPaused = &g_searchWorkerPaused;

    bool ok = false;
    if (action == SEARCH_QUEUED_START) {
      ok = breeze::RunStartSearch(condition, outputStem, stats, &err, &control);
    } else {
      ok = breeze::RunContinueSearch(condition, sourcePath, outputStem, stats, &err,
                                     &control);
    }
    g_searchWorkerStats = stats;
    g_searchWorkerError = err;
    g_searchWorkerSuccess = ok;
    g_searchWorkerRunning.store(false, std::memory_order_release);
    g_searchWorkerDone.store(true, std::memory_order_release);
  });
}

void FinalizeCompletedSearchWorker() {
  if (g_searchWorkerThread.joinable()) {
    g_searchWorkerThread.join();
  }
  g_searchInProgress = false;

  if (!g_searchWorkerSuccess) {
    return;
  }

  g_lastSearchStatsValid = true;
  g_lastSearchPrimaryBufferBytes = g_searchWorkerStats.primaryBufferBytes;
  g_lastSearchSecondaryBufferBytes = g_searchWorkerStats.secondaryBufferBytes;
  g_lastSearchOutputBufferBytes = g_searchWorkerStats.outputBufferBytes;
  g_lastSearchBufferCount = g_searchWorkerStats.bufferCount;
  g_lastSearchSeconds = g_searchWorkerStats.secondsTaken;

  if (g_searchActiveAction == SEARCH_QUEUED_START) {
    g_searchContinueSourcePath = CandidatePathFromStem(g_searchWorkerOutputStem);
    g_searchConditionSourcePath = g_searchContinueSourcePath;
    g_startSearchOutputName = AutoGenerateStartOutputName();
    g_continueSearchOutputName =
        AutoGenerateContinueOutputName(g_searchContinueSourcePath);
    return;
  }

  g_searchContinueSourcePath = CandidatePathFromStem(g_searchWorkerOutputStem);
  g_searchConditionSourcePath = g_searchContinueSourcePath;
  g_continueSearchOutputName =
      AutoGenerateContinueOutputName(g_searchContinueSourcePath);
}

bool GetLatestCandidatePath(std::string &outPath) {
  const auto files = breeze::ListCandidateFiles();
  if (files.empty()) {
    return false;
  }
  std::string best;
  time_t bestTime = 0;
  for (const auto &path : files) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) {
      continue;
    }
    if (best.empty() || st.st_mtime > bestTime) {
      best = path;
      bestTime = st.st_mtime;
    }
  }
  if (best.empty()) {
    return false;
  }
  outPath = best;
  return true;
}

size_t PurgeInvalidCandidatesForCurrentProcess() {
  DmntCheatProcessMetadata currentMetadata{};
  if (R_FAILED(dmntchtGetCheatProcessMetadata(&currentMetadata))) {
    return 0;
  }

  const u64 currentMainBase = currentMetadata.main_nso_extents.base;
  size_t removedCount = 0;
  for (const auto &path : breeze::ListCandidateFiles()) {
    breeze::BreezeFileHeader_t header{};
    std::string err;
    const bool valid = breeze::ReadCandidateHeader(path, header, &err);
    const bool mainBaseMatches =
        valid && (header.Metadata.main_nso_extents.base == currentMainBase);
    if (mainBaseMatches) {
      continue;
    }
    if (std::remove(path.c_str()) == 0) {
      ++removedCount;
    }
  }
  return removedCount;
}

std::string AutoGenerateStartOutputName() {
  std::set<int> usedNumericBases;
  for (const auto &p : breeze::ListCandidateFiles()) {
    const std::string stem = CandidateStemFromPath(p);
    if (stem.empty()) {
      continue;
    }

    size_t pos = 0;
    while (pos < stem.size() && std::isdigit(static_cast<unsigned char>(stem[pos]))) {
      ++pos;
    }
    if (pos == 0) {
      continue;
    }
    if (pos == stem.size()) {
      usedNumericBases.insert(std::atoi(stem.c_str()));
      continue;
    }

    // Accept only strict numeric-series form: "<digits>(<digits>)".
    if (stem[pos] != '(' || stem.back() != ')') {
      continue;
    }
    bool suffixDigitsOnly = true;
    for (size_t i = pos + 1; i + 1 < stem.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(stem[i]))) {
        suffixDigitsOnly = false;
        break;
      }
    }
    if (!suffixDigitsOnly || pos + 2 >= stem.size()) {
      continue;
    }
    usedNumericBases.insert(std::atoi(stem.substr(0, pos).c_str()));
  }

  // First free numeric series base (1,2,3...) and always start at (00).
  for (int i = 1; i < 10000; ++i) {
    if (!usedNumericBases.count(i)) {
      return FormatSeriesStem(std::to_string(i), 0);
    }
  }
  return "1(00)";
}

bool ParseSeriesSuffix(const std::string &stem, std::string &baseOut,
                       int &indexOut) {
  if (stem.size() < 4 || stem.back() != ')') {
    return false;
  }
  const size_t open = stem.rfind('(');
  if (open == std::string::npos || open >= stem.size() - 2) {
    return false;
  }
  const std::string digits = stem.substr(open + 1, stem.size() - open - 2);
  for (char c : digits) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  baseOut = stem.substr(0, open);
  indexOut = std::atoi(digits.c_str());
  return true;
}

std::string FormatSeriesStem(const std::string &base, int index) {
  char suffix[32] = {};
  std::snprintf(suffix, sizeof(suffix), "(%02d)", index);
  return base + suffix;
}

std::string NormalizeSeriesStartStem(const std::string &stem) {
  if (stem.empty()) {
    return stem;
  }
  std::string parsedBase;
  int parsedIndex = 0;
  if (ParseSeriesSuffix(stem, parsedBase, parsedIndex)) {
    return stem;
  }
  return FormatSeriesStem(stem, 0);
}

std::string DisplayStartOutputStem(const std::string &stem) {
  std::string base;
  int parsedIndex = 0;
  if (ParseSeriesSuffix(stem, base, parsedIndex) && parsedIndex == 0) {
    return base;
  }
  return stem;
}

std::string AutoGenerateContinueOutputName(const std::string &sourcePath) {
  const std::string sourceStem = CandidateStemFromPath(sourcePath);
  std::string base = sourceStem;
  int nextIndex = 0;

  std::string parsedBase;
  int parsedIndex = 0;
  if (ParseSeriesSuffix(sourceStem, parsedBase, parsedIndex)) {
    base = parsedBase;
    nextIndex = parsedIndex + 1;
  } else {
    nextIndex = 0;
  }

  return FormatSeriesStem(base, nextIndex);
}

std::string SeriesBaseFromStem(const std::string &stem) {
  std::string base;
  int parsedIndex = 0;
  if (ParseSeriesSuffix(stem, base, parsedIndex)) {
    return base;
  }
  return stem;
}

bool IsSeriesStartStem(const std::string &stem) {
  std::string base;
  int parsedIndex = 0;
  if (!ParseSeriesSuffix(stem, base, parsedIndex)) {
    // Legacy/no-suffix file names are treated as series starts.
    return true;
  }
  // With current naming, "(00)" is the first item of a series.
  return parsedIndex == 0;
}

int SeriesIndexFromStem(const std::string &stem) {
  std::string parsedBase;
  int parsedIndex = 0;
  if (ParseSeriesSuffix(stem, parsedBase, parsedIndex)) {
    return parsedIndex;
  }
  return 0;
}

std::string SeriesEndStemForBase(const std::string &base,
                                 const std::vector<std::string> &files) {
  std::string bestStem;
  int bestIndex = std::numeric_limits<int>::min();
  for (const auto &path : files) {
    const std::string stem = CandidateStemFromPath(path);
    if (SeriesBaseFromStem(stem) != base) {
      continue;
    }
    const int index = SeriesIndexFromStem(stem);
    if (bestStem.empty() || index > bestIndex ||
        (index == bestIndex && stem > bestStem)) {
      bestStem = stem;
      bestIndex = index;
    }
  }
  return bestStem;
}

std::string SeriesEndStemForBase(const std::string &base) {
  return SeriesEndStemForBase(base, breeze::ListCandidateFiles());
}

std::string FindCandidatePathByStem(const std::vector<std::string> &files,
                                    const std::string &stem) {
  if (stem.empty()) {
    return std::string();
  }
  for (const auto &path : files) {
    if (CandidateStemFromPath(path) == stem) {
      return path;
    }
  }
  return std::string();
}

bool SelectContinueSourceAfterDeletion(const std::string &preferredBase) {
  const auto files = breeze::ListCandidateFiles();
  if (files.empty()) {
    g_searchContinueSourcePath.clear();
    g_searchConditionSourcePath.clear();
    g_continueSourceJumpStem.clear();
    return false;
  }

  std::string targetPath;
  const std::string sameSeriesEnd = SeriesEndStemForBase(preferredBase, files);
  if (!sameSeriesEnd.empty()) {
    targetPath = FindCandidatePathByStem(files, sameSeriesEnd);
  }
  if (targetPath.empty()) {
    GetLatestCandidatePath(targetPath);
  }
  if (targetPath.empty()) {
    g_searchContinueSourcePath.clear();
    g_searchConditionSourcePath.clear();
    g_continueSourceJumpStem.clear();
    return false;
  }

  g_searchContinueSourcePath = targetPath;
  if (g_searchConditionSourcePath.empty()) {
    g_searchConditionSourcePath = targetPath;
  }
  g_continueSourceJumpStem = CandidateStemFromPath(targetPath);
  return true;
}

std::string CandidatePathFromStemInSameFolder(const std::string &sourcePath,
                                              const std::string &targetStem) {
  const size_t slash = sourcePath.find_last_of("/\\");
  const std::string folder =
      (slash == std::string::npos) ? std::string() : sourcePath.substr(0, slash + 1);
  return folder + targetStem + ".dat";
}

struct CandidateSeriesRenamePlanEntry {
  std::string fromPath;
  std::string toPath;
};

bool RenameCandidateSeries(const std::string &selectedPath,
                           const std::string &newBaseInput,
                           std::vector<CandidateSeriesRenamePlanEntry> &renamesOut,
                           std::string &renamedSelectedPath,
                           std::string *errorOut = nullptr) {
  renamesOut.clear();
  renamedSelectedPath = selectedPath;
  if (selectedPath.empty()) {
    if (errorOut) {
      *errorOut = "No source candidate selected";
    }
    return false;
  }

  const std::string selectedStem = CandidateStemFromPath(selectedPath);
  if (selectedStem.empty()) {
    if (errorOut) {
      *errorOut = "Invalid source candidate";
    }
    return false;
  }
  const std::string oldBase = SeriesBaseFromStem(selectedStem);

  std::string newBase = TrimCopy(newBaseInput);
  if (newBase.size() > 4 && newBase.substr(newBase.size() - 4) == ".dat") {
    newBase.erase(newBase.size() - 4);
  }
  if (newBase.empty()) {
    if (errorOut) {
      *errorOut = "Series name cannot be empty";
    }
    return false;
  }
  if (newBase.find_first_of("/\\") != std::string::npos) {
    if (errorOut) {
      *errorOut = "Series name cannot contain / or \\";
    }
    return false;
  }

  // Parentheses break strict "<base>(<index>)" parsing for series suffixes.
  if (newBase.find('(') != std::string::npos ||
      newBase.find(')') != std::string::npos) {
    if (errorOut) {
      *errorOut = "Series name cannot contain parentheses";
    }
    return false;
  }

  if (newBase == oldBase) {
    return true;
  }

  std::vector<CandidateSeriesRenamePlanEntry> plan;
  std::set<std::string> sourcePaths;
  std::set<std::string> targetPaths;

  const auto files = breeze::ListCandidateFiles();
  for (const auto &path : files) {
    const std::string stem = CandidateStemFromPath(path);
    if (SeriesBaseFromStem(stem) != oldBase) {
      continue;
    }

    std::string parsedBase;
    int parsedIndex = 0;
    const bool hasSuffix = ParseSeriesSuffix(stem, parsedBase, parsedIndex);
    const int targetIndex = hasSuffix ? parsedIndex : 0;
    const std::string targetStem = FormatSeriesStem(newBase, targetIndex);
    const std::string targetPath = CandidatePathFromStemInSameFolder(path, targetStem);

    if (!targetPaths.insert(targetPath).second) {
      if (errorOut) {
        *errorOut = "Rename would create duplicate targets";
      }
      return false;
    }
    sourcePaths.insert(path);
    plan.push_back({path, targetPath});
  }

  if (plan.empty()) {
    if (errorOut) {
      *errorOut = "No files found for this series";
    }
    return false;
  }

  for (const auto &entry : plan) {
    if (entry.fromPath == entry.toPath) {
      continue;
    }
    struct stat st {};
    if (stat(entry.toPath.c_str(), &st) == 0 &&
        !sourcePaths.count(entry.toPath)) {
      if (errorOut) {
        *errorOut = "Target file already exists: " + CandidateStemFromPath(entry.toPath);
      }
      return false;
    }
  }

  std::vector<size_t> renamedIndices;
  for (size_t i = 0; i < plan.size(); ++i) {
    const auto &entry = plan[i];
    if (entry.fromPath == entry.toPath) {
      continue;
    }
    if (std::rename(entry.fromPath.c_str(), entry.toPath.c_str()) != 0) {
      for (auto it = renamedIndices.rbegin(); it != renamedIndices.rend(); ++it) {
        const auto &rollbackEntry = plan[*it];
        std::rename(rollbackEntry.toPath.c_str(), rollbackEntry.fromPath.c_str());
      }
      if (errorOut) {
        *errorOut = "Rename failed: " + std::string(std::strerror(errno));
      }
      return false;
    }
    renamedIndices.push_back(i);
  }

  renamesOut = plan;
  for (const auto &entry : renamesOut) {
    if (entry.fromPath == selectedPath) {
      renamedSelectedPath = entry.toPath;
      break;
    }
  }
  return true;
}

void ApplyCandidateRenameToPath(std::string &path,
                                const std::vector<CandidateSeriesRenamePlanEntry> &renames) {
  for (const auto &entry : renames) {
    if (path == entry.fromPath) {
      path = entry.toPath;
      return;
    }
  }
}

size_t DeleteCandidateSeries(const std::string &selectedPath,
                             bool fromSelectedForwardOnly,
                             std::vector<std::string> &deletedPathsOut,
                             size_t &failedCountOut,
                             std::string *errorOut = nullptr) {
  deletedPathsOut.clear();
  failedCountOut = 0;
  if (selectedPath.empty()) {
    if (errorOut) {
      *errorOut = "No source candidate selected";
    }
    return 0;
  }

  const std::string selectedStem = CandidateStemFromPath(selectedPath);
  if (selectedStem.empty()) {
    if (errorOut) {
      *errorOut = "Invalid source candidate";
    }
    return 0;
  }
  const std::string base = SeriesBaseFromStem(selectedStem);
  const int selectedIndex = SeriesIndexFromStem(selectedStem);

  std::vector<std::string> targets;
  const auto files = breeze::ListCandidateFiles();
  for (const auto &path : files) {
    const std::string stem = CandidateStemFromPath(path);
    if (SeriesBaseFromStem(stem) != base) {
      continue;
    }
    if (fromSelectedForwardOnly && SeriesIndexFromStem(stem) < selectedIndex) {
      continue;
    }
    targets.push_back(path);
  }
  if (targets.empty()) {
    if (errorOut) {
      *errorOut = fromSelectedForwardOnly ? "No files found from selected sequence"
                                          : "No files found for this series";
    }
    return 0;
  }

  for (const auto &path : targets) {
    if (std::remove(path.c_str()) == 0) {
      deletedPathsOut.push_back(path);
    } else {
      ++failedCountOut;
    }
  }

  if (deletedPathsOut.empty()) {
    if (errorOut) {
      *errorOut = "Failed to delete series files";
    }
  }
  return deletedPathsOut.size();
}

struct CandidateRecordView {
  u64 address = 0;
  u64 value = 0;
};

size_t CandidateValueWidth(searchType_t type) {
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

searchValue_t SearchValueFromRaw(u64 raw, searchType_t type) {
  searchValue_t out{};
  switch (type) {
  case SEARCH_TYPE_UNSIGNED_8BIT:
  case SEARCH_TYPE_SIGNED_8BIT: {
    const u8 v = static_cast<u8>(raw & 0xFFULL);
    std::memcpy(&out, &v, sizeof(v));
    break;
  }
  case SEARCH_TYPE_UNSIGNED_16BIT:
  case SEARCH_TYPE_SIGNED_16BIT: {
    const u16 v = static_cast<u16>(raw & 0xFFFFULL);
    std::memcpy(&out, &v, sizeof(v));
    break;
  }
  case SEARCH_TYPE_UNSIGNED_32BIT:
  case SEARCH_TYPE_SIGNED_32BIT:
  case SEARCH_TYPE_FLOAT_32BIT: {
    const u32 v = static_cast<u32>(raw & 0xFFFFFFFFULL);
    std::memcpy(&out, &v, sizeof(v));
    break;
  }
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_SIGNED_64BIT:
  case SEARCH_TYPE_FLOAT_64BIT:
  case SEARCH_TYPE_POINTER: {
    const u64 v = raw;
    std::memcpy(&out, &v, sizeof(v));
    break;
  }
  case SEARCH_TYPE_UNSIGNED_40BIT:
    out._u64 = raw & 0xFFFFFFFFFFULL;
    break;
  default:
    break;
  }
  return out;
}

std::string FormatCandidateRegionAddress(u64 address,
                                         const DmntCheatProcessMetadata &metadata) {
  auto tryRegion = [&](u64 base, u64 size, const char *label) -> std::string {
    if (size == 0 || address < base || address >= base + size) {
      return "";
    }
    const u64 offset = address - base;
    char buf[64] = {};
    std::snprintf(buf, sizeof(buf), "[%s+%llX]", label,
                  static_cast<unsigned long long>(offset));
    return buf;
  };

  std::string out = tryRegion(metadata.alias_extents.base, metadata.alias_extents.size, "A");
  if (!out.empty()) {
    return out;
  }
  out = tryRegion(metadata.heap_extents.base, metadata.heap_extents.size, "H");
  if (!out.empty()) {
    return out;
  }
  out = tryRegion(metadata.main_nso_extents.base, metadata.main_nso_extents.size, "M");
  if (!out.empty()) {
    return out;
  }
  out = tryRegion(metadata.address_space_extents.base, metadata.address_space_extents.size, "AS");
  if (!out.empty()) {
    return out;
  }

  char absBuf[64] = {};
  std::snprintf(absBuf, sizeof(absBuf), "[0x%llX]",
                static_cast<unsigned long long>(address));
  return absBuf;
}

std::string FormatCandidateValueFromRaw(u64 raw, searchType_t type) {
  return ValueToDisplay(SearchValueFromRaw(raw, type), type);
}

bool ReadCandidateEntriesPage(const std::string &path, size_t entryOffset,
                              size_t maxEntries,
                              std::vector<CandidateRecordView> &outEntries,
                              std::string *errorOut = nullptr) {
  outEntries.clear();
  if (maxEntries == 0) {
    return true;
  }

  FILE *fp = std::fopen(path.c_str(), "rb");
  if (!fp) {
    if (errorOut) {
      *errorOut = "failed to open candidate file";
    }
    return false;
  }

  const long dataOffset =
      static_cast<long>(sizeof(breeze::BreezeFileHeader_t) +
                        entryOffset * sizeof(CandidateRecordView));
  if (std::fseek(fp, dataOffset, SEEK_SET) != 0) {
    std::fclose(fp);
    if (errorOut) {
      *errorOut = "failed to seek candidate data";
    }
    return false;
  }

  outEntries.resize(maxEntries);
  const size_t readCount =
      std::fread(outEntries.data(), sizeof(CandidateRecordView), maxEntries, fp);
  std::fclose(fp);
  outEntries.resize(readCount);
  return true;
}

bool ReadCandidateEntryAt(const std::string &path, size_t entryIndex,
                          CandidateRecordView &outEntry,
                          std::string *errorOut = nullptr) {
  std::vector<CandidateRecordView> page;
  if (!ReadCandidateEntriesPage(path, entryIndex, 1, page, errorOut)) {
    return false;
  }
  if (page.empty()) {
    if (errorOut) {
      *errorOut = "candidate entry index out of range";
    }
    return false;
  }
  outEntry = page[0];
  return true;
}

bool WriteCandidateValueToMemory(u64 address, u64 rawValue, searchType_t type) {
  char bytes[8] = {};
  const size_t width = std::min<size_t>(8, CandidateValueWidth(type));
  std::memcpy(bytes, &rawValue, width);
  return R_SUCCEEDED(dmntchtWriteCheatProcessMemory(address, bytes, width));
}

searchType_t KeyboardTypeForCandidateValue(searchType_t type) {
  switch (type) {
  case SEARCH_TYPE_FLOAT_32BIT:
  case SEARCH_TYPE_FLOAT_64BIT:
    return SEARCH_TYPE_DOUBLE;
  case SEARCH_TYPE_SIGNED_8BIT:
  case SEARCH_TYPE_SIGNED_16BIT:
  case SEARCH_TYPE_SIGNED_32BIT:
  case SEARCH_TYPE_SIGNED_64BIT:
    return SEARCH_TYPE_SIGNED_64BIT;
  case SEARCH_TYPE_UNSIGNED_8BIT:
  case SEARCH_TYPE_UNSIGNED_16BIT:
  case SEARCH_TYPE_UNSIGNED_32BIT:
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_POINTER:
  case SEARCH_TYPE_UNSIGNED_40BIT:
  default:
    return SEARCH_TYPE_UNSIGNED_64BIT;
  }
}

u64 AddDeltaToStoredRaw(u64 storedRaw, searchValue_t delta, searchType_t type,
                        u64 multiplier) {
  const auto clampUnsigned = [](long double value, long double maxValue) -> u64 {
    if (value < 0.0L) {
      value = 0.0L;
    }
    if (value > maxValue) {
      value = maxValue;
    }
    return static_cast<u64>(value);
  };
  const auto clampSigned = [](long double value, long double minValue,
                              long double maxValue) -> s64 {
    if (value < minValue) {
      value = minValue;
    }
    if (value > maxValue) {
      value = maxValue;
    }
    return static_cast<s64>(value);
  };

  const searchValue_t base = SearchValueFromRaw(storedRaw, type);
  switch (type) {
  case SEARCH_TYPE_UNSIGNED_8BIT: {
    const long double next = static_cast<long double>(base._u8) +
                             static_cast<long double>(delta._u8) * multiplier;
    return clampUnsigned(next, 255.0L) & 0xFFULL;
  }
  case SEARCH_TYPE_SIGNED_8BIT: {
    const long double next = static_cast<long double>(base._s8) +
                             static_cast<long double>(delta._s8) * multiplier;
    const s64 v = clampSigned(next, -128.0L, 127.0L);
    return static_cast<u64>(static_cast<u8>(static_cast<s8>(v)));
  }
  case SEARCH_TYPE_UNSIGNED_16BIT: {
    const long double next = static_cast<long double>(base._u16) +
                             static_cast<long double>(delta._u16) * multiplier;
    return clampUnsigned(next, 65535.0L) & 0xFFFFULL;
  }
  case SEARCH_TYPE_SIGNED_16BIT: {
    const long double next = static_cast<long double>(base._s16) +
                             static_cast<long double>(delta._s16) * multiplier;
    const s64 v = clampSigned(next, -32768.0L, 32767.0L);
    return static_cast<u64>(static_cast<u16>(static_cast<s16>(v)));
  }
  case SEARCH_TYPE_UNSIGNED_32BIT: {
    const long double next = static_cast<long double>(base._u32) +
                             static_cast<long double>(delta._u32) * multiplier;
    return clampUnsigned(next, 4294967295.0L) & 0xFFFFFFFFULL;
  }
  case SEARCH_TYPE_SIGNED_32BIT: {
    const long double next = static_cast<long double>(base._s32) +
                             static_cast<long double>(delta._s32) * multiplier;
    const s64 v = clampSigned(next, -2147483648.0L, 2147483647.0L);
    return static_cast<u64>(static_cast<u32>(static_cast<s32>(v)));
  }
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_POINTER: {
    const long double maxU64 =
        static_cast<long double>(std::numeric_limits<u64>::max());
    const long double next = static_cast<long double>(base._u64) +
                             static_cast<long double>(delta._u64) * multiplier;
    return clampUnsigned(next, maxU64);
  }
  case SEARCH_TYPE_SIGNED_64BIT: {
    const long double minS64 =
        static_cast<long double>(std::numeric_limits<s64>::min());
    const long double maxS64 =
        static_cast<long double>(std::numeric_limits<s64>::max());
    const long double next = static_cast<long double>(base._s64) +
                             static_cast<long double>(delta._s64) * multiplier;
    const s64 v = clampSigned(next, minS64, maxS64);
    return static_cast<u64>(v);
  }
  case SEARCH_TYPE_FLOAT_32BIT: {
    const long double next = static_cast<long double>(base._f32) +
                             static_cast<long double>(delta._f32) * multiplier;
    const float v = static_cast<float>(next);
    u32 raw = 0;
    std::memcpy(&raw, &v, sizeof(v));
    return raw;
  }
  case SEARCH_TYPE_FLOAT_64BIT: {
    const long double next = static_cast<long double>(base._f64) +
                             static_cast<long double>(delta._f64) * multiplier;
    const double v = static_cast<double>(next);
    u64 raw = 0;
    std::memcpy(&raw, &v, sizeof(v));
    return raw;
  }
  case SEARCH_TYPE_UNSIGNED_40BIT: {
    const long double next =
        static_cast<long double>(base._u64 & 0xFFFFFFFFFFULL) +
        static_cast<long double>(delta._u64 & 0xFFFFFFFFFFULL) * multiplier;
    return clampUnsigned(next, static_cast<long double>(0xFFFFFFFFFFULL)) &
           0xFFFFFFFFFFULL;
  }
  default:
    return storedRaw;
  }
}

bool ApplyRevertAllCandidates(const std::string &path, searchType_t type, size_t startIndex,
                              size_t totalEntries) {
  if (startIndex >= totalEntries) {
    return true;
  }
  const size_t chunkSize = 128;
  for (size_t idx = startIndex; idx < totalEntries; idx += chunkSize) {
    const size_t count = std::min(chunkSize, totalEntries - idx);
    std::vector<CandidateRecordView> records;
    if (!ReadCandidateEntriesPage(path, idx, count, records, nullptr)) {
      return false;
    }
    for (const auto &rec : records) {
      if (!WriteCandidateValueToMemory(rec.address, rec.value, type)) {
        return false;
      }
    }
  }
  return true;
}

bool ApplyIncrementCandidates(const std::string &path, searchType_t type, searchValue_t delta,
                              size_t startIndex, size_t totalEntries) {
  if (startIndex >= totalEntries) {
    return true;
  }
  const size_t chunkSize = 128;
  u64 multiplier = 1;
  for (size_t idx = startIndex; idx < totalEntries; idx += chunkSize) {
    const size_t count = std::min(chunkSize, totalEntries - idx);
    std::vector<CandidateRecordView> records;
    if (!ReadCandidateEntriesPage(path, idx, count, records, nullptr)) {
      return false;
    }
    for (const auto &rec : records) {
      const u64 nextRaw = AddDeltaToStoredRaw(rec.value, delta, type, multiplier);
      if (!WriteCandidateValueToMemory(rec.address, nextRaw, type)) {
        return false;
      }
      ++multiplier;
    }
  }
  return true;
}

u64 RawFromSearchValue(searchValue_t value, searchType_t type) {
  switch (type) {
  case SEARCH_TYPE_UNSIGNED_8BIT:
    return static_cast<u64>(value._u8);
  case SEARCH_TYPE_SIGNED_8BIT:
    return static_cast<u64>(static_cast<u8>(value._s8));
  case SEARCH_TYPE_UNSIGNED_16BIT:
    return static_cast<u64>(value._u16);
  case SEARCH_TYPE_SIGNED_16BIT:
    return static_cast<u64>(static_cast<u16>(value._s16));
  case SEARCH_TYPE_UNSIGNED_32BIT:
    return static_cast<u64>(value._u32);
  case SEARCH_TYPE_SIGNED_32BIT:
    return static_cast<u64>(static_cast<u32>(value._s32));
  case SEARCH_TYPE_FLOAT_32BIT: {
    u32 raw = 0;
    std::memcpy(&raw, &value._f32, sizeof(raw));
    return raw;
  }
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_POINTER:
    return value._u64;
  case SEARCH_TYPE_SIGNED_64BIT:
    return static_cast<u64>(value._s64);
  case SEARCH_TYPE_FLOAT_64BIT: {
    u64 raw = 0;
    std::memcpy(&raw, &value._f64, sizeof(raw));
    return raw;
  }
  case SEARCH_TYPE_UNSIGNED_40BIT:
    return value._u64 & 0xFFFFFFFFFFULL;
  default:
    return value._u64;
  }
}

class CandidateEntriesMenu;

class CandidateEntryOptionsMenu : public tsl::Gui {
private:
  std::string m_path;
  size_t m_pageIndex = 0;
  size_t m_globalIndex = 0;
  size_t m_totalEntries = 0;
  searchType_t m_type = SEARCH_TYPE_UNSIGNED_32BIT;
  CandidateRecordView m_entry{};

  u64 readCurrentRawValue() const {
    char bytes[8] = {};
    const size_t width = std::min<size_t>(8, CandidateValueWidth(m_type));
    if (R_SUCCEEDED(
            dmntchtReadCheatProcessMemory(m_entry.address, bytes, width))) {
      u64 raw = 0;
      std::memcpy(&raw, bytes, width);
      return raw;
    }
    return m_entry.value;
  }

public:
  CandidateEntryOptionsMenu(const std::string &path, size_t pageIndex,
                            size_t globalIndex, size_t totalEntries,
                            searchType_t type, CandidateRecordView entry)
      : m_path(path), m_pageIndex(pageIndex), m_globalIndex(globalIndex),
        m_totalEntries(totalEntries), m_type(type), m_entry(entry) {}

  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {
    if (keysDown & KEY_B) {
      tsl::swapTo<CandidateEntriesMenu>(m_path, m_pageIndex);
      return true;
    }
    return false;
  }

  virtual tsl::elm::Element *createUI() override {
    auto *frame =
        new tsl::elm::OverlayFrame("Candidate Options",
                                   CandidateStemFromPath(m_path) + " #" +
                                       std::to_string(m_globalIndex + 1));
    auto *list = new tsl::elm::List();

    auto *editItem = new tsl::elm::ListItem("Edit value");
    editItem->setClickListener([this](u64 keys) {
      if (!(keys & KEY_A)) {
        return false;
      }
      const std::string initial = ValueToEditable(
          SearchValueFromRaw(readCurrentRawValue(), m_type), m_type);
      tsl::changeTo<tsl::KeyboardGui>(
          KeyboardTypeForCandidateValue(m_type), initial, "Edit value",
          [this](std::string result) {
            searchValue_t parsed{};
            if (!ParseValueFromText(TrimCopy(result), m_type, parsed)) {
              return;
            }
            const u64 raw = RawFromSearchValue(parsed, m_type);
            if (!WriteCandidateValueToMemory(m_entry.address, raw, m_type)) {
              return;
            }
            tsl::goBack();
            tsl::swapTo<CandidateEntriesMenu>(m_path, m_pageIndex);
          },
          nullptr, false);
      return true;
    });
    list->addItem(editItem);

    auto *incrementItem = new tsl::elm::ListItem("Increment");
    incrementItem->setClickListener([this](u64 keys) {
      if (!(keys & KEY_A)) {
        return false;
      }
      const std::string initial = "1";
      tsl::changeTo<tsl::KeyboardGui>(
          KeyboardTypeForCandidateValue(m_type), initial, "Delta",
          [this](std::string result) {
            searchValue_t delta{};
            if (!ParseValueFromText(TrimCopy(result), m_type, delta)) {
              return;
            }
            if (!ApplyIncrementCandidates(m_path, m_type, delta, m_globalIndex,
                                          m_totalEntries)) {
              return;
            }
            tsl::goBack();
            tsl::swapTo<CandidateEntriesMenu>(m_path, m_pageIndex);
          },
          nullptr, false);
      return true;
    });
    list->addItem(incrementItem);

    auto *revertItem = new tsl::elm::ListItem("Revert");
    revertItem->setClickListener([this](u64 keys) {
      if (!(keys & KEY_A)) {
        return false;
      }
      if (!WriteCandidateValueToMemory(m_entry.address, m_entry.value, m_type)) {
        return true;
      }
      tsl::swapTo<CandidateEntriesMenu>(m_path, m_pageIndex);
      return true;
    });
    list->addItem(revertItem);

    auto *revertAllItem = new tsl::elm::ListItem("Revert all");
    revertAllItem->setClickListener([this](u64 keys) {
      if (!(keys & KEY_A)) {
        return false;
      }
      if (!ApplyRevertAllCandidates(m_path, m_type, 0, m_totalEntries)) {
        return true;
      }
      tsl::swapTo<CandidateEntriesMenu>(m_path, m_pageIndex);
      return true;
    });
    list->addItem(revertAllItem);

    frame->setContent(list);
    return frame;
  }
};

class CandidateEntriesMenu : public tsl::Gui {
private:
  class CandidateToggleItem : public tsl::elm::ToggleListItem {
  public:
    explicit CandidateToggleItem(const std::string &text, bool initialState)
        : tsl::elm::ToggleListItem(text, initialState, "", "", false) {}

    void setXClickListener(std::function<bool(u64)> listener) {
      m_xClickListener = std::move(listener);
    }

    virtual bool onClick(u64 keys) override {
      if ((keys & KEY_X) && m_xClickListener) {
        return m_xClickListener(keys);
      }
      return tsl::elm::ToggleListItem::onClick(keys);
    }

  private:
    std::function<bool(u64)> m_xClickListener;
  };

  struct CandidateLiveRow {
    CandidateToggleItem *item = nullptr;
    CandidateRecordView rec{};
    std::string prefix;
    std::string lastLiveText;
    bool lastFrozen = false;
    size_t globalIndex = 0;
  };

  std::string m_path;
  size_t m_pageIndex = 0;
  size_t m_totalEntries = 0;
  size_t m_pageCount = 1;
  static constexpr size_t kPageSize = 10;
  std::vector<CandidateLiveRow> m_liveRows;
  searchType_t m_type = SEARCH_TYPE_UNSIGNED_32BIT;
  size_t m_valueWidth = 4;
  u32 m_liveRefreshTick = 0;
  static constexpr u32 kLiveRefreshIntervalTicks = 18;
  int m_candidateFontSize = 20;
  std::string m_notesPath;
  bool m_fontLoaded = false;
  tsl::elm::OverlayFrame *m_frame = nullptr;
  tsl::elm::List *m_list = nullptr;

  bool IsAddressFrozen(u64 address) const {
    DmntFrozenAddressEntry frozen{};
    return R_SUCCEEDED(dmntchtGetFrozenAddress(&frozen, address));
  }

  std::string BuildRowText(const CandidateLiveRow &row) const {
    return row.prefix + row.lastLiveText;
  }

  void UpdateSubtitleFromFocus() {
    if (!m_frame) {
      return;
    }
    size_t currentIndex = (m_pageIndex * kPageSize) + 1;
    for (const auto &row : m_liveRows) {
      if (row.item && row.item->hasFocus()) {
        currentIndex = row.globalIndex + 1;
        break;
      }
    }
    m_frame->setSubtitle("index = " + std::to_string(currentIndex) + " page " +
                         std::to_string(m_pageIndex + 1) + " / " +
                         std::to_string(m_pageCount));
  }

  std::string ReadLiveValueText(u64 address) const {
    u64 liveRaw = 0;
    char rawBuffer[8] = {};
    if (R_SUCCEEDED(dmntchtReadCheatProcessMemory(
            address, rawBuffer, std::min<size_t>(m_valueWidth, sizeof(rawBuffer))))) {
      std::memcpy(&liveRaw, rawBuffer, std::min<size_t>(m_valueWidth, sizeof(rawBuffer)));
      return FormatCandidateValueFromRaw(liveRaw, m_type);
    }
    return "N/A";
  }

public:
  CandidateEntriesMenu(const std::string &path, size_t pageIndex = 0)
      : m_path(path), m_pageIndex(pageIndex) {}

  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {
    if (keysHeld & KEY_ZL) {
      if (keysDown & KEY_R) {
        m_candidateFontSize = std::min(m_candidateFontSize + 1, 30);
        for (auto &row : m_liveRows) {
          if (row.item) {
            row.item->setFontSize(m_candidateFontSize);
          }
        }
        if (m_list) {
          m_list->layout(m_list->getX(), m_list->getY(), m_list->getWidth(),
                         m_list->getHeight());
        }
        if (!m_notesPath.empty()) {
          ult::setIniFileValue(m_notesPath, "Breeze", "candidate_font_size",
                               std::to_string(m_candidateFontSize));
        }
        return true;
      }
      if (keysDown & KEY_L) {
        m_candidateFontSize = std::max(m_candidateFontSize - 1, 10);
        for (auto &row : m_liveRows) {
          if (row.item) {
            row.item->setFontSize(m_candidateFontSize);
          }
        }
        if (m_list) {
          m_list->layout(m_list->getX(), m_list->getY(), m_list->getWidth(),
                         m_list->getHeight());
        }
        if (!m_notesPath.empty()) {
          ult::setIniFileValue(m_notesPath, "Breeze", "candidate_font_size",
                               std::to_string(m_candidateFontSize));
        }
        return true;
      }
    }
    if (keysDown & KEY_B) {
      tsl::goBack();
      return true;
    }
    if ((keysDown & KEY_L) && m_pageIndex > 0) {
      tsl::swapTo<CandidateEntriesMenu>(m_path, m_pageIndex - 1);
      return true;
    }
    if ((keysDown & KEY_R) && (m_pageIndex + 1) < m_pageCount) {
      tsl::swapTo<CandidateEntriesMenu>(m_path, m_pageIndex + 1);
      return true;
    }
    return false;
  }

  virtual tsl::elm::Element *createUI() override {
    breeze::BreezeFileHeader_t header{};
    std::string err;
    if (!breeze::ReadCandidateHeader(m_path, header, &err)) {
      auto *frame = new tsl::elm::OverlayFrame("Candidates", "Invalid file");
      auto *list = new tsl::elm::List();
      list->addItem(new tsl::elm::ListItem("Failed to load candidate file"));
      frame->setContent(list);
      return frame;
    }

    m_totalEntries =
        static_cast<size_t>(header.dataSize / sizeof(CandidateRecordView));
    m_pageCount = std::max<size_t>(1, (m_totalEntries + kPageSize - 1) / kPageSize);
    if (m_pageIndex >= m_pageCount) {
      m_pageIndex = m_pageCount - 1;
    }

    std::string title = "Candidates " + std::to_string(m_pageIndex + 1) + "/" +
                        std::to_string(m_pageCount);

    auto *frame = new tsl::elm::OverlayFrame(title, "");
    m_frame = frame;
    auto *list = new tsl::elm::List();
    m_list = list;
    addHeader(list, CandidateStemFromPath(m_path),
              std::string("\uE0E2 ") + "Options  \uE0E4/\uE0E5 Page");

    if (m_totalEntries == 0) {
      list->addItem(new tsl::elm::ListItem("No entries"));
      frame->setContent(list);
      return frame;
    }

    DmntCheatProcessMetadata metadata = header.Metadata;
    DmntCheatProcessMetadata liveMetadata{};
    if (R_SUCCEEDED(dmntchtGetCheatProcessMetadata(&liveMetadata))) {
      metadata = liveMetadata;
    }
    if (m_notesPath.empty()) {
      std::stringstream ss;
      ss << "sdmc:/switch/breeze/cheats/" << std::setfill('0') << std::setw(16)
         << std::uppercase << std::hex << metadata.title_id << "/notes.txt";
      m_notesPath = ss.str();
    }
    if (!m_fontLoaded && !m_notesPath.empty()) {
      const std::string fontSizeStr = ult::parseValueFromIniSection(
          m_notesPath, "Breeze", "candidate_font_size");
      if (!fontSizeStr.empty()) {
        m_candidateFontSize =
            std::clamp(static_cast<int>(ult::stoi(fontSizeStr)), 10, 30);
      }
      m_fontLoaded = true;
    }

    const size_t offset = m_pageIndex * kPageSize;
    const size_t count = std::min(kPageSize, m_totalEntries - offset);
    std::vector<CandidateRecordView> records;
    if (!ReadCandidateEntriesPage(m_path, offset, count, records, &err)) {
      list->addItem(new tsl::elm::ListItem("Failed to read entries"));
      frame->setContent(list);
      return frame;
    }

    m_type = header.search_condition.searchType;
    m_valueWidth = CandidateValueWidth(m_type);
    m_liveRows.clear();
    m_liveRefreshTick = 0;

    for (size_t i = 0; i < records.size(); ++i) {
      const auto &rec = records[i];
      const std::string addrText = FormatCandidateRegionAddress(rec.address, metadata);
      const std::string fileValueText = FormatCandidateValueFromRaw(rec.value, m_type);
      const std::string liveValueText = ReadLiveValueText(rec.address);
      const bool frozen = IsAddressFrozen(rec.address);

      const std::string prefix = addrText + SearchTypeLabel(m_type) + " ";
      CandidateLiveRow row{};
      row.rec = rec;
      row.prefix = prefix;
      row.lastLiveText = liveValueText;
      row.lastFrozen = frozen;
      const std::string line = BuildRowText(row);
      auto *item = new CandidateToggleItem(line, frozen);
      item->setUseLeftBox(true);
      item->setFontSize(m_candidateFontSize);
      item->setNote("previous = " + fileValueText);
      const size_t globalIndex = offset + i;
      item->setStateChangedListener([item, rec, this](bool state) {
        bool ok = false;
        if (state) {
          u64 outValue = 0;
          const u64 width =
              static_cast<u64>(std::min<size_t>(8, CandidateValueWidth(m_type)));
          ok = R_SUCCEEDED(dmntchtEnableFrozenAddress(rec.address, width, &outValue));
        } else {
          ok = R_SUCCEEDED(dmntchtDisableFrozenAddress(rec.address));
        }
        if (!ok) {
          item->setState(!state);
        }
      });
      item->setXClickListener([this, globalIndex, rec](u64 keys) {
        if (!(keys & KEY_X)) {
          return false;
        }
        tsl::swapTo<CandidateEntryOptionsMenu>(m_path, m_pageIndex, globalIndex,
                                               m_totalEntries, m_type, rec);
        return true;
      });
      list->addItem(item);
      row.item = item;
      row.globalIndex = globalIndex;
      m_liveRows.push_back(row);
    }

    UpdateSubtitleFromFocus();
    frame->setContent(list);
    return frame;
  }

  virtual void update() override {
    if (m_liveRows.empty()) {
      return;
    }
    if (++m_liveRefreshTick < kLiveRefreshIntervalTicks) {
      return;
    }
    m_liveRefreshTick = 0;

    for (auto &row : m_liveRows) {
      if (!row.item) {
        continue;
      }
      const std::string liveValueText = ReadLiveValueText(row.rec.address);
      const bool frozen = IsAddressFrozen(row.rec.address);
      if (liveValueText == row.lastLiveText && frozen == row.lastFrozen) {
        continue;
      }
      row.lastLiveText = liveValueText;
      row.lastFrozen = frozen;
      row.item->setText(BuildRowText(row));
      row.item->setState(frozen);
    }
    UpdateSubtitleFromFocus();
  }
};

void SwapToContinueSearchFileMenu();

class ContinueSourceOptionsMenu : public tsl::Gui {
private:
  std::string m_path;
  tsl::elm::ListItem *m_deleteFileItem = nullptr;
  tsl::elm::ListItem *m_deleteSeriesItem = nullptr;
  bool IsDeleteFilePending() const { return g_pendingDeleteFilePath == m_path; }
  bool IsDeleteSeriesPending() const { return g_pendingDeleteSeriesPath == m_path; }
  bool HasPendingDelete() const { return IsDeleteFilePending() || IsDeleteSeriesPending(); }
  void RefreshDeleteFileUi() {
    if (!m_deleteFileItem) {
      return;
    }
    m_deleteFileItem->setNote(IsDeleteFilePending() ? "A=Confirm B=Abort"
                                                    : "Deletes selected file");
  }
  void RefreshDeleteSeriesUi() {
    if (!m_deleteSeriesItem) {
      return;
    }
    if (IsDeleteSeriesPending()) {
      m_deleteSeriesItem->setNote("A=Confirm B=Abort");
      return;
    }
    m_deleteSeriesItem->setNote(
        (g_continueSourceFilterMode == 2) ? "Deletes selected and newer in series"
                                          : "Deletes all in series");
  }
  void RefreshDeleteUi() {
    RefreshDeleteFileUi();
    RefreshDeleteSeriesUi();
  }

  bool ExecuteDeleteFile() {
    g_pendingDeleteFilePath.clear();
    g_pendingDeleteFileWaitRelease = false;

    if (m_path.empty()) {
      if (tsl::notification) {
        tsl::notification->show("No source candidate selected");
      }
      RefreshDeleteUi();
      return true;
    }

    const std::string selectedBase = SeriesBaseFromStem(CandidateStemFromPath(m_path));
    if (std::remove(m_path.c_str()) != 0) {
      if (tsl::notification) {
        tsl::notification->show("Failed to delete file");
      }
      RefreshDeleteUi();
      return true;
    }

    if (g_searchConditionSourcePath == m_path) {
      g_searchConditionSourcePath.clear();
    }

    SelectContinueSourceAfterDeletion(selectedBase);
    if (!g_searchContinueSourcePath.empty()) {
      g_continueSearchOutputName =
          AutoGenerateContinueOutputName(g_searchContinueSourcePath);
    } else {
      g_continueSearchOutputName = AutoGenerateStartOutputName();
    }

    if (!g_continueSourceFilterAnchorStem.empty() &&
        SeriesBaseFromStem(g_continueSourceFilterAnchorStem) == selectedBase) {
      g_continueSourceFilterAnchorStem =
          CandidateStemFromPath(g_searchContinueSourcePath);
    }

    if (tsl::notification) {
      tsl::notification->show("Deleted file");
    }

    tsl::goBack();
    SwapToContinueSearchFileMenu();
    return true;
  }

  bool ExecuteDeleteSeries() {
    g_pendingDeleteSeriesPath.clear();
    g_pendingDeleteSeriesWaitRelease = false;
    const std::string selectedBase = SeriesBaseFromStem(CandidateStemFromPath(m_path));
    std::vector<std::string> deletedPaths;
    size_t failedCount = 0;
    std::string err;
    const size_t removedCount =
        DeleteCandidateSeries(m_path, g_continueSourceFilterMode == 2,
                              deletedPaths, failedCount, &err);
    if (removedCount == 0) {
      if (tsl::notification) {
        tsl::notification->show(err.empty() ? "Failed to delete series" : err);
      }
      RefreshDeleteSeriesUi();
      return true;
    }

    auto wasDeleted = [&deletedPaths](const std::string &path) -> bool {
      return std::find(deletedPaths.begin(), deletedPaths.end(), path) !=
             deletedPaths.end();
    };

    if (wasDeleted(g_searchContinueSourcePath)) {
      g_searchContinueSourcePath.clear();
    }
    if (wasDeleted(g_searchConditionSourcePath)) {
      g_searchConditionSourcePath.clear();
    }

    if (g_searchContinueSourcePath.empty()) {
      SelectContinueSourceAfterDeletion(selectedBase);
    }
    if (!g_searchContinueSourcePath.empty()) {
      g_continueSearchOutputName =
          AutoGenerateContinueOutputName(g_searchContinueSourcePath);
    } else {
      g_continueSearchOutputName = AutoGenerateStartOutputName();
    }

    if (!g_continueSourceFilterAnchorStem.empty() &&
        SeriesBaseFromStem(g_continueSourceFilterAnchorStem) == selectedBase) {
      g_continueSourceFilterAnchorStem =
          CandidateStemFromPath(g_searchContinueSourcePath);
    }

    if (tsl::notification) {
      std::string msg = "Deleted " + std::to_string(removedCount) + " file(s)";
      if (failedCount > 0) {
        msg += ", failed " + std::to_string(failedCount);
      }
      tsl::notification->show(msg);
    }

    tsl::goBack();
    SwapToContinueSearchFileMenu();
    return true;
  }

public:
  explicit ContinueSourceOptionsMenu(const std::string &path) : m_path(path) {}

  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {
    if (IsDeleteFilePending()) {
      if (g_pendingDeleteFileWaitRelease) {
        if (!(keysHeld & KEY_A)) {
          g_pendingDeleteFileWaitRelease = false;
        }
      }
      if (!g_pendingDeleteFileWaitRelease && (keysDown & KEY_A)) {
        return ExecuteDeleteFile();
      }
      if (keysDown & KEY_A) {
        return true;
      }
      if (keysDown & KEY_B) {
        g_pendingDeleteFilePath.clear();
        g_pendingDeleteFileWaitRelease = false;
        RefreshDeleteUi();
        return true;
      }
    }
    if (IsDeleteSeriesPending()) {
      if (g_pendingDeleteSeriesWaitRelease) {
        if (!(keysHeld & KEY_A)) {
          g_pendingDeleteSeriesWaitRelease = false;
        }
      }
      if (!g_pendingDeleteSeriesWaitRelease && (keysDown & KEY_A)) {
        return ExecuteDeleteSeries();
      }
      if (keysDown & KEY_A) {
        return true;
      }
      if (keysDown & KEY_B) {
        g_pendingDeleteSeriesPath.clear();
        g_pendingDeleteSeriesWaitRelease = false;
        RefreshDeleteUi();
        return true;
      }
    }
    if (keysDown & KEY_B) {
      tsl::goBack();
      return true;
    }
    return false;
  }

  virtual tsl::elm::Element *createUI() override {
    auto *frame =
        new tsl::elm::OverlayFrame("Continue Source Options", CandidateStemFromPath(m_path));
    auto *list = new tsl::elm::List();

    auto *copyConditionItem = new tsl::elm::ListItem("Copy condition");
    copyConditionItem->setClickListener([this](u64 keys) {
      if (!(keys & KEY_A)) {
        return false;
      }
      if (HasPendingDelete()) {
        return true;
      }
      breeze::BreezeFileHeader_t header{};
      std::string err;
      if (!breeze::ReadCandidateHeader(m_path, header, &err)) {
        return true;
      }
      g_searchCondition = header.search_condition;
      g_searchConditionReady = true;
      g_searchConditionSourcePath = m_path;
      tsl::goBack();
      return true;
    });
    list->addItem(copyConditionItem);

    auto *viewCandidatesItem = new tsl::elm::ListItem("View candidates");
    viewCandidatesItem->setClickListener([this](u64 keys) {
      if (!(keys & KEY_A)) {
        return false;
      }
      if (HasPendingDelete()) {
        return true;
      }
      tsl::changeTo<CandidateEntriesMenu>(m_path, 0);
      return true;
    });
    list->addItem(viewCandidatesItem);

    auto *renameSeriesItem = new tsl::elm::ListItem("Rename series");
    renameSeriesItem->setClickListener([this](u64 keys) {
      if (!(keys & KEY_A)) {
        return false;
      }
      if (HasPendingDelete()) {
        return true;
      }
      const std::string currentStem = CandidateStemFromPath(m_path);
      const std::string currentBase = SeriesBaseFromStem(currentStem);
      tsl::changeTo<tsl::KeyboardGui>(
          SEARCH_TYPE_TEXT, currentBase, "Rename series",
          [this, currentBase](std::string result) {
            std::vector<CandidateSeriesRenamePlanEntry> renames;
            std::string renamedSelected = m_path;
            std::string err;
            if (!RenameCandidateSeries(m_path, result, renames, renamedSelected, &err)) {
              if (tsl::notification) {
                tsl::notification->show(err.empty() ? "Failed to rename series" : err);
              }
              return;
            }

            ApplyCandidateRenameToPath(g_searchContinueSourcePath, renames);
            ApplyCandidateRenameToPath(g_searchConditionSourcePath, renames);
            m_path = renamedSelected;

            const std::string newBase = SeriesBaseFromStem(CandidateStemFromPath(m_path));
            if (!g_searchContinueSourcePath.empty()) {
              g_continueSearchOutputName =
                  AutoGenerateContinueOutputName(g_searchContinueSourcePath);
            }

            if (!g_continueSourceFilterAnchorStem.empty() &&
                SeriesBaseFromStem(g_continueSourceFilterAnchorStem) == currentBase) {
              g_continueSourceFilterAnchorStem = FormatSeriesStem(newBase, 0);
            }

            if (tsl::notification) {
              tsl::notification->show("Renamed " + std::to_string(renames.size()) +
                                      " file(s)");
            }

            // Rename is triggered from KeyboardGui -> Options -> File list.
            // Unwind back to the old file list first, then replace it.
            tsl::goBack();
            tsl::goBack();
            SwapToContinueSearchFileMenu();
          },
          nullptr, false);
      return true;
    });
    list->addItem(renameSeriesItem);

    auto *deleteFileItem = new tsl::elm::ListItem("Delete file");
    m_deleteFileItem = deleteFileItem;
    deleteFileItem->setAlwaysShowNote(true);
    RefreshDeleteFileUi();
    deleteFileItem->setClickListener([this](u64 keys) {
      if (!(keys & KEY_A)) {
        return false;
      }
      if (HasPendingDelete()) {
        return true;
      }
      g_pendingDeleteSeriesPath.clear();
      g_pendingDeleteSeriesWaitRelease = false;
      g_pendingDeleteFilePath = m_path;
      g_pendingDeleteFileWaitRelease = true;
      RefreshDeleteUi();
      return true;
    });
    list->addItem(deleteFileItem);

    auto *deleteSeriesItem = new tsl::elm::ListItem("Delete series");
    m_deleteSeriesItem = deleteSeriesItem;
    deleteSeriesItem->setAlwaysShowNote(true);
    RefreshDeleteSeriesUi();
    deleteSeriesItem->setClickListener([this](u64 keys) {
      if (!(keys & KEY_A)) {
        return false;
      }
      if (HasPendingDelete()) {
        return true;
      }
      g_pendingDeleteFilePath.clear();
      g_pendingDeleteFileWaitRelease = false;
      g_pendingDeleteSeriesPath = m_path;
      g_pendingDeleteSeriesWaitRelease = true;
      RefreshDeleteUi();
      return true;
    });
    list->addItem(deleteSeriesItem);

    frame->setContent(list);
    return frame;
  }
};

class ContinueSearchFileMenu : public tsl::Gui {
private:
  tsl::elm::List *m_list = nullptr;

public:
  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {
    if (keysDown & KEY_Y) {
      std::string focusedStem = CandidateStemFromPath(g_searchContinueSourcePath);
      if (m_list) {
        struct ListProxy : public tsl::elm::List {
          using tsl::elm::List::m_items;
        };
        for (auto *item : static_cast<ListProxy *>(m_list)->m_items) {
          if (item && item->m_isItem && item->hasFocus()) {
            auto *listItem = static_cast<tsl::elm::ListItem *>(item);
            focusedStem = listItem->getText();
            break;
          }
        }
      }

      const int nextMode = (g_continueSourceFilterMode == 1) ? 2 : 1;
      if (nextMode == 2) {
        g_continueSourceFilterAnchorStem = focusedStem;
      } else {
        const std::string base = SeriesBaseFromStem(focusedStem);
        if (!base.empty()) {
          const std::string endStem = SeriesEndStemForBase(base);
          g_continueSourceJumpStem = endStem.empty() ? focusedStem : endStem;
        } else {
          g_continueSourceJumpStem = focusedStem;
        }
      }
      g_continueSourceFilterMode = nextMode;
      tsl::swapTo<ContinueSearchFileMenu>();
      return true;
    }
    if (keysDown & KEY_B) {
      tsl::goBack();
      return true;
    }
    return false;
  }

  virtual tsl::elm::Element *createUI() override {
    std::string subtitle =
        (g_continueSourceFilterMode == 2) ? "Filtered (focused series)"
                                          : "Series ends only";
    auto *frame = new tsl::elm::OverlayFrame("Continue Source", subtitle);
    auto *list = new tsl::elm::List();
    m_list = list;
    addHeader(list, "Candidate Files",
              std::string("\uE0E2 ") + "Options  \uE0E3 Filter");

    const auto files = breeze::ListCandidateFiles();
    if (files.empty()) {
      list->addItem(new tsl::elm::ListItem("No candidate files found"));
      frame->setContent(list);
      return frame;
    }

    const std::string focusedStem =
        g_continueSourceFilterMode == 2 && !g_continueSourceFilterAnchorStem.empty()
            ? g_continueSourceFilterAnchorStem
            : CandidateStemFromPath(g_searchContinueSourcePath);
    const std::string focusedSeriesBase = SeriesBaseFromStem(focusedStem);
    std::set<std::string> seriesEndStems;
    if (g_continueSourceFilterMode == 1) {
      struct SeriesEndEntry {
        std::string base;
        std::string stem;
        int index = 0;
      };
      std::vector<SeriesEndEntry> ends;
      for (const auto &path : files) {
        const std::string stem = CandidateStemFromPath(path);
        const std::string base = SeriesBaseFromStem(stem);
        const int index = SeriesIndexFromStem(stem);

        auto it = std::find_if(ends.begin(), ends.end(),
                               [&base](const SeriesEndEntry &entry) {
                                 return entry.base == base;
                               });
        if (it == ends.end()) {
          ends.push_back(SeriesEndEntry{base, stem, index});
          continue;
        }
        if (index > it->index || (index == it->index && stem > it->stem)) {
          it->stem = stem;
          it->index = index;
        }
      }
      for (const auto &entry : ends) {
        seriesEndStems.insert(entry.stem);
      }
    }

    bool anyShown = false;
    for (const auto &path : files) {
      const std::string stem = CandidateStemFromPath(path);
      const std::string stemSeriesBase = SeriesBaseFromStem(stem);
      if (g_continueSourceFilterMode == 1 &&
          !seriesEndStems.count(stem)) {
        continue;
      }
      if (g_continueSourceFilterMode == 2 && !focusedSeriesBase.empty()) {
        if (stemSeriesBase != focusedSeriesBase) {
          continue;
        }
      }
      anyShown = true;
      auto *item = new tsl::elm::ListItem(stem);
      const std::string status = CandidateStatusFromPath(path);
      item->setNote(status);
      item->setAlwaysShowNote(true);
      item->setClickListener([path](u64 keys) {
        if (keys & KEY_A) {
          g_searchContinueSourcePath = path;
          if (g_continueSearchOutputName.empty()) {
            g_continueSearchOutputName =
                AutoGenerateContinueOutputName(g_searchContinueSourcePath);
          }
          tsl::goBack();
          return true;
        }
        if (keys & KEY_X) {
          tsl::changeTo<ContinueSourceOptionsMenu>(path);
          return true;
        }
        return false;
      });
      list->addItem(item);
    }

    if (!anyShown) {
      const char *emptyText = "No candidate files found";
      if (g_continueSourceFilterMode == 1) {
        emptyText = "No series ends found";
      } else if (g_continueSourceFilterMode == 2) {
        emptyText = "No candidates in focused series";
      }
      list->addItem(new tsl::elm::ListItem(emptyText));
    }

    std::string jumpStem = g_continueSourceJumpStem.empty()
                               ? CandidateStemFromPath(g_searchContinueSourcePath)
                               : g_continueSourceJumpStem;
    g_continueSourceJumpStem.clear();
    if (!jumpStem.empty()) {
      list->jumpToItem(jumpStem, "", true);
    }

    frame->setContent(list);
    return frame;
  }
};

void SwapToContinueSearchFileMenu() { tsl::swapTo<ContinueSearchFileMenu>(); }

std::string SearchDataNote(const Search_condition &condition, int slot,
                           bool hexMode) {
  auto eqPlusDerivedNote = [&](bool exclusiveRange) -> std::string {
    const searchValue_t asU32 =
        ConvertValueType(condition.searchValue_1, condition.searchType,
                         SEARCH_TYPE_UNSIGNED_32BIT);
    const searchValue_t asF32 =
        ConvertValueType(condition.searchValue_1, condition.searchType,
                         SEARCH_TYPE_FLOAT_32BIT);
    const searchValue_t asF64 =
        ConvertValueType(condition.searchValue_1, condition.searchType,
                         SEARCH_TYPE_FLOAT_64BIT);

    if (!exclusiveRange) {
      return "u32=" + ValueToDisplay(asU32, SEARCH_TYPE_UNSIGNED_32BIT) +
             " flt=" + ValueToDisplay(asF32, SEARCH_TYPE_FLOAT_32BIT) +
             " dbl=" + ValueToDisplay(asF64, SEARCH_TYPE_FLOAT_64BIT);
    }

    const double f32Center = static_cast<double>(asF32._f32);
    const double f32Low = f32Center - 1.0;
    const double f32High = f32Center + 1.0;
    const double f64Center = asF64._f64;
    const double f64Low = f64Center - 1.0;
    const double f64High = f64Center + 1.0;

    char f32buf[96] = {};
    char f64buf[128] = {};
    std::snprintf(f32buf, sizeof(f32buf), "flt<%.8g..%.8g>", f32Low, f32High);
    std::snprintf(f64buf, sizeof(f64buf), "dbl<%.16g..%.16g>", f64Low, f64High);

    return "u32=" + ValueToDisplay(asU32, SEARCH_TYPE_UNSIGNED_32BIT) + " " +
           std::string(f32buf) + " " + std::string(f64buf);
  };

  const bool stringMode = condition.searchMode == SM_STRING;
  if (stringMode && slot == 0) {
    return std::string("text=\"") + condition.searchString + "\"";
  }
  if (slot == 0) {
    if (condition.searchMode == SM_EQ_plus) {
      return eqPlusDerivedNote(false);
    }
    if (condition.searchMode == SM_EQ_plus_plus) {
      return eqPlusDerivedNote(true);
    }
    return hexMode ? ValueToHexDisplay(condition.searchValue_1, condition.searchType)
                   : ValueToDisplay(condition.searchValue_1, condition.searchType);
  }
  if (slot == 1) {
    return hexMode ? ValueToHexDisplay(condition.searchValue_2, condition.searchType)
                   : ValueToDisplay(condition.searchValue_2, condition.searchType);
  }
  return hexMode ? ValueToHexDisplay(condition.searchValue_3, condition.searchType)
                 : ValueToDisplay(condition.searchValue_3, condition.searchType);
}

bool TryLoadConditionFromLatestCandidate() {
  Search_condition loaded{};
  std::string filePath;
  std::string error;
  if (!breeze::LoadLatestCandidateCondition(loaded, filePath, &error)) {
    return false;
  }
  g_searchCondition = loaded;
  g_searchConditionSourcePath = filePath;
  g_searchConditionReady = true;
  return true;
}

searchType_t KeyboardTypeForDataSlot(const Search_condition &condition,
                                     int slot, bool hexMode = false) {
  if (condition.searchMode == SM_STRING && slot == 0) {
    return SEARCH_TYPE_TEXT;
  }
  if (hexMode) {
    return SEARCH_TYPE_HEX;
  }
  switch (condition.searchType) {
  case SEARCH_TYPE_FLOAT_32BIT:
  case SEARCH_TYPE_FLOAT_64BIT:
    return SEARCH_TYPE_DOUBLE;
  case SEARCH_TYPE_SIGNED_8BIT:
  case SEARCH_TYPE_SIGNED_16BIT:
  case SEARCH_TYPE_SIGNED_32BIT:
  case SEARCH_TYPE_SIGNED_64BIT:
    return SEARCH_TYPE_SIGNED_64BIT;
  default:
    return SEARCH_TYPE_UNSIGNED_64BIT;
  }
}

std::string EditableDataText(const Search_condition &condition, int slot,
                             bool hexMode = false) {
  if (condition.searchMode == SM_STRING && slot == 0) {
    return condition.searchString;
  }
  if (hexMode) {
    if (slot == 0) {
      return ValueToHexDisplay(condition.searchValue_1, condition.searchType);
    }
    if (slot == 1) {
      return ValueToHexDisplay(condition.searchValue_2, condition.searchType);
    }
    return ValueToHexDisplay(condition.searchValue_3, condition.searchType);
  }
  if (slot == 0) {
    return ValueToEditable(condition.searchValue_1, condition.searchType);
  }
  if (slot == 1) {
    return ValueToEditable(condition.searchValue_2, condition.searchType);
  }
  return ValueToEditable(condition.searchValue_3, condition.searchType);
}

bool ApplyDataText(Search_condition &condition, int slot, const std::string &text,
                   bool hexMode = false) {
  if (condition.searchMode == SM_STRING && slot == 0) {
    const std::string trimmed = text.substr(0, sizeof(condition.searchString) - 1);
    std::memset(condition.searchString, 0, sizeof(condition.searchString));
    std::memcpy(condition.searchString, trimmed.data(), trimmed.size());
    condition.searchStringLen = static_cast<u8>(trimmed.size());
    return true;
  }

  searchValue_t parsed{};
  if (hexMode) {
    if (!ParseHexValueForType(text, condition.searchType, parsed)) {
      return false;
    }
  } else if (!ParseValueFromText(text, condition.searchType, parsed)) {
    return false;
  }
  if (slot == 0) {
    condition.searchValue_1 = parsed;
  } else if (slot == 1) {
    condition.searchValue_2 = parsed;
  } else {
    condition.searchValue_3 = parsed;
  }
  return true;
}

class SearchTypeSelectMenu : public tsl::Gui {
public:
  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {
    if (keysDown & KEY_B) {
      tsl::goBack();
      return true;
    }
    return false;
  }

  virtual tsl::elm::Element *createUI() override {
    auto *frame = new tsl::elm::OverlayFrame("Search Type", "");
    auto *list = new tsl::elm::List();
    list->addItem(new tsl::elm::CategoryHeader("Select Type"));

    const std::vector<searchType_t> types = {
        SEARCH_TYPE_UNSIGNED_8BIT,  SEARCH_TYPE_SIGNED_8BIT,
        SEARCH_TYPE_UNSIGNED_16BIT, SEARCH_TYPE_SIGNED_16BIT,
        SEARCH_TYPE_UNSIGNED_32BIT, SEARCH_TYPE_SIGNED_32BIT,
        SEARCH_TYPE_UNSIGNED_64BIT, SEARCH_TYPE_SIGNED_64BIT,
        SEARCH_TYPE_FLOAT_32BIT,    SEARCH_TYPE_FLOAT_64BIT,
        SEARCH_TYPE_POINTER,        SEARCH_TYPE_UNSIGNED_40BIT};

    for (const auto type : types) {
      auto *item = new tsl::elm::ListItem(SearchTypeLabel(type));
      item->setNote(type == g_searchCondition.searchType ? "current" : "");
      item->setAlwaysShowNote(true);
      item->setClickListener([type](u64 keys) {
        if (keys & KEY_A) {
          const searchType_t oldType = g_searchCondition.searchType;
          if (oldType != type) {
            g_searchCondition.searchValue_1 =
                ConvertValueType(g_searchCondition.searchValue_1, oldType, type);
            g_searchCondition.searchValue_2 =
                ConvertValueType(g_searchCondition.searchValue_2, oldType, type);
            g_searchCondition.searchValue_3 =
                ConvertValueType(g_searchCondition.searchValue_3, oldType, type);
          }
          g_searchCondition.searchType = type;
          return tsl::goBack(), true;
        }
        return false;
      });
      list->addItem(item);
    }

    const char *selectedTypeLabel = SearchTypeLabel(g_searchCondition.searchType);
    if (selectedTypeLabel && selectedTypeLabel[0] != '\0') {
      list->jumpToItem(selectedTypeLabel, "", true);
    }

    frame->setContent(list);
    return frame;
  }
};

class SearchModeSelectMenu : public tsl::Gui {
private:
  tsl::elm::List *m_list = nullptr;
  struct ModeItemInfo {
    tsl::elm::ListItem *item;
    searchMode_t mode;
  };

  std::vector<ModeItemInfo> m_items;
  static inline bool s_showModeHelpNotes = false;

  const char *ModeMenuLabel(searchMode_t mode) const {
    switch (mode) {
    case SM_EQ:
      return "Equal (==A)";
    case SM_NE:
      return "Not equal (!=A)";
    case SM_GT:
      return "Greater than (>A)";
    case SM_GE:
      return "Greater or equal (>=A)";
    case SM_LT:
      return "Less than (<A)";
    case SM_LE:
      return "Less or equal (<=A)";
    case SM_RANGE_EQ:
      return "Range [A..B]";
    case SM_RANGE_LT:
      return "Range exclusive <A..B>";
    case SM_TWO_VALUE:
      return "Two value [A,B]";
    case SM_TWO_VALUE_PLUS:
      return "Two value + [A,,B]";
    case SM_THREE_VALUE:
      return "Three value [A.B.C]";
    case SM_STRING:
      return "String";
    case SM_MORE:
      return "More (++)";
    case SM_LESS:
      return "Less (--)";
    case SM_DIFF:
      return "Different";
    case SM_SAME:
      return "Same";
    case SM_INC_BY:
      return "Increase by A";
    case SM_DEC_BY:
      return "Decrease by A";
    case SM_EQ_plus:
      return "==*A";
    case SM_EQ_plus_plus:
      return "==**A";
    case SM_PTR:
      return "Pointer";
    case SM_NPTR:
      return "Not pointer";
    case SM_BMEQ:
      return "Bitmask (&B=A)";
    case SM_NoDecimal:
      return "No decimal [A..B]f.0";
    case SM_GETB:
      return "GetB";
    case SM_GETBZ:
      return "GetB==A";
    default:
      return "";
    }
  }

  std::string ModeHelpText(searchMode_t mode) const {
    switch (mode) {
    case SM_EQ:
      return "Match values exactly equal to A.";
    case SM_NE:
      return "Match values not equal to A.";
    case SM_GT:
      return "Match values greater than A.";
    case SM_GE:
      return "Match values greater than or equal to A.";
    case SM_LT:
      return "Match values less than A.";
    case SM_LE:
      return "Match values less than or equal to A.";
    case SM_RANGE_EQ:
      return "Match values inside inclusive range [A..B].";
    case SM_RANGE_LT:
      return "Match values strictly between A and B.";
    case SM_TWO_VALUE:
      return "Pair search: match A and B in nearby slots.";
    case SM_TWO_VALUE_PLUS:
      return "Pair+ search with wider/relaxed pairing.";
    case SM_THREE_VALUE:
      return "Triple-value search using A, B and C.";
    case SM_STRING:
      return "String search using text in A.";
    case SM_MORE:
      return "Compare against previous scan; value increased.";
    case SM_LESS:
      return "Compare against previous scan; value decreased.";
    case SM_DIFF:
      return "Compare against previous scan; value changed.";
    case SM_SAME:
      return "Compare against previous scan; value unchanged.";
    case SM_INC_BY:
      return "Value increased by exactly A from previous scan.";
    case SM_DEC_BY:
      return "Value decreased by exactly A from previous scan.";
    case SM_EQ_plus:
      return "Convert A from current search type to u32/flt/dbl and match all 3 exactly.";
    case SM_EQ_plus_plus:
      return "Convert A to u32/flt/dbl; u32 exact, flt/dbl in exclusive <A-1..A+1>.";
    case SM_PTR:
      return "Match values that look like valid pointers.";
    case SM_NPTR:
      return "Match values that are not valid pointers.";
    case SM_BMEQ:
      return "Bitmask compare: (value & B) equals A.";
    case SM_NoDecimal:
      return "Float-range match while enforcing integer-like result.";
    case SM_GETB:
      return "Take values from B-source candidate list.";
    case SM_GETBZ:
      return "Take B-source values where B equals A.";
    default:
      return "Mode behavior follows Breeze engine semantics.";
    }
  }

  void RefreshModeNotes() {
    for (auto &entry : m_items) {
      if (entry.item == nullptr)
        continue;
      if (s_showModeHelpNotes) {
        const std::string prefix =
            (entry.mode == g_searchCondition.searchMode) ? "[Current] " : "";
        entry.item->setNote(prefix + ModeHelpText(entry.mode));
      } else {
        entry.item->setNote("");
      }
    }
    if (m_list) {
      m_list->recalculateLayout();
    }
  }

  void AddModeItem(tsl::elm::List *list, const char *label, searchMode_t mode) {
    auto *item = new tsl::elm::ListItem(label);
    item->setAlwaysShowNote(true);
    item->setClickListener([mode](u64 keys) {
      if (keys & KEY_A) {
        g_searchCondition.searchMode = mode;
        return tsl::goBack(), true;
      }
      return false;
    });
    list->addItem(item);
    m_items.push_back({item, mode});
  }

public:
  ~SearchModeSelectMenu() override { ult::FOOTER_Y_HINT.clear(); }

  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {
    if (keysDown & KEY_Y) {
      s_showModeHelpNotes = !s_showModeHelpNotes;
      RefreshModeNotes();
      return true;
    }
    if (keysDown & KEY_B) {
      ult::FOOTER_Y_HINT.clear();
      tsl::goBack();
      return true;
    }
    return false;
  }

  virtual tsl::elm::Element *createUI() override {
    ult::FOOTER_Y_HINT = NOTES;
    auto *frame = new tsl::elm::OverlayFrame("Search Mode", "");
    auto *list = new tsl::elm::List();
    m_list = list;

    list->addItem(new tsl::elm::CategoryHeader("Eq* Multi-Type"));
    AddModeItem(list, "==*A", SM_EQ_plus);
    AddModeItem(list, "==**A", SM_EQ_plus_plus);

    list->addItem(new tsl::elm::CategoryHeader("Basic"));
    AddModeItem(list, "Equal (==A)", SM_EQ);
    AddModeItem(list, "Not equal (!=A)", SM_NE);
    AddModeItem(list, "Greater than (>A)", SM_GT);
    AddModeItem(list, "Greater or equal (>=A)", SM_GE);
    AddModeItem(list, "Less than (<A)", SM_LT);
    AddModeItem(list, "Less or equal (<=A)", SM_LE);

    list->addItem(new tsl::elm::CategoryHeader("Range / Multi-Value"));
    AddModeItem(list, "Range [A..B]", SM_RANGE_EQ);
    AddModeItem(list, "Range exclusive <A..B>", SM_RANGE_LT);
    AddModeItem(list, "Two value [A,B]", SM_TWO_VALUE);
    AddModeItem(list, "Two value + [A,,B]", SM_TWO_VALUE_PLUS);
    AddModeItem(list, "Three value [A.B.C]", SM_THREE_VALUE);
    AddModeItem(list, "String", SM_STRING);

    list->addItem(new tsl::elm::CategoryHeader("Relative"));
    AddModeItem(list, "More (++)", SM_MORE);
    AddModeItem(list, "Less (--)", SM_LESS);
    AddModeItem(list, "Different", SM_DIFF);
    AddModeItem(list, "Same", SM_SAME);
    AddModeItem(list, "Increase by A", SM_INC_BY);
    AddModeItem(list, "Decrease by A", SM_DEC_BY);

    list->addItem(new tsl::elm::CategoryHeader("Pointer / Other"));
    AddModeItem(list, "Pointer", SM_PTR);
    AddModeItem(list, "Not pointer", SM_NPTR);
    AddModeItem(list, "Bitmask (&B=A)", SM_BMEQ);
    AddModeItem(list, "No decimal [A..B]f.0", SM_NoDecimal);
    AddModeItem(list, "GetB", SM_GETB);
    AddModeItem(list, "GetB==A", SM_GETBZ);

    RefreshModeNotes();
    const char *selectedLabel = ModeMenuLabel(g_searchCondition.searchMode);
    if (selectedLabel && selectedLabel[0] != '\0') {
      list->jumpToItem(selectedLabel, "", true);
    }
    frame->setContent(list);
    return frame;
  }

  virtual void update() override { RefreshModeNotes(); }
};

class SearchDataMenu : public tsl::Gui {
private:
  static inline bool s_hexMode = false;
  tsl::elm::ListItem *m_hexModeItem = nullptr;
  tsl::elm::ListItem *m_itemA = nullptr;
  tsl::elm::ListItem *m_itemB = nullptr;
  tsl::elm::ListItem *m_itemC = nullptr;

  void RefreshNotes() {
    if (m_hexModeItem) {
      m_hexModeItem->setNote(s_hexMode ? ON : OFF);
    }
    if (m_itemA)
      m_itemA->setNote("A=" + SearchDataNote(g_searchCondition, 0, s_hexMode));
    if (m_itemB)
      m_itemB->setNote("B=" + SearchDataNote(g_searchCondition, 1, s_hexMode));
    if (m_itemC)
      m_itemC->setNote("C=" + SearchDataNote(g_searchCondition, 2, s_hexMode));
  }

  void OpenEditorForSlot(int slot, const std::string &title) {
    const searchType_t keyboardType =
        KeyboardTypeForDataSlot(g_searchCondition, slot, s_hexMode);
    const std::string initial = EditableDataText(g_searchCondition, slot, s_hexMode);
    tsl::changeTo<tsl::KeyboardGui>(
        keyboardType, initial, title,
        [slot](std::string result) {
          if (!ApplyDataText(g_searchCondition, slot, result, s_hexMode)) {
            if (tsl::notification) {
              tsl::notification->show("Invalid value");
            }
            return;
          }
          tsl::goBack();
        },
        nullptr, false);
  }

public:
  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {
    if (keysDown & KEY_B) {
      tsl::goBack();
      return true;
    }
    return false;
  }

  virtual tsl::elm::Element *createUI() override {
    auto *frame = new tsl::elm::OverlayFrame("Search Data", "");
    auto *list = new tsl::elm::List();
    list->addItem(new tsl::elm::CategoryHeader("Edit Values"));

    m_itemA = new tsl::elm::ListItem("A");
    m_itemA->setAlwaysShowNote(true);
    m_itemA->setClickListener([this](u64 keys) {
      if (keys & KEY_A) {
        OpenEditorForSlot(0, "Edit A");
        return true;
      }
      return false;
    });
    list->addItem(m_itemA);

    m_itemB = new tsl::elm::ListItem("B");
    m_itemB->setAlwaysShowNote(true);
    m_itemB->setClickListener([this](u64 keys) {
      if (keys & KEY_A) {
        OpenEditorForSlot(1, "Edit B");
        return true;
      }
      return false;
    });
    list->addItem(m_itemB);

    m_itemC = new tsl::elm::ListItem("C");
    m_itemC->setAlwaysShowNote(true);
    m_itemC->setClickListener([this](u64 keys) {
      if (keys & KEY_A) {
        OpenEditorForSlot(2, "Edit C");
        return true;
      }
      return false;
    });
    list->addItem(m_itemC);
    
    m_hexModeItem = new tsl::elm::ListItem("Hex mode");
    m_hexModeItem->setAlwaysShowNote(true);
    m_hexModeItem->setClickListener([this](u64 keys) {
      if (keys & KEY_A) {
        s_hexMode = !s_hexMode;
        RefreshNotes();
        return true;
      }
      return false;
    });
    list->addItem(m_hexModeItem);

    RefreshNotes();
    frame->setContent(list);
    return frame;
  }

  virtual void update() override { RefreshNotes(); }
};

class SearchSetupMenu : public tsl::Gui {
private:
  tsl::elm::ListItem *m_modeItem = nullptr;
  tsl::elm::ListItem *m_typeItem = nullptr;
  tsl::elm::ListItem *m_dataItem = nullptr;

  void RefreshNotes() {
    if (m_modeItem) {
      m_modeItem->setNote(SearchModeLabel(g_searchCondition.searchMode));
    }
    if (m_typeItem) {
      m_typeItem->setNote(SearchTypeLabel(g_searchCondition.searchType));
    }
    if (m_dataItem) {
      m_dataItem->setNote(SearchDataNote(g_searchCondition, 0) + " " +
                          SearchDataNote(g_searchCondition, 1) + " " +
                          SearchDataNote(g_searchCondition, 2));
    }
  }

public:
  virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {
    if (keysDown & KEY_B) {
      tsl::goBack();
      return true;
    }
    return false;
  }

  virtual tsl::elm::Element *createUI() override {
    auto *frame = new tsl::elm::OverlayFrame("Setup Search", "");
    auto *list = new tsl::elm::List();
    list->addItem(new tsl::elm::CategoryHeader("Search Condition"));

    m_modeItem = new tsl::elm::ListItem("Search mode");
    m_modeItem->setAlwaysShowNote(true);
    m_modeItem->setClickListener([](u64 keys) {
      if (keys & KEY_A) {
        tsl::changeTo<SearchModeSelectMenu>();
        return true;
      }
      return false;
    });
    list->addItem(m_modeItem);

    m_typeItem = new tsl::elm::ListItem("Search type");
    m_typeItem->setAlwaysShowNote(true);
    m_typeItem->setClickListener([](u64 keys) {
      if (keys & KEY_A) {
        tsl::changeTo<SearchTypeSelectMenu>();
        return true;
      }
      return false;
    });
    list->addItem(m_typeItem);

    m_dataItem = new tsl::elm::ListItem("Search data");
    m_dataItem->setAlwaysShowNote(true);
    m_dataItem->setClickListener([](u64 keys) {
      if (keys & KEY_A) {
        tsl::changeTo<SearchDataMenu>();
        return true;
      }
      return false;
    });
    list->addItem(m_dataItem);

    RefreshNotes();
    frame->setContent(list);
    return frame;
  }

  virtual void update() override { RefreshNotes(); }
};

} // namespace

class MainMenu : public tsl::Gui {

private:
  static constexpr const char *SEARCH_MANAGER_MENU_MODE = "search_manager";
  std::string packageIniPath = PACKAGE_PATH + PACKAGE_FILENAME;
  std::string packageConfigIniPath = PACKAGE_PATH + CONFIG_FILENAME;
  std::string menuMode, fullPath, optionName, priority, starred, hide;
  bool useDefaultMenu = false;
  bool useOverlayLaunchArgs = false;
  std::string hiddenMenuMode, dropdownSection;
  u8 m_cheatFontSize = 21;
  std::string m_notesPath = "";
  bool m_notesLoaded = false;
  tsl::elm::List *cheatList = nullptr;
  u64 m_lastTitleId = 0;
  u64 m_lastBuildId = 0;
  u32 m_updateCounter = 0;
  static constexpr u32 CHECK_INTERVAL = 50;
  bool m_noGameRunning = false;
  tsl::elm::ListItem *m_setupSearchItem = nullptr;
  tsl::elm::ListItem *m_startSearchItem = nullptr;
  tsl::elm::ListItem *m_continueSearchItem = nullptr;
  tsl::elm::ListItem *m_lastBufferItem = nullptr;
  tsl::elm::ListItem *m_lastTimeItem = nullptr;

  void focusSearchProgressItem() {
    if (menuMode == SEARCH_MANAGER_MENU_MODE && m_lastTimeItem) {
      tsl::shiftItemFocus(m_lastTimeItem);
    }
  }
  // bool initializingSpawn = false;
  // std::string defaultLang = "en";

public:
  /**
   * @brief Constructs a `MainMenu` instance.
   *
   * Initializes a new instance of the `MainMenu` class with the necessary
   * parameters.
   */
  MainMenu(const std::string &hiddenMenuMode = "",
           const std::string &sectionName = "")
      : hiddenMenuMode(hiddenMenuMode), dropdownSection(sectionName) {
    std::lock_guard<std::mutex> lock(transitionMutex);
    // dmntchtInitialize moved to Overlay::initServices to support background
    // thread
    // dmntchtInitialize();
    // tsl::gfx::FontManager::clearCache();
    {
      // std::lock_guard<std::mutex> lock(jumpItemMutex);
      if (skipJumpReset.exchange(false, std::memory_order_acq_rel)) {
        return;
      }

      jumpItemName = std::move(returnJumpItemName);
      jumpItemValue = std::move(returnJumpItemValue);
      
      // If jumpItemName is set (from focus restoration), use exact match
      if (!jumpItemName.empty()) {
        jumpItemExactMatch.store(true, release);
      } else {
        jumpItemExactMatch.store(false, release);
      }
    }
    settingsInitialized.store(true, release);
  }
  /**
   * @brief Destroys the `MainMenu` instance.
   *
   * Cleans up any resources associated with the `MainMenu` instance.
   */
  ~MainMenu() {
    std::lock_guard<std::mutex> lock(transitionMutex);
    // dmntchtExit moved to Overlay::exitServices
    // dmntchtExit();
  }

  /**
   * @brief Creates the graphical user interface (GUI) for the main menu
   * overlay.
   *
   * This function initializes and sets up the GUI elements for the main menu
   * overlay, allowing users to navigate and access various submenus.
   *
   * @return A pointer to the GUI element representing the main menu overlay.
   */
  virtual tsl::elm::Element *createUI() override {
    std::lock_guard<std::mutex> lock(transitionMutex);

    // Handle hidden mode flags
    {
      auto iniData = getParsedDataFromIniFile(ULTRAHAND_CONFIG_INI_PATH);
      auto &ultrahandSection = iniData[ULTRAHAND_PROJECT_NAME];
      bool needsUpdate = false;

      auto overlayIt = ultrahandSection.find(IN_HIDDEN_OVERLAY_STR);
      if (overlayIt != ultrahandSection.end() &&
          overlayIt->second == TRUE_STR) {
        inMainMenu.store(false, std::memory_order_release);
        inHiddenMode.store(true, std::memory_order_release);
        hiddenMenuMode = OVERLAYS_STR;
        skipJumpReset.store(true, release);
        ultrahandSection[IN_HIDDEN_OVERLAY_STR] = FALSE_STR;
        needsUpdate = true;
      } else {
        auto packageIt = ultrahandSection.find(IN_HIDDEN_PACKAGE_STR);
        if (packageIt != ultrahandSection.end() &&
            packageIt->second == TRUE_STR) {
          inMainMenu.store(false, std::memory_order_release);
          inHiddenMode.store(true, std::memory_order_release);
          hiddenMenuMode = PACKAGES_STR;
          skipJumpReset.store(true, release);
          ultrahandSection[IN_HIDDEN_PACKAGE_STR] = FALSE_STR;
          needsUpdate = true;
        }
      }

      if (needsUpdate) {
        saveIniFileData(ULTRAHAND_CONFIG_INI_PATH, iniData);
      }
    }

    if (!inHiddenMode.load(std::memory_order_acquire) &&
        dropdownSection.empty())
      inMainMenu.store(true, std::memory_order_release);
    else
      inMainMenu.store(false, std::memory_order_release);

    const bool showRestartInFooter =
        takeOverOvlmenu && inMainMenu.load(std::memory_order_acquire) &&
        !inHiddenMode.load(std::memory_order_acquire) &&
        dropdownSection.empty();
    setFooterBackLabel(showRestartInFooter);

    lastMenuMode = hiddenMenuMode;

    static bool hasInitialized = false;
    if (!hasInitialized) {
      if (!inOverlay) {
        currentMenu = usePageSwap ? PACKAGES_STR : OVERLAYS_STR;
      }
      hasInitialized = true;
    }

    if (toPackages) {
      setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
                      "to_packages", FALSE_STR);
      toPackages = false;
      currentMenu = OVERLAYS_STR;//hack it, something keep setting to_packages to true
    }

    menuMode = !hiddenMenuMode.empty() ? hiddenMenuMode : currentMenu;

    auto *list = new tsl::elm::List();
    bool noClickableItems = false;

    if (menuMode == SEARCH_MANAGER_MENU_MODE) {
      createSearchManagerMenu(list);
    } else if (menuMode == OVERLAYS_STR) {
      createCheatsMenu(list);
    } else if (menuMode == PACKAGES_STR) {
      noClickableItems = createPackagesMenu(list);
    } else {
      // Fallback to cheats page if menu mode is unknown.
      menuMode = OVERLAYS_STR;
      createCheatsMenu(list);
    }

    std::string frameTitle = CAPITAL_ULTRAHAND_PROJECT_NAME;
    if (menuMode == OVERLAYS_STR && !g_cheatFolderNameStack.empty()) {
      frameTitle = g_cheatFolderNameStack.back();
    }
    std::string pageLeftName;
    std::string pageRightName;
    if (menuMode == SEARCH_MANAGER_MENU_MODE) {
      pageRightName = CHEATS;
    } else if (menuMode == OVERLAYS_STR) {
      pageLeftName = "Search";
      pageRightName = PACKAGES;
    } else if (menuMode == PACKAGES_STR) {
      pageLeftName = CHEATS;
    }

    // Use custom page labels for 3-panel navigation and avoid duplicate footer
    // labels from m_menuMode defaults.
    const std::string frameMenuMode =
        (!pageLeftName.empty() || !pageRightName.empty()) ? "" : menuMode;
    auto *rootFrame = new tsl::elm::OverlayFrame(
        frameTitle, versionLabel, noClickableItems, frameMenuMode, "",
        pageLeftName, pageRightName);


    list->jumpToItem(jumpItemName, jumpItemValue,
                     jumpItemExactMatch.load(acquire));
    // if (g_overlayFilename != "ovlmenu.ovl") {
    //     list->jumpToItem(jumpItemName, jumpItemValue,
    //     jumpItemExactMatch.load(acquire));
    // } else {
    //     //g_overlayFilename = "";
    // }

    rootFrame->setContent(list);
    return rootFrame;
  }

  void createSearchManagerMenu(tsl::elm::List *list) {
    inOverlaysPage.store(false, std::memory_order_release);
    inPackagesPage.store(false, std::memory_order_release);

    addHeader(list, "Search Manager",
              std::string("\uE0E2 ") + "Edit \uE0F0 Pause/View");

    const size_t purgedCandidates = PurgeInvalidCandidatesForCurrentProcess();
    if (!g_searchContinueSourcePath.empty()) {
      struct stat st {};
      if (stat(g_searchContinueSourcePath.c_str(), &st) != 0) {
        g_searchContinueSourcePath.clear();
      }
    }
    if (!g_searchConditionSourcePath.empty()) {
      struct stat st {};
      if (stat(g_searchConditionSourcePath.c_str(), &st) != 0) {
        g_searchConditionSourcePath.clear();
      }
    }
    if (purgedCandidates > 0 && tsl::notification) {
      tsl::notification->show("Removed " + std::to_string(purgedCandidates) +
                              " stale candidate files");
    }

    if (!TryLoadConditionFromLatestCandidate() && !g_searchConditionReady) {
      g_searchCondition = Search_condition{};
      g_searchConditionReady = true;
    }
    if (g_searchContinueSourcePath.empty()) {
      std::string latest;
      if (GetLatestCandidatePath(latest)) {
        g_searchContinueSourcePath = latest;
      }
    }
    if (g_startSearchOutputName.empty()) {
      g_startSearchOutputName = AutoGenerateStartOutputName();
    }
    if (g_continueSearchOutputName.empty()) {
      g_continueSearchOutputName =
          AutoGenerateContinueOutputName(g_searchContinueSourcePath);
    }

    auto *setupSearchItem = new tsl::elm::ListItem("Setup search");
    setupSearchItem->setAlwaysShowNote(true);
    setupSearchItem->setNote(SearchConditionSummary(g_searchCondition));
    setupSearchItem->setClickListener([](u64 keys) {
      if (keys & KEY_A) {
        tsl::changeTo<SearchSetupMenu>();
        return true;
      }
      return false;
    });
    list->addItem(setupSearchItem);
    m_setupSearchItem = setupSearchItem;

    auto *startSearchItem = new tsl::elm::ListItem("Start search");
    startSearchItem->setAlwaysShowNote(true);
    startSearchItem->setNote(DisplayStartOutputStem(g_startSearchOutputName));
    startSearchItem->setClickListener([](u64 keys) {
      if (keys & KEY_X) {
        tsl::changeTo<tsl::KeyboardGui>(
            SEARCH_TYPE_TEXT, DisplayStartOutputStem(g_startSearchOutputName),
            "Output file",
            [](std::string result) {
              const std::string trimmed = TrimCopy(result);
              if (trimmed.empty()) {
                if (tsl::notification) {
                  tsl::notification->show("File name cannot be empty");
                }
                return;
              }
              g_startSearchOutputName = trimmed;
              if (g_startSearchOutputName.size() > 4 &&
                  g_startSearchOutputName.substr(g_startSearchOutputName.size() - 4) ==
                      ".dat") {
                g_startSearchOutputName.erase(g_startSearchOutputName.size() - 4);
              }
              tsl::goBack();
            },
            nullptr, false);
        return true;
      }
      if (keys & KEY_A) {
        if (!checkOverlayMemory(8)) {
          return true;
        }
        if (!g_searchContinueSourcePath.empty() &&
            CandidatePathFromStem(g_startSearchOutputName) ==
                g_searchContinueSourcePath) {
          g_startSearchOutputName = AutoGenerateStartOutputName();
        }
        QueueSearchAction(SEARCH_QUEUED_START);
        return true;
      }
      return false;
    });
    list->addItem(startSearchItem);
    m_startSearchItem = startSearchItem;

    auto *continueSearchItem = new tsl::elm::ListItem("Continue search");
    continueSearchItem->setAlwaysShowNote(true);
    continueSearchItem->setNote(
        ContinueSearchNoteFromPath(g_searchContinueSourcePath));
    continueSearchItem->setClickListener([](u64 keys) {
      if (keys & KEY_X) {
        const std::string sourceStem =
            CandidateStemFromPath(g_searchContinueSourcePath);
        g_continueSourceFilterMode = 2;
        g_continueSourceFilterAnchorStem = sourceStem;
        g_continueSourceJumpStem = sourceStem;
        tsl::changeTo<ContinueSearchFileMenu>();
        return true;
      }
      if (keys & KEY_A) {
        return TryQueueContinueSearchFromUi();
      }
      return false;
    });
    list->addItem(continueSearchItem);
    m_continueSearchItem = continueSearchItem;

    auto *editAAndContinueItem = new tsl::elm::ListItem("Edit A and continue");
    editAAndContinueItem->setClickListener([](u64 keys) {
      if (!(keys & KEY_A)) {
        return false;
      }
      const searchType_t keyboardType =
          KeyboardTypeForDataSlot(g_searchCondition, 0, false);
      const std::string initial = EditableDataText(g_searchCondition, 0, false);
      tsl::changeTo<tsl::KeyboardGui>(
          keyboardType, initial, "Edit A and continue",
          [](std::string result) {
            if (!ApplyDataText(g_searchCondition, 0, result, false)) {
              if (tsl::notification) {
                tsl::notification->show("Invalid value");
              }
              return;
            }
            tsl::goBack();
            TryQueueContinueSearchFromUi();
          },
          nullptr, false);
      return true;
    });
    list->addItem(editAAndContinueItem);

    list->addItem(new tsl::elm::CategoryHeader("Information"));

    auto *lastTimeItem = new tsl::elm::ListItem(SearchTimeItemTitle());
    lastTimeItem->setAlwaysShowNote(true);
    lastTimeItem->setNote(LastSearchTimeNote());
    list->addItem(lastTimeItem);
    m_lastTimeItem = lastTimeItem;

    auto *lastBufferItem = new tsl::elm::ListItem("Buffer size");
    lastBufferItem->setAlwaysShowNote(true);
    lastBufferItem->setNote(LastSearchBufferNote());
    list->addItem(lastBufferItem);
    m_lastBufferItem = lastBufferItem;
  }

  void createCheatsMenu(tsl::elm::List *list) {
    inOverlaysPage.store(true, std::memory_order_release);
    inPackagesPage.store(false, std::memory_order_release);

    // Display game info at the top
    if (!hideUserGuide && dropdownSection.empty()) {
      DmntCheatProcessMetadata metadata;
      if (R_SUCCEEDED(dmntchtGetCheatProcessMetadata(&metadata))) {
        this->cheatList = list;

        if (m_notesPath.empty()) {
          std::stringstream ss;
          ss << "sdmc:/switch/breeze/cheats/" << std::setfill('0')
             << std::setw(16) << std::uppercase << std::hex << metadata.title_id
             << "/notes.txt";
          m_notesPath = ss.str();
        }

        if (!m_notesLoaded && !m_notesPath.empty()) {
          std::string fontSizeStr =
              ult::parseValueFromIniSection(m_notesPath, "Breeze", "font_size");
          if (!fontSizeStr.empty()) {
            m_cheatFontSize =
                std::clamp(static_cast<int>(ult::stoi(fontSizeStr)), 10, 30);
          }
          std::string showNotesStr = ult::parseValueFromIniSection(
              m_notesPath, "Breeze", "show_notes");
          if (!showNotesStr.empty()) {
            ult::showCheatNotes = (showNotesStr == "true");
          }
          m_notesLoaded = true;
        }

        CheatUtils::EnsureMetadata();
        std::string tidStr = CheatUtils::GetTitleIdString();
        std::string bidStr = CheatUtils::GetBuildIdString();

        std::string titleStr = "";
        std::string versionStr = "";
        static NsApplicationControlData appControlData;
        size_t appControlDataSize = 0;
        NacpLanguageEntry *languageEntry = nullptr;
        if (R_SUCCEEDED(nsGetApplicationControlData(
                NsApplicationControlSource_Storage,
                metadata.title_id & 0xFFFFFFFFFFFFFFF0, &appControlData,
                sizeof(NsApplicationControlData), &appControlDataSize))) {
          if (R_SUCCEEDED(nsGetApplicationDesiredLanguage(&appControlData.nacp,
                                                          &languageEntry)) &&
              languageEntry) {
            titleStr = languageEntry->name;
          }
          versionStr = appControlData.nacp.display_version;
        }

        if (titleStr.empty()) {
          titleStr = tidStr;
          std::string titleFilePath =
              "sdmc:/switch/breeze/cheats/" + tidStr + "/title.txt";
          if (ult::isFile(titleFilePath)) {
            std::ifstream tfile(titleFilePath);
            if (std::getline(tfile, titleStr))
              ult::trim(titleStr);
          }
        }

        
        // Create CustomDrawer for game info with text wrapping (includes header)
        std::string fullTitle = titleStr + (versionStr.empty() ? "" : " v" + versionStr);
        std::string tidLine = "TID: " + tidStr;
        std::string bidLine = "BID: " + bidStr;
        
        const s32 fontSize = 20;
        const s32 lineHeight = 24;
        const s32 maxWidth = tsl::cfg::FramebufferWidth - 50;
        
        auto *gameInfoDrawer = new tsl::elm::CustomDrawer([fullTitle, tidLine, bidLine, fontSize, lineHeight, maxWidth](tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, s32 h) {
            s32 currentY = y + lineHeight;  // Top padding
            
            // Draw "Current Game" header
            // r->drawString("Current Game", false, x + 10, currentY, fontSize, tsl::style::color::ColorText);
            // currentY += lineHeight;
            
            // Helper lambda to wrap and draw text
            auto drawWrappedText = [&](const std::string& text, s32& yPos) {
                std::string currentLine;
                std::string word;
                
                for (size_t i = 0; i <= text.length(); ++i) {
                    if (i == text.length() || text[i] == ' ') {
                        // End of word or string
                        if (!word.empty()) {
                            std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
                            auto dims = r->getTextDimensions(testLine, false, fontSize);
                            
                            if (dims.first <= maxWidth - 40) {
                                currentLine = testLine;
                            } else {
                                // Line would be too long, draw current line and start new one
                                if (!currentLine.empty()) {
                                    r->drawString(currentLine, false, x + 10, yPos, fontSize, tsl::style::color::ColorText);
                                    yPos += lineHeight;
                                    currentLine = word;
                                } else {
                                    // Single word is too long, draw it anyway
                                    r->drawString(word, false, x + 10, yPos, fontSize, tsl::style::color::ColorText);
                                    yPos += lineHeight;
                                    currentLine = "";
                                }
                            }
                            word = "";
                        }
                    } else {
                        word += text[i];
                    }
                }
                
                // Draw remaining text
                if (!currentLine.empty()) {
                    r->drawString(currentLine, false, x + 10, yPos, fontSize, tsl::style::color::ColorText);
                    yPos += lineHeight;
                }
            };
            
            // Draw title (with wrapping)
            drawWrappedText(fullTitle, currentY);
            
            // Draw TID
            r->drawString(tidLine, false, x + 10, currentY, fontSize, tsl::style::color::ColorText);
            currentY += lineHeight;
            
            // Draw BID
            r->drawString(bidLine, false, x + 10, currentY, fontSize, tsl::style::color::ColorText);
            currentY += lineHeight;
            

        });
        
        // Use generous fixed height (accommodates up to 3-line wrapped titles)
        gameInfoDrawer->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, lineHeight*4);
        list->addItem(gameInfoDrawer);

        // auto *titleItem = new tsl::elm::ListItem(
        //     titleStr + (versionStr.empty() ? "" : " v" + versionStr));
        // titleItem->setUseWrapping(true);
        // titleItem->setFontSize(m_cheatFontSize);
        // list->addItem(titleItem);

        // auto *tidItem = new tsl::elm::ListItem("TID: " + tidStr);
        // tidItem->setUseWrapping(true);
        // tidItem->setFontSize(m_cheatFontSize);
        // list->addItem(tidItem);

        // auto *bidItem = new tsl::elm::ListItem("BID: " + bidStr);
        // bidItem->setUseWrapping(true);
        // bidItem->setFontSize(m_cheatFontSize);
        // list->addItem(bidItem);
      }
    }

    addHeader(list, ult::CHEATS,
              std::string("\uE0E3 ") + ult::NOTES + " " + DIVIDER_SYMBOL +
                  " \uE0E2 " + ult::SETTINGS);

    bool hasCheatProcess = false;
    dmntchtHasCheatProcess(&hasCheatProcess);
    if (!hasCheatProcess) {
      dmntchtForceOpenCheatProcess();
      dmntchtHasCheatProcess(&hasCheatProcess);
    }

    if (!hasCheatProcess) {
      list->addItem(new tsl::elm::ListItem("No game running"));
      return;
    }

    u64 cheatCount = 0;
    this->cheatList = list;

    DmntCheatProcessMetadata metadata;
    std::string notesPath;
    if (R_SUCCEEDED(dmntchtGetCheatProcessMetadata(&metadata))) {
      std::stringstream ss;
      ss << "sdmc:/switch/breeze/cheats/" << std::setfill('0') << std::setw(16)
         << std::uppercase << std::hex << metadata.title_id << "/notes.txt";
      m_notesPath = ss.str();
    }

    if (!m_notesLoaded && !m_notesPath.empty()) {
      std::string fontSizeStr =
          ult::parseValueFromIniSection(m_notesPath, "Breeze", "font_size");
      if (!fontSizeStr.empty()) {
        m_cheatFontSize =
            std::clamp(static_cast<int>(ult::stoi(fontSizeStr)), 10, 30);
      }
      std::string showNotesStr =
          ult::parseValueFromIniSection(m_notesPath, "Breeze", "show_notes");
      if (!showNotesStr.empty()) {
        ult::showCheatNotes = (showNotesStr == "true");
      }
      m_notesLoaded = true;
    }

    auto notesData = getParsedDataFromIniFile(m_notesPath);

    if (R_SUCCEEDED(dmntchtGetCheatCount(&cheatCount)) && cheatCount > 0) {
      std::vector<DmntCheatEntry> cheats(cheatCount);
      if (R_SUCCEEDED(
              dmntchtGetCheats(cheats.data(), cheatCount, 0, &cheatCount))) {

        if (!g_cheatFolderNameStack.empty()) {
          auto *backItem = new tsl::elm::ListItem(".. [Back]");
          backItem->setFontSize(m_cheatFontSize);
          backItem->setClickListener([](u64 keys) {
            if (keys & KEY_A) {
              if (!g_cheatFolderNameStack.empty()) {
                jumpItemName = "\uE132 " + g_cheatFolderNameStack.back();
                jumpItemExactMatch.store(true, release);
                skipJumpReset.store(true, release);
                g_cheatFolderNameStack.pop_back();
              }
              if (!g_cheatFolderIndexStack.empty())
                g_cheatFolderIndexStack.pop_back();
              refreshPage.store(true, std::memory_order_release);
              return true;
            }
            return false;
          });
          list->addItem(backItem);
        }

        u32 currentDepth = 0;
        u32 targetDepth = g_cheatFolderIndexStack.size();
        bool inTargetFolder = (targetDepth == 0);
        u32 targetMatchCount = 0;

        for (u32 i = 0; i < cheatCount; i++) {
          const auto &cheat = cheats[i];
          u32 opcode = (cheat.definition.num_opcodes > 0)
                           ? cheat.definition.opcodes[0]
                           : 0;
          bool isFolderStart = (opcode == 0x20000000);
          bool isFolderEnd = (opcode == 0x20000001);

          if (isFolderStart) {
            if (inTargetFolder && currentDepth == targetDepth) {
              std::string folderName =
                  "\uE132 " + std::string(cheat.definition.readable_name);
              auto *folderItem = new tsl::elm::ListItem(folderName);
              folderItem->setUseWrapping(true);
              folderItem->setFontSize(m_cheatFontSize);

              auto it = notesData.find(cheat.definition.readable_name);
              if (it != notesData.end()) {
                auto noteIt = it->second.find("note");
                if (noteIt != it->second.end())
                  folderItem->setNote(noteIt->second);
              }

              u32 folderIdx = i;
              std::string rawName = cheat.definition.readable_name;
              folderItem->setClickListener([folderIdx, rawName](u64 keys) {
                if (keys & KEY_A) {
                  g_cheatFolderIndexStack.push_back(folderIdx);
                  g_cheatFolderNameStack.push_back(rawName);
                  refreshPage.store(true, std::memory_order_release);
                  return true;
                }
                return false;
              });
              list->addItem(folderItem);
            }
            currentDepth++;
            if (!inTargetFolder && currentDepth <= targetDepth) {
              if (i == g_cheatFolderIndexStack[currentDepth - 1]) {
                targetMatchCount = currentDepth;
                if (targetMatchCount == targetDepth)
                  inTargetFolder = true;
              }
            }
            continue;
          }

          if (isFolderEnd) {
            if (currentDepth == targetDepth && inTargetFolder)
              inTargetFolder = false;
            if (currentDepth > 0)
              currentDepth--;
            if (targetMatchCount > currentDepth) {
              targetMatchCount = currentDepth;
              inTargetFolder = (targetMatchCount == targetDepth);
            }
            continue;
          }

          if (inTargetFolder && currentDepth == targetDepth) {
            u32 key_mask = 0;
            if (cheat.definition.num_opcodes >= 1) {
              u32 firstOp = cheat.definition.opcodes[0];
              if ((firstOp & 0xF0000000) == 0x80000000)
                key_mask = firstOp & 0x0FFFFFFF;
            }

            std::string rawName = cheat.definition.readable_name;
            bool isMaster = (cheat.cheat_id == 0);
            std::string displayName =
                CheatUtils::GetComboKeyGlyphs(key_mask) + rawName;

            tsl::elm::ListItem *item;
            if (isMaster) {
              item = new tsl::elm::ListItem(displayName);
              item->setTextColor(
                  tsl::style::color::ColorDescription); // Grey for Master Codes
            } else {
              auto *toggleItem = new CheatUtils::CheatToggleItem(
                  displayName, cheat.enabled, cheat.cheat_id, m_cheatFontSize);
              toggleItem->setStateChangedListener(
                  [cheat](bool state) { dmntchtToggleCheat(cheat.cheat_id); });
              item = toggleItem;
            }

            item->setFontSize(m_cheatFontSize);
            item->setUseWrapping(true);

            auto it = notesData.find(cheat.definition.readable_name);
            if (it != notesData.end()) {
              auto noteIt = it->second.find("note");
              if (noteIt != it->second.end())
                item->setNote(noteIt->second);
            }

            item->setClickListener([cheat, displayName](u64 keys) {
              if (keys & KEY_X) {
                tsl::changeTo<CheatMenu>(cheat.cheat_id,
                                         cheat.definition.readable_name);
                return true;
              }
              if (keys & KEY_MINUS) {
                // tsl::changeTo<CheatEditMenu>(cheat.cheat_id,
                // cheat.definition.readable_name, cheat.enabled);
                std::string path = "sdmc:/switch/.overlays/editcheat.ovl";
                const u32 currentMB = bytesToMB(static_cast<u64>(currentHeapSize));
                if (currentMB >= 6) {
                  path = "sdmc:/switch/.overlays/editcheatk.ovl";
                }
                if (!ult::isFile(path)) {
                  const std::string fallbackPath = "sdmc:/switch/.overlays/editcheat.ovl";
                  if (ult::isFile(fallbackPath)) {
                    path = fallbackPath;
                  }
                }
                if (!ult::isFile(path)) {
                  if (tsl::notification) {
                    tsl::notification->show("Missing editcheat.ovl/editcheatk.ovl");
                  }
                  return true;
                }
                std::string args =
                    "--cheat_id " + std::to_string(cheat.cheat_id) +
                    " --cheat_name " + cheat.definition.readable_name +
                    " --enabled " + std::to_string(cheat.enabled);
                
                // Pass focus info and folder stack for restoration
                args += " --focus_cheat_name \"" + displayName + "\"";
                
                // Pass folder stack (both indices and names)
                if (!g_cheatFolderIndexStack.empty()) {
                  args += " --folder_indices";
                  for (u32 idx : g_cheatFolderIndexStack) {
                    args += " " + std::to_string(idx);
                  }
                  args += " --folder_names";
                  for (const auto& name : g_cheatFolderNameStack) {
                    args += " \"" + name + "\"";
                  }
                }

                std::lock_guard<std::mutex> lock(ult::overlayLaunchMutex);
                ult::requestedOverlayPath = path;
                ult::requestedOverlayArgs = args;
                ult::setIniFileValue(ult::ULTRAHAND_CONFIG_INI_PATH,
                                     ult::ULTRAHAND_PROJECT_NAME,
                                     ult::IN_OVERLAY_STR, ult::TRUE_STR);
                ult::overlayLaunchRequested.store(true,
                                                  std::memory_order_release);
                return true;
              }
              return false;
            });

            list->addItem(item);
          }
        }
      } else {
        list->addItem(new tsl::elm::ListItem("Failed to retrieve cheats"));
      }
    } else {
      // Auto-load Logic
      static u64 lastAutoLoadTid = 0;
      static u64 lastAutoLoadBid = 0;
      bool alreadyTried =
          (lastAutoLoadTid == metadata.title_id &&
           lastAutoLoadBid == *((u64 *)metadata.main_nso_build_id));

      if (!alreadyTried) {
        lastAutoLoadTid = metadata.title_id;
        lastAutoLoadBid = *((u64 *)metadata.main_nso_build_id);

        std::string tidStr = CheatUtils::GetTitleIdString();
        std::string bidStr = CheatUtils::GetBuildIdString();
        std::string localPath =
            "sdmc:/switch/breeze/cheats/" + tidStr + "/" + bidStr + ".txt";

        if (ult::isFile(localPath)) {
          if (CheatUtils::ParseCheats(localPath)) {
            refreshPage.store(true, std::memory_order_release);
            return;
          }
        }

        // Fallback to Download
        // if (CheatUtils::TryDownloadCheats(false)) { // false = quiet mode
        //     refreshPage.store(true, std::memory_order_release);
        //     return;
        // }
      }

      list->addItem(new tsl::elm::ListItem("No cheats found"));
    }


  }

  /* LEGACY OVERLAY CODE - DISABLED
  void legacyCreateOverlaysMenu(tsl::elm::List* list) {
      if (overlaySet.empty()) {
          addSelectionIsEmptyDrawer(list);
      } else {
          // Pre-allocate string buffers for parsing loop
          std::string overlayFileName, overlayName, overlayVersion,
  newOverlayName, displayVersion; overlayFileName.reserve(64);
          overlayName.reserve(128);
          overlayVersion.reserve(64);
          newOverlayName.reserve(192);
          displayVersion.reserve(64);


          // Helper to build return name for overlays (defined before listItem)
          auto buildOverlayReturnName = [](bool isStarred, const std::string&
  fileName, const std::string& displayName) -> std::string { std::string name;
              if (!isStarred) {
                  name.reserve(STAR_SYMBOL.size() + 2 + displayName.size() + 1 +
  fileName.size()); name = STAR_SYMBOL; name += "  "; name += displayName; }
  else { name = displayName;
              }
              name += '?';
              name += fileName;
              return name;
          };






                      if (wasInHiddenMode) {
                          inMainMenu.store(false, std::memory_order_release);
                          reloadMenu2 = true;
                      }

                      refreshPage.store(true, std::memory_order_release);
                      triggerRumbleClick.store(true, std::memory_order_release);
                      triggerMoveSound.store(true, std::memory_order_release);
                      return true;
                  }

                  if (keys & SETTINGS_KEY && cleanKeys == SETTINGS_KEY) {
                      if (!inHiddenMode.load(std::memory_order_acquire)) {
                          lastMenu = "";
                          inMainMenu.store(false, std::memory_order_release);
                      } else {
                          lastMenu = "hiddenMenuMode";
                          inHiddenMode.store(false, std::memory_order_release);
                      }

                      returnJumpItemName = buildOverlayReturnName(newStarred,
  overlayFileName, overlayName); returnJumpItemValue = hideOverlayVersions ? ""
  : overlayVersion; jumpItemName = jumpItemValue = "";

                      tsl::shiftItemFocus(listItem);
                      tsl::changeTo<SettingsMenu>(overlayFileName, OVERLAY_STR,
  overlayName, overlayVersion, "", !supportsAMS110);
                      triggerRumbleClick.store(true, std::memory_order_release);
                      triggerSettingsSound.store(true,
  std::memory_order_release); return true;
                  }

                  if (keys & SYSTEM_SETTINGS_KEY && cleanKeys ==
  SYSTEM_SETTINGS_KEY) { returnJumpItemName = buildOverlayReturnName(newStarred,
  overlayFileName, overlayName); returnJumpItemValue = hideOverlayVersions ? ""
  : overlayVersion; return true;
                  }

                  return false;
              });

              if (requiresAMS110Handling) {
                  listItem->isLocked = true;
              }
              listItem->disableClickAnimation();
              list->addItem(listItem);
          }

          overlaySet.clear();
      }

      if (drawHiddenTab && !inHiddenMode.load(std::memory_order_acquire) &&
  !hideHidden) { tsl::elm::ListItem* listItem = new tsl::elm::ListItem(HIDDEN,
  DROPDOWN_SYMBOL); listItem->setClickListener([listItem](uint64_t keys) { if
  (runningInterpreter.load(std::memory_order_acquire)) return false; if
  (simulatedMenu.load(std::memory_order_acquire)) { keys |= SYSTEM_SETTINGS_KEY;
              }

              if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
                  jumpItemName = "";
                  jumpItemValue = "";
                  jumpItemExactMatch.store(true, std::memory_order_release);
                  inMainMenu.store(false, std::memory_order_release);
                  inHiddenMode.store(true, std::memory_order_release);
                  tsl::shiftItemFocus(listItem);
                  tsl::changeTo<MainMenu>(OVERLAYS_STR);
                  return true;
              } else if (keys & SYSTEM_SETTINGS_KEY && !(keys &
  ~SYSTEM_SETTINGS_KEY & ALL_KEYS_MASK)) { returnJumpItemName = "";
                  returnJumpItemValue = DROPDOWN_SYMBOL;
                  return true;
              }
              return false;
          });
          list->addItem(listItem);
      }
  }
  */

  bool createPackagesMenu(tsl::elm::List *list) {
    if (!isFile(PACKAGE_PATH + PACKAGE_FILENAME)) {
      deleteFileOrDirectory(PACKAGE_PATH + CONFIG_FILENAME);

      FILE *packageFileOut =
          fopen((PACKAGE_PATH + PACKAGE_FILENAME).c_str(), "w");
      if (packageFileOut) {
        constexpr const char packageContent[] =
            "[*Reboot To]\n[*Boot Entry]\nini_file_source "
            "/bootloader/hekate_ipl.ini\nfilter config\nreboot boot "
            "'{ini_file_source(*)}'\n[hekate - \uE073]\nreboot HEKATE\n[hekate "
            "UMS - \uE073\uE08D]\nreboot UMS\n\n[Commands]\n[Shutdown - "
            "]\n;hold=true\nshutdown\n";
        fwrite(packageContent, sizeof(packageContent) - 1, 1, packageFileOut);
        fclose(packageFileOut);
      }
    }

    inOverlaysPage.store(false, std::memory_order_release);
    inPackagesPage.store(true, std::memory_order_release);

    bool noClickableItems = false;

    if (dropdownSection.empty()) {
      createDirectory(PACKAGE_PATH);

      if (!isFile(PACKAGES_INI_FILEPATH)) {
        FILE *createFile = fopen(PACKAGES_INI_FILEPATH.c_str(), "w");
        if (createFile)
          fclose(createFile);
      }

      std::set<std::string> packageSet;
      bool drawHiddenTab = false;

      // Scope to immediately free INI data
      {
        auto packagesIniData = getParsedDataFromIniFile(PACKAGES_INI_FILEPATH);
        auto subdirectories = getSubdirectories(PACKAGE_PATH);

        subdirectories.erase(std::remove_if(subdirectories.begin(),
                                            subdirectories.end(),
                                            [](const std::string &dirName) {
                                              return dirName.front() == '.';
                                            }),
                             subdirectories.end());

        bool packagesNeedsUpdate = false;

        for (const auto &packageName : subdirectories) {
          auto packageIt = packagesIniData.find(packageName);
          if (packageIt == packagesIniData.end()) {
            const PackageHeader packageHeader = getPackageHeaderFromIni(
                PACKAGE_PATH + packageName + "/" + PACKAGE_FILENAME);

            auto &packageSection = packagesIniData[packageName];
            packageSection[PRIORITY_STR] = "20";
            packageSection[STAR_STR] = FALSE_STR;
            packageSection[HIDE_STR] = FALSE_STR;
            packageSection[USE_BOOT_PACKAGE_STR] = TRUE_STR;
            packageSection[USE_EXIT_PACKAGE_STR] = TRUE_STR;
            packageSection[USE_QUICK_LAUNCH_STR] = FALSE_STR;
            packageSection["custom_name"] = "";
            packageSection["custom_version"] = "";
            packagesNeedsUpdate = true;

            const std::string assignedName =
                packageHeader.title.empty() ? packageName : packageHeader.title;
            std::string entry;
            entry.reserve(128);
            entry = "0020:";
            entry += assignedName;
            entry += ":";
            entry += packageHeader.version;
            entry += ":";
            entry += packageName;
            packageSet.emplace(std::move(entry));
          } else {
            const std::string hide =
                (packageIt->second.find(HIDE_STR) != packageIt->second.end())
                    ? packageIt->second[HIDE_STR]
                    : FALSE_STR;
            if (hide == TRUE_STR)
              drawHiddenTab = true;

            if (inHiddenMode.load(std::memory_order_acquire) ==
                (hide == TRUE_STR)) {
              PackageHeader packageHeader = getPackageHeaderFromIni(
                  PACKAGE_PATH + packageName + "/" + PACKAGE_FILENAME);
              if (cleanVersionLabels) {
                packageHeader.version =
                    cleanVersionLabel(packageHeader.version);
                removeQuotes(packageHeader.version);
              }

              const std::string priority =
                  (packageIt->second.find(PRIORITY_STR) !=
                   packageIt->second.end())
                      ? formatPriorityString(packageIt->second[PRIORITY_STR])
                      : "0020";
              const std::string starred =
                  (packageIt->second.find(STAR_STR) != packageIt->second.end())
                      ? packageIt->second[STAR_STR]
                      : FALSE_STR;
              const std::string customName =
                  getValueOrDefault(packageIt->second, "custom_name", "");
              const std::string customVersion =
                  getValueOrDefault(packageIt->second, "custom_version", "");

              const std::string assignedName =
                  !customName.empty()
                      ? customName
                      : (packageHeader.title.empty() ? packageName
                                                     : packageHeader.title);
              const std::string assignedVersion = !customVersion.empty()
                                                      ? customVersion
                                                      : packageHeader.version;

              std::string entry;
              entry.reserve(128);
              if (starred == TRUE_STR) {
                entry = "-1:";
              }
              entry += priority;
              entry += ":";
              entry += assignedName;
              entry += ":";
              entry += assignedVersion;
              entry += ":";
              entry += packageName;
              packageSet.emplace(std::move(entry));
            }
          }
        }

        if (packagesNeedsUpdate) {
          saveIniFileData(PACKAGES_INI_FILEPATH, packagesIniData);
        }
      } // packagesIniData freed here

      std::string packageName, packageVersion, newPackageName;

      // Helper to build return name (defined before listItem)
      auto buildReturnName = [](bool isStarred, const std::string &pkgName,
                                const std::string &displayName) -> std::string {
        std::string name;
        if (!isStarred) {
          name.reserve(STAR_SYMBOL.size() + 2 + displayName.size() + 1 +
                       pkgName.size());
          name = STAR_SYMBOL;
          name += "  ";
          name += displayName;
        } else {
          name = displayName;
        }
        name += "?";
        name += pkgName;
        return name;
      };

      bool firstItem = true;
      for (const auto &taintedPackageName : packageSet) {
        if (firstItem) {
          addHeader(list, (!inHiddenMode.load(std::memory_order_acquire)
                               ? PACKAGES
                               : HIDDEN_PACKAGES) +
                              " " + DIVIDER_SYMBOL + " \uE0E3 " + SETTINGS +
                              " " + DIVIDER_SYMBOL + " \uE0E2 " + SETTINGS);
          firstItem = false;
        }

        packageName.clear();
        packageVersion.clear();
        newPackageName.clear();
        const bool packageStarred =
            (taintedPackageName.compare(0, 3, "-1:") == 0);
        const std::string &tempPackageName =
            packageStarred ? taintedPackageName : taintedPackageName;
        const size_t offset = packageStarred ? 3 : 0;

        // Parse from the end more efficiently
        size_t pos = tempPackageName.size();
        size_t count = 0;
        size_t positions[3];

        while (pos > offset && count < 3) {
          pos = tempPackageName.rfind(':', pos - 1);
          if (pos == std::string::npos || pos < offset)
            break;
          positions[count++] = pos;
        }

        if (count == 3) {
          packageName = tempPackageName.substr(positions[0] + 1);
          packageVersion = tempPackageName.substr(
              positions[1] + 1, positions[0] - positions[1] - 1);
          newPackageName = tempPackageName.substr(
              positions[2] + 1, positions[1] - positions[2] - 1);
        }

        const std::string packageFilePath = PACKAGE_PATH + packageName + "/";
        if (!isFileOrDirectory(packageFilePath))
          continue;

        const bool newStarred = !packageStarred;

        std::string displayName;
        if (packageStarred) {
          displayName.reserve(STAR_SYMBOL.size() + 2 + newPackageName.size() +
                              1 + packageName.size());
          displayName = STAR_SYMBOL;
          displayName += "  ";
          displayName += newPackageName;
        } else {
          displayName = newPackageName;
        }
        displayName += "?";
        displayName += packageName;

        tsl::elm::ListItem *listItem = new tsl::elm::ListItem(displayName, "");
        listItem->enableShortHoldKey();
        listItem->enableLongHoldKey();

        if (!hidePackageVersions) {
          listItem->setValue(packageVersion, true);
          listItem->setValueColor(usePackageVersions
                                      ? tsl::ultPackageVersionTextColor
                                      : tsl::packageVersionTextColor);
        }
        listItem->setTextColor(usePackageTitles ? tsl::ultPackageTextColor
                                                : tsl::packageTextColor);
        listItem->disableClickAnimation();

        listItem->setClickListener([listItem, packageFilePath, newStarred,
                                    packageName, newPackageName, packageVersion,
                                    packageStarred, buildReturnName](s64 keys) {
          if (runningInterpreter.load(acquire))
            return false;

          if (simulatedMenu.load(std::memory_order_acquire)) {
            keys |= SYSTEM_SETTINGS_KEY;
          }

          // Check for single key press (no other keys)
          const s64 cleanKeys = keys & ALL_KEYS_MASK;

          if (keys & KEY_A && cleanKeys == KEY_A) {
            inMainMenu.store(false, std::memory_order_release);

            // Check for boot package
            if (isFile(packageFilePath + BOOT_PACKAGE_FILENAME)) {
              bool useBootPackage = true;
              const auto packagesIniData =
                  getParsedDataFromIniFile(PACKAGES_INI_FILEPATH);
              auto sectionIt = packagesIniData.find(packageName);

              if (sectionIt != packagesIniData.end()) {
                auto bootIt = sectionIt->second.find(USE_BOOT_PACKAGE_STR);
                useBootPackage = (bootIt == sectionIt->second.end()) ||
                                 (bootIt->second != FALSE_STR);

                if (!selectedPackage.empty()) {
                  auto quickIt = sectionIt->second.find(USE_QUICK_LAUNCH_STR);
                  useBootPackage =
                      useBootPackage && ((quickIt == sectionIt->second.end()) ||
                                         (quickIt->second != TRUE_STR));
                }
              }

              if (useBootPackage) {
                auto bootCommands = loadSpecificSectionFromIni(
                    packageFilePath + BOOT_PACKAGE_FILENAME, "boot");
                if (!bootCommands.empty()) {
                  const bool resetCommandSuccess =
                      !commandSuccess.load(std::memory_order_acquire);
                  interpretAndExecuteCommands(std::move(bootCommands),
                                              packageFilePath, "boot");
                  resetPercentages();
                  if (resetCommandSuccess)
                    commandSuccess.store(false, release);
                }
              }
            }

            packageRootLayerTitle = newPackageName;
            packageRootLayerName = packageName;
            packageRootLayerVersion = packageVersion;

            returnJumpItemName =
                buildReturnName(newStarred, packageName, newPackageName);
            returnJumpItemValue = hidePackageVersions ? "" : packageVersion;

            tsl::clearGlyphCacheNow.store(true, release);
            tsl::swapTo<PackageMenu>(SwapDepth(2), packageFilePath, "");
            return true;
          }

          if (keys & STAR_KEY && cleanKeys == STAR_KEY) {
            if (!packageName.empty()) {
              setIniFileValue(PACKAGES_INI_FILEPATH, packageName, STAR_STR,
                              newStarred ? TRUE_STR : FALSE_STR);
            }

            skipJumpReset.store(true, release);
            jumpItemName =
                buildReturnName(!newStarred, packageName, newPackageName);
            jumpItemValue = hidePackageVersions ? "" : packageVersion;
            jumpItemExactMatch.store(true, release);

            wasInHiddenMode = inHiddenMode.load(std::memory_order_acquire);
            if (wasInHiddenMode) {
              inMainMenu.store(false, std::memory_order_release);
              reloadMenu2 = true;
            }

            refreshPage.store(true, release);
            triggerRumbleClick.store(true, std::memory_order_release);
            triggerMoveSound.store(true, std::memory_order_release);
            return true;
          }

          if (keys & SETTINGS_KEY && cleanKeys == SETTINGS_KEY) {
            if (!inHiddenMode.load(std::memory_order_acquire)) {
              lastMenu = "";
              inMainMenu.store(false, std::memory_order_release);
            } else {
              lastMenu = "hiddenMenuMode";
              inHiddenMode.store(false, std::memory_order_release);
            }

            returnJumpItemName =
                buildReturnName(newStarred, packageName, newPackageName);
            returnJumpItemValue = hidePackageVersions ? "" : packageVersion;
            jumpItemName = jumpItemValue = "";

            tsl::shiftItemFocus(listItem);
            tsl::changeTo<SettingsMenu>(packageName, PACKAGE_STR,
                                        newPackageName, packageVersion);
            triggerRumbleClick.store(true, std::memory_order_release);
            triggerSettingsSound.store(true, std::memory_order_release);
            return true;
          }

          if (keys & SYSTEM_SETTINGS_KEY && cleanKeys == SYSTEM_SETTINGS_KEY) {
            returnJumpItemName =
                buildReturnName(newStarred, packageName, newPackageName);
            returnJumpItemValue = hidePackageVersions ? "" : packageVersion;
            return true;
          }

          return false;
        });

        list->addItem(listItem);
      }

      packageSet.clear();

      if (drawHiddenTab && !inHiddenMode.load(std::memory_order_acquire) &&
          !hideHidden) {
        tsl::elm::ListItem *listItem =
            new tsl::elm::ListItem(HIDDEN, DROPDOWN_SYMBOL);
        listItem->setClickListener([listItem](uint64_t keys) {
          if (runningInterpreter.load(acquire))
            return false;
          if (simulatedMenu.load(std::memory_order_acquire)) {
            keys |= SYSTEM_SETTINGS_KEY;
          }

          if ((keys & KEY_A && !(keys & ~KEY_A & ALL_KEYS_MASK))) {
            inMainMenu.store(false, std::memory_order_release);
            inHiddenMode.store(true, std::memory_order_release);
            tsl::shiftItemFocus(listItem);
            tsl::changeTo<MainMenu>(PACKAGES_STR);
            return true;
          } else if (keys & SYSTEM_SETTINGS_KEY &&
                     !(keys & ~SYSTEM_SETTINGS_KEY & ALL_KEYS_MASK)) {
            returnJumpItemName = "";
            returnJumpItemValue = DROPDOWN_SYMBOL;
            return true;
          }
          return false;
        });
        list->addItem(listItem);
      }
    }

    if (!inHiddenMode.load(std::memory_order_acquire)) {
      // Display game info at the top
      // if (!hideUserGuide && dropdownSection.empty()) {
      //     DmntCheatProcessMetadata metadata;
      //     if (R_SUCCEEDED(dmntchtGetCheatProcessMetadata(&metadata))) {
      //         this->cheatList = list;

      //         if (m_notesPath.empty()) {
      //             std::stringstream ss;
      //             ss << "sdmc:/switch/breeze/cheats/" << std::setfill('0') <<
      //             std::setw(16) << std::uppercase << std::hex <<
      //             metadata.title_id << "/notes.txt"; m_notesPath = ss.str();
      //         }

      //         if (!m_notesLoaded && !m_notesPath.empty()) {
      //             std::string fontSizeStr =
      //             ult::parseValueFromIniSection(m_notesPath, "Breeze",
      //             "font_size"); if (!fontSizeStr.empty()) {
      //                 m_cheatFontSize =
      //                 std::clamp(static_cast<int>(ult::stoi(fontSizeStr)),
      //                 10, 30);
      //             }
      //             std::string showNotesStr =
      //             ult::parseValueFromIniSection(m_notesPath, "Breeze",
      //             "show_notes"); if (!showNotesStr.empty()) {
      //                 ult::showCheatNotes = (showNotesStr == "true");
      //             }
      //             m_notesLoaded = true;
      //         }

      //         CheatUtils::EnsureMetadata();
      //         std::string tidStr = CheatUtils::GetTitleIdString();
      //         std::string bidStr = CheatUtils::GetBuildIdString();

      //         std::string titleStr = "";
      //         std::string versionStr = "";
      //         static NsApplicationControlData appControlData;
      //         size_t appControlDataSize = 0;
      //         NacpLanguageEntry *languageEntry = nullptr;
      //         if
      //         (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage,
      //         metadata.title_id & 0xFFFFFFFFFFFFFFF0, &appControlData,
      //         sizeof(NsApplicationControlData), &appControlDataSize))) {
      //             if
      //             (R_SUCCEEDED(nsGetApplicationDesiredLanguage(&appControlData.nacp,
      //             &languageEntry)) && languageEntry) {
      //                 titleStr = languageEntry->name;
      //             }
      //             versionStr = appControlData.nacp.display_version;
      //         }

      //         if (titleStr.empty()) {
      //             titleStr = tidStr;
      //             std::string titleFilePath = "sdmc:/switch/breeze/cheats/" +
      //             tidStr + "/title.txt"; if (ult::isFile(titleFilePath)) {
      //                 std::ifstream tfile(titleFilePath);
      //                 if (std::getline(tfile, titleStr)) ult::trim(titleStr);
      //             }
      //         }

      //         // Add "Current Game" category header
      //         auto* categoryHeader = new tsl::elm::CategoryHeader("Current
      //         Game"); list->addItem(categoryHeader);

      //         auto* titleItem = new tsl::elm::ListItem(titleStr +
      //         (versionStr.empty() ? "" : " v" + versionStr));
      //         titleItem->setUseWrapping(true);
      //         titleItem->setFontSize(m_cheatFontSize);
      //         list->addItem(titleItem);

      //         auto* tidItem = new tsl::elm::ListItem("TID: " + tidStr);
      //         tidItem->setUseWrapping(true);
      //         tidItem->setFontSize(m_cheatFontSize);
      //         list->addItem(tidItem);

      //         auto* bidItem = new tsl::elm::ListItem("BID: " + bidStr);
      //         bidItem->setUseWrapping(true);
      //         bidItem->setFontSize(m_cheatFontSize);
      //         list->addItem(bidItem);
      //     }
      // }

      std::string pageLeftName, pageRightName, pathPattern, pathPatternOn,
          pathPatternOff;
      bool usingPages = false;

      const PackageHeader packageHeader = getPackageHeaderFromIni(PACKAGE_PATH);
      noClickableItems = drawCommandsMenu(
          list, packageIniPath, packageConfigIniPath, packageHeader, "",
          pageLeftName, pageRightName, PACKAGE_PATH, LEFT_STR, "package.ini",
          this->dropdownSection, 0, pathPattern, pathPatternOn, pathPatternOff,
          usingPages, false);

      if (!hideUserGuide && dropdownSection.empty()) {
        addHelpInfo(list);
      }
    }

    return noClickableItems;
  }

  /**
   * @brief Handles user input for the main menu overlay.
   *
   * Processes user input and responds accordingly within the main menu overlay.
   * Captures key presses and performs actions based on user interactions.
   *
   * @param keysDown A bitset representing keys that are currently pressed.
   * @param keysHeld A bitset representing keys that are held down.
   * @param touchInput Information about touchscreen input.
   * @param leftJoyStick Information about the left joystick input.
   * @param rightJoyStick Information about the right joystick input.
   * @return `true` if the input was handled within the overlay, `false`
   * otherwise.
   */
  virtual bool handleInput(uint64_t keysDown, uint64_t keysHeld,
                           touchPosition touchInput,
                           JoystickPosition leftJoyStick,
                           JoystickPosition rightJoyStick) override {
    // Keep footer back label in sync even when returning to an existing
    // MainMenu instance (no createUI rebuild).
    const bool shouldShowRestartLabel =
        takeOverOvlmenu && inMainMenu.load(std::memory_order_acquire) &&
        !inHiddenMode.load(std::memory_order_acquire) &&
        dropdownSection.empty();
    setFooterBackLabel(shouldShowRestartLabel);

    if (menuMode == SEARCH_MANAGER_MENU_MODE &&
        g_searchWorkerRunning.load(std::memory_order_acquire)) {
      focusSearchProgressItem();
      if (keysHeld & KEY_MINUS) {
        g_searchPauseRequested.store(true, std::memory_order_release);
      }
      if (g_searchWorkerPaused.load(std::memory_order_acquire)) {
        if (keysDown & KEY_A) {
          g_searchPauseRequested.store(false, std::memory_order_release);
          return true;
        }
        if (keysDown & KEY_B) {
          g_searchAbortRequested.store(true, std::memory_order_release);
          g_searchPauseRequested.store(false, std::memory_order_release);
          return true;
        }
      }
      return true;
    }
    if (menuMode == SEARCH_MANAGER_MENU_MODE && (keysDown & KEY_MINUS)) {
      if (!g_searchContinueSourcePath.empty()) {
        tsl::changeTo<CandidateEntriesMenu>(g_searchContinueSourcePath, 0);
      } else if (tsl::notification) {
        tsl::notification->show("No candidate file selected");
      }
      return true;
    }

    if (menuMode == SEARCH_MANAGER_MENU_MODE && m_setupSearchItem) {
      const std::string summary = SearchConditionSummary(g_searchCondition);
      if (m_setupSearchItem->getNote() != summary) {
        m_setupSearchItem->setNote(summary);
      }
      if (m_startSearchItem) {
        const std::string startNote = g_startSearchOutputName.empty()
                                          ? std::string("output")
                                          : DisplayStartOutputStem(g_startSearchOutputName);
        if (m_startSearchItem->getNote() != startNote) {
          m_startSearchItem->setNote(startNote);
        }
      }
      if (m_continueSearchItem) {
        const std::string continueNote =
            ContinueSearchNoteFromPath(g_searchContinueSourcePath);
        if (m_continueSearchItem->getNote() != continueNote) {
          m_continueSearchItem->setNote(continueNote);
        }
      }
      if (m_lastBufferItem) {
        const std::string bufferNote = LastSearchBufferNote();
        if (m_lastBufferItem->getNote() != bufferNote) {
          m_lastBufferItem->setNote(bufferNote);
        }
      }
      if (m_lastTimeItem) {
        const std::string title = SearchTimeItemTitle();
        if (m_lastTimeItem->getText() != title) {
          m_lastTimeItem->setText(title);
        }
        const std::string timeNote = LastSearchTimeNote();
        if (m_lastTimeItem->getNote() != timeNote) {
          m_lastTimeItem->setNote(timeNote);
        }
      }
    }

    if ((keysHeld & KEY_ZL) && menuMode == OVERLAYS_STR) {
      if (keysDown & KEY_R) {
        m_cheatFontSize = std::min(static_cast<int>(m_cheatFontSize) + 1, 30);
        if (cheatList) {
          struct ListProxy : public tsl::elm::List {
            using tsl::elm::List::m_items;
          };
          for (auto *item : static_cast<ListProxy *>(cheatList)->m_items) {
            if (item && item->m_isItem) {
              static_cast<tsl::elm::ListItem *>(item)->setFontSize(
                  m_cheatFontSize);
            }
          }
          cheatList->layout(cheatList->getX(), cheatList->getY(),
                            cheatList->getWidth(), cheatList->getHeight());
          if (!m_notesPath.empty())
            ult::setIniFileValue(m_notesPath, "Breeze", "font_size",
                                 std::to_string(m_cheatFontSize));
        }
        return true;
      }
      if (keysDown & KEY_L) {
        m_cheatFontSize = std::max(static_cast<int>(m_cheatFontSize) - 1, 10);
        if (cheatList) {
          struct ListProxy : public tsl::elm::List {
            using tsl::elm::List::m_items;
          };
          for (auto *item : static_cast<ListProxy *>(cheatList)->m_items) {
            if (item && item->m_isItem) {
              static_cast<tsl::elm::ListItem *>(item)->setFontSize(
                  m_cheatFontSize);
            }
          }
          cheatList->layout(cheatList->getX(), cheatList->getY(),
                            cheatList->getWidth(), cheatList->getHeight());
          if (!m_notesPath.empty())
            ult::setIniFileValue(m_notesPath, "Breeze", "font_size",
                                 std::to_string(m_cheatFontSize));
        }
        return true;
      }
    }

    // Y button toggles notes visibility
    if (keysDown & KEY_Y) {
      ult::showCheatNotes = !ult::showCheatNotes;
      if (!m_notesPath.empty())
        ult::setIniFileValue(m_notesPath, "Breeze", "show_notes",
                             ult::showCheatNotes ? "true" : "false");

      if (cheatList && menuMode == OVERLAYS_STR) {
        // Find currently focused item to restore focus and scroll after refresh
        struct ListProxy : public tsl::elm::List {
          using tsl::elm::List::m_items;
        };
        for (auto *item : static_cast<ListProxy *>(cheatList)->m_items) {
          if (item && item->m_isItem && item->hasFocus()) {
            auto *listItem = static_cast<tsl::elm::ListItem *>(item);
            jumpItemName = listItem->getText();
            jumpItemValue = listItem->getValue();
            jumpItemExactMatch.store(true, release);
            skipJumpReset.store(true, release);
            break;
          }
        }
        refreshPage.store(true, release);
      }
      return true;
    }

    // X button switches to CheatMenu (Settings for cheats)
    if (keysDown & KEY_X) {
      if (menuMode == OVERLAYS_STR) {
        u32 cheatId = 0;
        std::string cheatName = "";

        if (this->cheatList) {
          struct ListProxy : public tsl::elm::List {
            using tsl::elm::List::m_items;
          };
          for (auto *item :
               static_cast<ListProxy *>(this->cheatList)->m_items) {
            if (item && item->m_isItem && item->hasFocus()) {
              auto *listItem = static_cast<tsl::elm::ListItem *>(item);
              std::string text = listItem->getText();
              // Only cast to CheatToggleItem if it's actually a cheat item
              if (!text.empty() && text != "No cheats found" &&
                  text != "Failed to retrieve cheats") {
                auto *cheatItem =
                    static_cast<CheatUtils::CheatToggleItem *>(listItem);
                if (cheatItem->cheat_id != 0) {
                  cheatId = cheatItem->cheat_id;
                  cheatName = text;
                }
              }
              break;
            }
          }
        }

        tsl::changeTo<CheatMenu>(cheatId, cheatName);
        return true;
      }
    }
    bool isHolding = (lastCommandIsHold &&
                      runningInterpreter.load(std::memory_order_acquire));
    if (isHolding) {
      processHold(
          keysDown, keysHeld, holdStartTick, isHolding,
          [&]() {
            // Execute interpreter commands if needed
            displayPercentage.store(-1, std::memory_order_release);
            // lastCommandMode.clear();
            // lastKeyName.clear();
            lastCommandIsHold = false;
            lastSelectedListItem->setValue(INPROGRESS_SYMBOL);
            // lastSelectedListItemFooter.clear();
            // lastSelectedListItem->enableClickAnimation();
            // lastSelectedListItem->triggerClickAnimation();
            // lastSelectedListItem->disableClickAnimation();
            triggerEnterFeedback();
            executeInterpreterCommands(std::move(storedCommands),
                                       packageIniPath, lastKeyName);
            lastRunningInterpreter.store(true, std::memory_order_release);
          },
          nullptr, true); // true = reset storedCommands
      return true;
    }

    if (ult::launchingOverlay.load(acquire))
      return true;

    const bool isRunningInterp = runningInterpreter.load(acquire);
    const bool isTouching = stillTouching.load(acquire);

    if (isRunningInterp)
      return handleRunningInterpreter(keysDown, keysHeld);

    if (lastRunningInterpreter.exchange(false, std::memory_order_acq_rel)) {
      // tsl::clearGlyphCacheNow.store(true, release);
      isDownloadCommand.store(false, release);

      if (lastSelectedListItem) {
        const bool success = commandSuccess.load(acquire);

        if (lastCommandMode == OPTION_STR || lastCommandMode == SLOT_STR) {
          if (success) {
            if (isFile(packageConfigIniPath)) {
              auto packageConfigData =
                  getParsedDataFromIniFile(packageConfigIniPath);

              if (auto it = packageConfigData.find(lastKeyName);
                  it != packageConfigData.end()) {
                auto &optionSection = it->second;

                // Update footer if valid
                if (auto footerIt = optionSection.find(FOOTER_STR);
                    footerIt != optionSection.end() &&
                    footerIt->second.find(NULL_STR) == std::string::npos) {
                  // Check if footer_highlight exists in the INI
                  auto footerHighlightIt =
                      optionSection.find("footer_highlight");
                  if (footerHighlightIt != optionSection.end()) {
                    // Use the value from the INI
                    bool footerHighlightFromIni =
                        (footerHighlightIt->second == TRUE_STR);
                    lastSelectedListItem->setValue(footerIt->second,
                                                   !footerHighlightFromIni);
                  } else {
                    // footer_highlight not defined in INI, use default
                    lastSelectedListItem->setValue(footerIt->second, false);
                  }
                }
              }

              lastCommandMode.clear();
            } else {
              lastSelectedListItem->setValue(CHECKMARK_SYMBOL);
            }
          } else {
            lastSelectedListItem->setValue(CROSSMARK_SYMBOL);
          }
        } else {
          // Handle toggle or checkmark logic
          if (nextToggleState.empty()) {
            lastSelectedListItem->setValue(success ? CHECKMARK_SYMBOL
                                                   : CROSSMARK_SYMBOL);
          } else {
            // Determine the final state to save
            const std::string finalState =
                success ? nextToggleState
                        : (nextToggleState == CAPITAL_ON_STR ? CAPITAL_OFF_STR
                                                             : CAPITAL_ON_STR);

            // Update displayed value
            lastSelectedListItem->setValue(finalState);

            // Update toggle state
            static_cast<tsl::elm::ToggleListItem *>(lastSelectedListItem)
                ->setState(finalState == CAPITAL_ON_STR);

            // Save to config file
            setIniFileValue(packageConfigIniPath, lastKeyName, FOOTER_STR,
                            finalState);

            lastKeyName.clear();
            nextToggleState.clear();
          }
        }

        lastSelectedListItem->enableClickAnimation();
        lastSelectedListItem = nullptr;
        lastFooterHighlight = lastFooterHighlightDefined = false;
      }

      closeInterpreterThread();
      resetPercentages();

      if (!commandSuccess.load()) {
        triggerRumbleDoubleClick.store(true, std::memory_order_release);
      }
      if (!limitedMemory && useSoundEffects) {
        reloadSoundCacheNow.store(true, std::memory_order_release);
        // ult::Audio::initialize();
      }
      // lastRunningInterpreter.store(false, std::memory_order_release);
      return true;
    }

    if (ult::refreshWallpaperNow.exchange(false, std::memory_order_acq_rel)) {
      closeInterpreterThread();
      ult::reloadWallpaper();

      if (!limitedMemory && useSoundEffects) {
        reloadSoundCacheNow.store(true, std::memory_order_release);
        // ult::Audio::initialize();
      }
    }

    if (goBackAfter.exchange(false, std::memory_order_acq_rel)) {
      disableSound.store(true, std::memory_order_release);
      simulatedBack.store(true, std::memory_order_release);
      return true;
    }

    if (refreshPage.load(acquire) && !isTouching) {
      refreshPage.store(false, release);
      // tsl::pop();
      tsl::swapTo<MainMenu>(hiddenMenuMode, dropdownSection);
      if (wasInHiddenMode) {
        // std::lock_guard<std::mutex> lock(jumpItemMutex);
        skipJumpReset.store(true, release);
        jumpItemName = HIDDEN;
        jumpItemValue = DROPDOWN_SYMBOL;
        jumpItemExactMatch.store(true, release);
        // g_overlayFilename = "";
        wasInHiddenMode = false;
      }
      return true;
    }

    // Periodically check if the game has changed
    m_updateCounter++;
    if (m_updateCounter >= CHECK_INTERVAL) {
      m_updateCounter = 0;

      // Check if a game is running
      bool hasCheatProcess = false;
      if (R_SUCCEEDED(dmntchtHasCheatProcess(&hasCheatProcess))) {
        // If no game is running and we had a game before, close overlay
        if (!hasCheatProcess) {
          m_noGameRunning = true;
          if (m_lastTitleId != 0 || m_lastBuildId != 0) {
            // Game was running but now stopped - close overlay
            tsl::Overlay::get()->close();
            return true;
          }
        } else {
          // if no game was running before exit overlay
          if (m_noGameRunning) {
            tsl::Overlay::get()->close();
            return true;
          }
          // Get current game metadata
          DmntCheatProcessMetadata currentMetadata;
          if (R_SUCCEEDED(dmntchtGetCheatProcessMetadata(&currentMetadata))) {
            u64 currentTitleId = currentMetadata.title_id;
            u64 currentBuildId = *((u64 *)currentMetadata.main_nso_build_id);

            // Initialize on first run
            if (m_lastTitleId == 0 && m_lastBuildId == 0) {
              m_lastTitleId = currentTitleId;
              m_lastBuildId = currentBuildId;
            }
            // Check if game has changed
            else if (currentTitleId != m_lastTitleId ||
                     currentBuildId != m_lastBuildId) {
              // Game has changed - close overlay to prevent stale display
              tsl::Overlay::get()->close();
              return true;
            }
          }
        }
      }
    }

    // Common condition for back key handling
    const bool backKeyPressed =
        !isTouching &&
        ((((keysDown & KEY_B)) && !(keysHeld & ~KEY_B & ALL_KEYS_MASK)));

    if (inMainMenu.load(acquire) && menuMode == OVERLAYS_STR &&
        !g_cheatFolderNameStack.empty() && backKeyPressed) {
      jumpItemName = "\uE132 " + g_cheatFolderNameStack.back();
      jumpItemExactMatch.store(true, release);
      skipJumpReset.store(true, release);
      g_cheatFolderNameStack.pop_back();
      g_cheatFolderIndexStack.pop_back();
      refreshPage.store(true, release);
      triggerExitSound.store(true, release);
      return true;
    }

    if (!dropdownSection.empty() && !returningToMain) {
      simulatedNextPage.exchange(false, std::memory_order_acq_rel);
      simulatedMenu.exchange(false, std::memory_order_acq_rel);

      if (backKeyPressed) {
        allowSlide.exchange(false, std::memory_order_acq_rel);
        unlockedSlide.exchange(false, std::memory_order_acq_rel);
        returningToMain = true;
        tsl::goBack();
        return true;
      }
    }

    if (inMainMenu.load(acquire) &&
        !inHiddenMode.load(std::memory_order_acquire) &&
        dropdownSection.empty()) {
      if (triggerMenuReload || triggerMenuReload2) {
        triggerMenuReload = triggerMenuReload2 = false;
        disableSound.store(true, std::memory_order_release);
        ult::launchingOverlay.store(true, std::memory_order_release);
        // if (menuMode == PACKAGES_STR)
        //     setIniFileValue(ULTRAHAND_CONFIG_INI_PATH,
        //     ULTRAHAND_PROJECT_NAME, "to_packages", FALSE_STR);
        //
        // setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
        // IN_OVERLAY_STR, TRUE_STR);

        // Load INI data once and modify in memory
        {
          auto iniData = getParsedDataFromIniFile(ULTRAHAND_CONFIG_INI_PATH);
          auto &ultrahandSection = iniData[ULTRAHAND_PROJECT_NAME];

          // Make all changes in memory
          if (menuMode == PACKAGES_STR) {
            ultrahandSection["to_packages"] = FALSE_STR;
          }
          ultrahandSection[IN_OVERLAY_STR] = TRUE_STR;

          // Write back once
          saveIniFileData(ULTRAHAND_CONFIG_INI_PATH, iniData);
        }

        tsl::setNextOverlay(OVERLAY_PATH + "ovlmenu.ovl",
                            "--skipCombo --comboReturn");
        tsl::Overlay::get()->close();
      }

      if (!freshSpawn && !returningToMain && !returningToHiddenMain) {
        // if (simulatedNextPage.exchange(false, acq_rel)) {
        //     const bool toPackages = (!usePageSwap && menuMode !=
        //     PACKAGES_STR) || (usePageSwap && menuMode != OVERLAYS_STR);
        //     keysDown |= toPackages ? (usePageSwap ? KEY_DLEFT : KEY_DRIGHT) :
        //     (usePageSwap ? KEY_DRIGHT : KEY_DLEFT);
        // }
        auto pageIndexFromMode = [&](const std::string &mode) -> int {
          if (mode == SEARCH_MANAGER_MENU_MODE)
            return 0;
          if (mode == OVERLAYS_STR)
            return 1;
          if (mode == PACKAGES_STR)
            return 2;
          return 1; // default to center (cheats)
        };
        auto modeFromPageIndex = [&](int index) -> std::string {
          switch (index) {
          case 0:
            return SEARCH_MANAGER_MENU_MODE;
          case 2:
            return PACKAGES_STR;
          default:
            return OVERLAYS_STR;
          }
        };

        const int currentPageIndex = pageIndexFromMode(menuMode);

        bool wasSimulated = false;
        {
          // std::lock_guard<std::mutex> lock(ult::simulatedNextPageMutex);
          if (simulatedNextPage.exchange(false, std::memory_order_acq_rel)) {
            if (currentPageIndex < 2) {
              keysDown |= KEY_DRIGHT;
            } else {
              keysDown |= KEY_DLEFT;
            }
            wasSimulated = true;
          }
        }

        // Cache slide conditions

        const bool onTrack = onTrackBar.load(acquire);
        const bool slideAllowed = allowSlide.load(acquire);
        const bool slideUnlocked = unlockedSlide.load(acquire);
        const bool slideCondition =
            ((!slideAllowed && !slideUnlocked && onTrack) ||
             (onTrack && keysHeld & KEY_R)) ||
            !onTrack;

        // Helper lambda to reset navigation state
        auto resetNavState = [&]() {
          {
            // std::lock_guard<std::mutex> lock(jumpItemMutex);
            // g_overlayFilename = "";
            jumpItemName = "";
            jumpItemValue = "";
            jumpItemExactMatch.store(true, release);
          }
          // if (allowSlide.load(acquire))
          allowSlide.store(false, release);
          // if (unlockedSlide.load(acquire))
          unlockedSlide.store(false, release);
        };

        // const bool switchToPackages = (!usePageSwap && menuMode !=
        // PACKAGES_STR) || (usePageSwap && menuMode != OVERLAYS_STR);
        if (currentPageIndex < 2 && !isTouching && slideCondition &&
            (keysDown & KEY_RIGHT) &&
            (!onTrack ? !(keysHeld & ~KEY_RIGHT & ALL_KEYS_MASK)
                      : !(keysHeld & ~KEY_RIGHT & ~KEY_R & ALL_KEYS_MASK))) {
          // bool safeToSwap = false;
          {
            std::lock_guard<std::mutex> lock(tsl::elm::s_safeToSwapMutex);
            // safeToSwap = tsl::elm::s_safeToSwap.load(acquire);

            // if (simulatedNextPage.load(acquire))
            //     simulatedNextPage.store(false, release);
            if (tsl::elm::s_safeToSwap.load(acquire)) {
              // tsl::elm::s_safeToSwap.store(false, release);
              currentMenu = modeFromPageIndex(currentPageIndex + 1);
              // tsl::pop();
              tsl::swapTo<MainMenu>();
              resetNavState();
              // triggerRumbleClick.store(true, std::memory_order_release);
              // triggerNavigationSound.store(true, std::memory_order_release);
              if (!wasSimulated)
                triggerNavigationFeedback();
              else
                triggerRumbleClick.store(true, release);
            }
            return true;
          }
        }

        // const bool switchToOverlays = (!usePageSwap && menuMode !=
        // OVERLAYS_STR) || (usePageSwap && menuMode != PACKAGES_STR);
        if (currentPageIndex > 0 && !isTouching && slideCondition &&
            (keysDown & KEY_LEFT) &&
            (!onTrack ? !(keysHeld & ~KEY_LEFT & ALL_KEYS_MASK)
                      : !(keysHeld & ~KEY_LEFT & ~KEY_R & ALL_KEYS_MASK))) {
          // bool safeToSwap = false;
          {
            std::lock_guard<std::mutex> lock(tsl::elm::s_safeToSwapMutex);
            // safeToSwap = tsl::elm::s_safeToSwap.load(acquire);

            // if (simulatedNextPage.load(acquire))
            //     simulatedNextPage.store(false, release);
            if (tsl::elm::s_safeToSwap.load(acquire)) {
              // tsl::elm::s_safeToSwap.store(false, release);
              currentMenu = modeFromPageIndex(currentPageIndex - 1);
              // tsl::pop();
              tsl::swapTo<MainMenu>();
              resetNavState();
              // triggerRumbleClick.store(true, std::memory_order_release);
              // triggerNavigationSound.store(true, std::memory_order_release);
              if (!wasSimulated)
                triggerNavigationFeedback();
              else
                triggerRumbleClick.store(true, release);
            }
            return true;
          }
        }

        if (backKeyPressed) {
          allowSlide.exchange(false, std::memory_order_acq_rel);
          unlockedSlide.exchange(false, std::memory_order_acq_rel);
          if (tsl::notification && tsl::notification->isActive()) {
            tsl::Overlay::get()->closeAfter();
            tsl::Overlay::get()->hide(true);
          } else {
            ult::launchingOverlay.store(true, std::memory_order_release);
            exitingUltrahand.store(true, release);

            tsl::setNextOverlay(OVERLAY_PATH + "ovlmenu.ovl");
            tsl::Overlay::get()->close();
          }

          return true;
        }

        if (simulatedMenu.load(std::memory_order_acquire)) {
          keysDown |= SYSTEM_SETTINGS_KEY;
        }

        if (!isTouching &&
            (((keysDown & SYSTEM_SETTINGS_KEY &&
               !(keysHeld & ~SYSTEM_SETTINGS_KEY & ALL_KEYS_MASK))))) {
          inMainMenu.store(false, std::memory_order_release);
          skipJumpReset.store(false, std::memory_order_release);
          tsl::changeTo<UltrahandSettingsMenu>();
          triggerRumbleClick.store(true, std::memory_order_release);
          triggerSettingsSound.store(true, std::memory_order_release);
          return true;
        }
      }
    }

    if (!inMainMenu.load(acquire) &&
        inHiddenMode.load(std::memory_order_acquire) &&
        !returningToHiddenMain && !returningToMain) {
      simulatedNextPage.exchange(false, std::memory_order_acq_rel);
      // simulatedMenu.exchange(false, std::memory_order_acq_rel);

      if (backKeyPressed) {
        // Check if we're in hidden mode with no underlying menu to go back to
        if (hiddenMenuMode == OVERLAYS_STR || hiddenMenuMode == PACKAGES_STR) {
          inMainMenu.store(true, std::memory_order_release);
          inHiddenMode.store(false, std::memory_order_release);
          hiddenMenuMode = "";
          // setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
          // IN_HIDDEN_OVERLAY_STR, "");
          // setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
          // IN_HIDDEN_PACKAGE_STR, "");
          {
            // Load INI data once and modify in memory
            auto iniData = getParsedDataFromIniFile(ULTRAHAND_CONFIG_INI_PATH);
            auto &ultrahandSection = iniData[ULTRAHAND_PROJECT_NAME];

            // Clear both values in memory
            ultrahandSection[IN_HIDDEN_OVERLAY_STR] = "";
            ultrahandSection[IN_HIDDEN_PACKAGE_STR] = "";

            // Write back once
            saveIniFileData(ULTRAHAND_CONFIG_INI_PATH, iniData);
          }

          {
            // std::lock_guard<std::mutex> lock(jumpItemMutex);
            skipJumpReset.store(true, release);
            jumpItemName = HIDDEN;
            jumpItemValue = DROPDOWN_SYMBOL;
            jumpItemExactMatch.store(true, release);
            // g_overlayFilename = "";
          }
          returningToMain = true;
          // tsl::pop();
          tsl::swapTo<MainMenu>();
          return true;
        }

        returningToMain = true;
        inHiddenMode.exchange(false, std::memory_order_acq_rel);

        if (reloadMenu2) {
          // tsl::pop();
          tsl::swapTo<MainMenu>();
          reloadMenu2 = false;
          return true;
        }

        allowSlide.exchange(false, std::memory_order_acq_rel);
        unlockedSlide.exchange(false, std::memory_order_acq_rel);
        tsl::goBack();
        return true;
      }

      if (simulatedMenu.load(std::memory_order_acquire)) {
        keysDown |= SYSTEM_SETTINGS_KEY;
      }

      if (!isTouching &&
          (((keysDown & SYSTEM_SETTINGS_KEY &&
             !(keysHeld & ~SYSTEM_SETTINGS_KEY & ALL_KEYS_MASK))))) {
        inMainMenu.store(false, std::memory_order_release);
        skipJumpReset.store(false, std::memory_order_release);
        lastMenu = "hiddenMenuMode"; // ADD THIS LINE
        tsl::changeTo<UltrahandSettingsMenu>();
        triggerRumbleClick.store(true, std::memory_order_release);
        triggerSettingsSound.store(true, std::memory_order_release);
        return true;
      }
    }

    if (freshSpawn && !(keysDown & KEY_B))
      freshSpawn = false;

    if (returningToMain && !(keysDown & KEY_B)) {
      returningToMain = false;
      inMainMenu.store(true, std::memory_order_release);
      const bool showRestartInFooter =
          takeOverOvlmenu && !inHiddenMode.load(std::memory_order_acquire) &&
          dropdownSection.empty();
      setFooterBackLabel(showRestartInFooter);
    }
    if (returningToHiddenMain && !(keysDown & KEY_B)) {
      returningToHiddenMain = false;
      inHiddenMode.store(true, std::memory_order_release);
      setFooterBackLabel(false);
    }

    if (triggerExit.exchange(false, std::memory_order_acq_rel)) {
      // triggerExit.store(false, release);
      ult::launchingOverlay.store(true, std::memory_order_release);
      tsl::setNextOverlay(OVERLAY_PATH + "ovlmenu.ovl");
      tsl::Overlay::get()->close();
    }

    // svcSleepThread(10'000'000);
    return false;
  }

  virtual void update() override {
    if (menuMode == SEARCH_MANAGER_MENU_MODE &&
        (g_searchWorkerRunning.load(std::memory_order_acquire) ||
         g_searchInProgress || g_searchQueuedAction != SEARCH_QUEUED_NONE)) {
      focusSearchProgressItem();
    }

    if (menuMode == SEARCH_MANAGER_MENU_MODE && m_setupSearchItem) {
      const std::string summary = SearchConditionSummary(g_searchCondition);
      if (m_setupSearchItem->getNote() != summary) {
        m_setupSearchItem->setNote(summary);
      }
      if (m_startSearchItem) {
        const std::string startNote = g_startSearchOutputName.empty()
                                          ? std::string("output")
                                          : DisplayStartOutputStem(g_startSearchOutputName);
        if (m_startSearchItem->getNote() != startNote) {
          m_startSearchItem->setNote(startNote);
        }
      }
      if (m_continueSearchItem) {
        const std::string continueNote =
            ContinueSearchNoteFromPath(g_searchContinueSourcePath);
        if (m_continueSearchItem->getNote() != continueNote) {
          m_continueSearchItem->setNote(continueNote);
        }
      }
      if (m_lastBufferItem) {
        const std::string bufferNote = LastSearchBufferNote();
        if (m_lastBufferItem->getNote() != bufferNote) {
          m_lastBufferItem->setNote(bufferNote);
        }
      }
      if (m_lastTimeItem) {
        const std::string title = SearchTimeItemTitle();
        if (m_lastTimeItem->getText() != title) {
          m_lastTimeItem->setText(title);
        }
        const std::string timeNote = LastSearchTimeNote();
        if (m_lastTimeItem->getNote() != timeNote) {
          m_lastTimeItem->setNote(timeNote);
        }
      }
    }

    if (menuMode != SEARCH_MANAGER_MENU_MODE) {
      return;
    }
    if (g_searchWorkerDone.load(std::memory_order_acquire)) {
      FinalizeCompletedSearchWorker();
      g_searchWorkerDone.store(false, std::memory_order_release);
      g_searchActiveAction = SEARCH_QUEUED_NONE;
    }
    if (g_searchWorkerRunning.load(std::memory_order_acquire)) {
      return;
    }
    if (g_searchQueuedAction == SEARCH_QUEUED_NONE) {
      return;
    }
    if (g_searchQueuedDelayTicks > 0) {
      --g_searchQueuedDelayTicks;
      return;
    }

    const SearchQueuedAction action = g_searchQueuedAction;
    g_searchQueuedAction = SEARCH_QUEUED_NONE;
    BeginQueuedSearchWorker(action);
  }
};

// Extract the settings initialization logic into a separate method
void initializeSettingsAndDirectories() {
  versionLabel = cleanVersionLabel(APP_VERSION) + " " + DIVIDER_SYMBOL + " " +
                 loaderTitle + " " + cleanVersionLabel(loaderInfo);
  std::string defaultLang = "en";

  // Create necessary directories
  createDirectory(PACKAGE_PATH);
  createDirectory(LANG_PATH);
  createDirectory(FLAGS_PATH);
  createDirectory(NOTIFICATIONS_PATH);
  createDirectory(THEMES_PATH);
  createDirectory(WALLPAPERS_PATH);
  createDirectory(SOUNDS_PATH);

  bool settingsLoaded = false;
  bool needsUpdate = false;

  std::map<std::string, std::map<std::string, std::string>> iniData;

  // Check if file didn't exist
  if (isFile(ULTRAHAND_CONFIG_INI_PATH)) {
    // Always try to load INI data (will be empty if file doesn't exist)
    iniData = getParsedDataFromIniFile(ULTRAHAND_CONFIG_INI_PATH);
    for (int i = 0; i < 3; i++) {
      if (iniData.empty() || iniData[ULTRAHAND_PROJECT_NAME].empty()) {
        svcSleepThread(100'000);
        iniData = getParsedDataFromIniFile(ULTRAHAND_CONFIG_INI_PATH);
      } else {
        break;
      }
    }
  }

  auto &ultrahandSection = iniData[ULTRAHAND_PROJECT_NAME];

  // Efficient lambdas that modify in-memory data and track updates
  auto setDefaultValue = [&](const std::string &section,
                             const std::string &defaultValue,
                             bool &settingFlag) {
    if (ultrahandSection.count(section) > 0) {
      settingFlag = (ultrahandSection.at(section) == TRUE_STR);
    } else {
      ultrahandSection[section] = defaultValue;
      settingFlag = (defaultValue == TRUE_STR);
      needsUpdate = true;
    }
  };

  auto setDefaultStrValue = [&](const std::string &section,
                                const std::string &defaultValue,
                                std::string &settingValue) {
    if (ultrahandSection.count(section) > 0) {
      settingValue = ultrahandSection.at(section);
    } else {
      ultrahandSection[section] = defaultValue;
      settingValue = defaultValue;
      needsUpdate = true;
    }
  };

  // Set default values for various settings (works for both existing and new
  // files)
  setDefaultValue("hide_user_guide", FALSE_STR, hideUserGuide);
  setDefaultValue("hide_hidden", FALSE_STR, hideHidden);
  setDefaultValue("hide_delete", FALSE_STR, hideDelete);
  if (requiresLNY2) {
    // setDefaultValue("hide_force_support", TRUE_STR, hideForceSupport);
    setDefaultValue("hide_unsupported", FALSE_STR, hideUnsupported);
  }
  setDefaultValue("clean_version_labels", FALSE_STR, cleanVersionLabels);
  setDefaultValue("hide_overlay_versions", FALSE_STR, hideOverlayVersions);
  setDefaultValue("hide_package_versions", FALSE_STR, hidePackageVersions);

  setDefaultValue("dynamic_logo", TRUE_STR, useDynamicLogo);
  setDefaultValue("selection_bg", TRUE_STR, useSelectionBG);
  setDefaultValue("selection_text", FALSE_STR, useSelectionText);
  setDefaultValue("selection_value", FALSE_STR, useSelectionValue);
  setDefaultValue("libultrahand_titles", FALSE_STR, useLibultrahandTitles);
  setDefaultValue("libultrahand_versions", TRUE_STR, useLibultrahandVersions);
  setDefaultValue("package_titles", FALSE_STR, usePackageTitles);
  setDefaultValue("package_versions", TRUE_STR, usePackageVersions);
  // setDefaultValue("memory_expansion", FALSE_STR, useMemoryExpansion);
  setDefaultValue("launch_combos", TRUE_STR, useLaunchCombos);
  setDefaultValue("notifications", TRUE_STR, useNotifications);
  setDefaultValue("startup_notification", TRUE_STR, useStartupNotification);
  setDefaultValue("sound_effects", TRUE_STR, useSoundEffects);
  setDefaultValue("haptic_feedback", FALSE_STR, useHapticFeedback);
  setDefaultValue("page_swap", FALSE_STR, usePageSwap);
  setDefaultValue("swipe_to_open", TRUE_STR, useSwipeToOpen);
  setDefaultValue("right_alignment", FALSE_STR, useRightAlignment);
  setDefaultValue("take_over_ovlmenu", FALSE_STR, takeOverOvlmenu);
  setDefaultValue("opaque_screenshots", TRUE_STR, useOpaqueScreenshots);

  setDefaultStrValue(DEFAULT_LANG_STR, defaultLang, defaultLang);

  // Widget settings - now properly loaded into variables
  setDefaultValue("hide_clock", FALSE_STR, hideClock);
  setDefaultValue("hide_battery", TRUE_STR, hideBattery);
  setDefaultValue("hide_pcb_temp", TRUE_STR, hidePCBTemp);
  setDefaultValue("hide_soc_temp", TRUE_STR, hideSOCTemp);
  setDefaultValue("dynamic_widget_colors", TRUE_STR, dynamicWidgetColors);
  setDefaultValue("hide_widget_backdrop", FALSE_STR, hideWidgetBackdrop);
  setDefaultValue("center_widget_alignment", TRUE_STR, centerWidgetAlignment);
  setDefaultValue("extended_widget_backdrop", FALSE_STR,
                  extendedWidgetBackdrop);

  // Datetime format string setting
  setDefaultStrValue("datetime_format", DEFAULT_DT_FORMAT, datetimeFormat);

  // Check if settings were previously loaded
  settingsLoaded = ultrahandSection.count(IN_OVERLAY_STR) > 0;

  // Handle the 'to_packages' option if it exists
  if (ultrahandSection.count("to_packages") > 0) {
    trim(ultrahandSection["to_packages"]);
    toPackages = (ultrahandSection["to_packages"] == TRUE_STR);
  }

  // Handle the 'in_overlay' setting
  if (settingsLoaded) {
    inOverlay = (ultrahandSection[IN_OVERLAY_STR] == TRUE_STR);
  }

  // If settings weren't previously loaded, add the missing defaults
  if (!settingsLoaded) {
    ultrahandSection[DEFAULT_LANG_STR] = defaultLang;
    ultrahandSection[IN_OVERLAY_STR] = FALSE_STR;
    needsUpdate = true;
  }

  // Only write back to file if we made changes
  if (needsUpdate) {
    saveIniFileData(ULTRAHAND_CONFIG_INI_PATH, iniData);
  }

  if (useNotifications) {
    if (!isFile(NOTIFICATIONS_FLAG_FILEPATH)) {
      FILE *file = std::fopen((NOTIFICATIONS_FLAG_FILEPATH).c_str(), "w");
      if (file) {
        std::fclose(file);
      }
    }
  } else {
    deleteFileOrDirectory(NOTIFICATIONS_FLAG_FILEPATH);
  }

  // Load language file
  const std::string langFile = LANG_PATH + defaultLang + ".json";
  if (isFile(langFile))
    parseLanguage(langFile);
  else {
    if (defaultLang == "en")
      reinitializeLangVars();
  }

  // Load local font if needed based on overlay language setting
  if (defaultLang == "zh-cn") {
    tsl::gfx::Renderer::get().loadLocalFont(PlSharedFontType_ChineseSimplified);
  } else if (defaultLang == "zh-tw") {
    tsl::gfx::Renderer::get().loadLocalFont(
        PlSharedFontType_ChineseTraditional);
  } else if (defaultLang == "ko") {
    tsl::gfx::Renderer::get().loadLocalFont(PlSharedFontType_KO);
  }

  // Initialize theme
  initializeTheme();
  tsl::initializeThemeVars();
  updateMenuCombos = copyTeslaKeyComboToUltrahand();

  // Set current menu based on settings
  static bool hasInitialized = false;
  if (!hasInitialized) {
    currentMenu = OVERLAYS_STR;
    hasInitialized = true;
  }
}

static void setFooterBackLabel(const bool useRestart) {
  if (BACK != "Restart") {
    g_defaultBackLabel = BACK;
  }
  BACK = useRestart ? "Restart" : g_defaultBackLabel;
}

/**
 * @brief The `Overlay` class manages the main overlay functionality.
 *
 * This class is responsible for handling the main overlay, which provides
 * access to various application features and options. It initializes necessary
 * services, handles user input, and manages the transition between different
 * menu modes.
 */
#ifndef EDITCHEAT_OVL
class Overlay : public tsl::Overlay {
public:
  /**
   * @brief Performs actions when the overlay becomes visible.
   *
   * This function is called when the overlay transitions from an invisible
   * state to a visible state. It can be used to perform actions or updates
   * specific to the overlay's visibility.
   */
  virtual void onShow() override {}

  /**
   * @brief Performs actions when the overlay becomes visible.
   *
   * This function is called when the overlay transitions from an invisible
   * state to a visible state. It can be used to perform actions or updates
   * specific to the overlay's visibility.
   */
  virtual void onHide() override {}

  /**
   * @brief Loads the initial graphical user interface (GUI) for the overlay.
   *
   * This function is responsible for loading the initial GUI when the overlay
   * is launched. It returns a unique pointer to the GUI element that will be
   * displayed as the overlay's starting interface. You can also pass arguments
   * to the constructor of the GUI element if needed.
   *
   * @return A unique pointer to the initial GUI element.
   */
  virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
    // settingsInitialized.exchange(false, acq_rel);
    // tsl::gfx::FontManager::preloadPersistentGlyphs("0123456789%●",
    // 20);
    // tsl::gfx::FontManager::preloadPersistentGlyphs(""+ult::HIDE+""+ult::CANCEL,
    // 23);
    initializeSettingsAndDirectories();

    // Check if a package was specified via command line
    if (!selectedPackage.empty()) {

      const std::string packageFilePath = PACKAGE_PATH + selectedPackage + "/";

      // Check if the package directory exists
      if (isFileOrDirectory(packageFilePath)) {
        // GET PROPER PACKAGE TITLE AND VERSION (like main menu does)
        PackageHeader packageHeader =
            getPackageHeaderFromIni(packageFilePath + PACKAGE_FILENAME);

        // Load packages.ini to check for custom name/version
        const std::map<std::string, std::map<std::string, std::string>>
            packagesIniData = getParsedDataFromIniFile(PACKAGES_INI_FILEPATH);

        std::string customName = "";
        std::string customVersion = "";
        std::string assignedOverlayName;
        std::string assignedOverlayVersion;

        // Check if package exists in packages.ini
        auto packageIt = packagesIniData.find(selectedPackage);
        if (packageIt != packagesIniData.end()) {
          // Get custom name and version if they exist
          customName = getValueOrDefault(packageIt->second, "custom_name", "");
          customVersion =
              getValueOrDefault(packageIt->second, "custom_version", "");
        }

        // Apply version cleaning if needed (same logic as main menu)
        if (cleanVersionLabels) {
          packageHeader.version = cleanVersionLabel(packageHeader.version);
          removeQuotes(packageHeader.version);
        }

        // Determine final name and version (same logic as main menu)
        assignedOverlayName = !customName.empty() ? customName
                                                  : (packageHeader.title.empty()
                                                         ? selectedPackage
                                                         : packageHeader.title);
        assignedOverlayVersion =
            !customVersion.empty() ? customVersion : packageHeader.version;

        // Handle boot package logic (similar to your KEY_A handler)
        if (isFile(packageFilePath + BOOT_PACKAGE_FILENAME)) {
          // bool useBootPackage =
          // !(parseValueFromIniSection(PACKAGES_INI_FILEPATH, selectedPackage,
          // USE_BOOT_PACKAGE_STR) == FALSE_STR); if (!selectedPackage.empty())
          //     useBootPackage = (useBootPackage &&
          //     !(parseValueFromIniSection(PACKAGES_INI_FILEPATH,
          //     selectedPackage, USE_QUICK_LAUNCH_STR) == TRUE_STR));

          bool useBootPackage = true;
          {
            // Load INI data once and extract both values
            const auto packagesIniData =
                getParsedDataFromIniFile(PACKAGES_INI_FILEPATH);
            auto sectionIt = packagesIniData.find(selectedPackage);

            if (sectionIt != packagesIniData.end()) {
              auto bootIt = sectionIt->second.find(USE_BOOT_PACKAGE_STR);
              useBootPackage = (bootIt == sectionIt->second.end()) ||
                               (bootIt->second != FALSE_STR);

              if (!selectedPackage.empty()) {
                auto quickIt = sectionIt->second.find(USE_QUICK_LAUNCH_STR);
                const bool useQuickLaunch =
                    (quickIt != sectionIt->second.end()) &&
                    (quickIt->second == TRUE_STR);
                useBootPackage = useBootPackage && !useQuickLaunch;
              }
            }
          }

          if (useBootPackage) {
            // Load only the commands from the specific section
            // (bootCommandName)
            auto bootCommands = loadSpecificSectionFromIni(
                packageFilePath + BOOT_PACKAGE_FILENAME, "boot");

            if (!bootCommands.empty()) {
              // bool resetCommandSuccess = false;
              // if (!commandSuccess.load(acquire)) resetCommandSuccess = true;
              const bool resetCommandSuccess =
                  !commandSuccess.load(std::memory_order_acquire);

              interpretAndExecuteCommands(std::move(bootCommands),
                                          packageFilePath, "boot");
              resetPercentages();

              if (resetCommandSuccess) {
                commandSuccess.store(false, release);
              }
            }
          }
        }

        // Set the necessary global variables with PROPER NAMES
        // lastPackagePath = packageFilePath;
        // lastPackageName = PACKAGE_FILENAME;
        packageRootLayerTitle = assignedOverlayName;      // Use proper title
        packageRootLayerVersion = assignedOverlayVersion; // Use proper version

        inMainMenu.store(false, std::memory_order_release);

        // Return PackageMenu directly instead of MainMenu
        return initially<PackageMenu>(packageFilePath, "");
      } else {
        // Package not found, clear the selection and fall back to main menu
        selectedPackage.clear();
      }
    }
    if (firstBoot && tsl::notification && useStartupNotification) {
      tsl::notification->show("  " + ((!reloadingBoot)
                                             ? ULTRAHAND_HAS_STARTED
                                             : ULTRAHAND_HAS_RESTARTED));
    }

    // settingsInitialized.exchange(true, acq_rel);
    //  Default behavior - load main menu
    return initially<MainMenu>();
  }

  /**
   * @brief Initializes essential services and resources.
   *
   * This function initializes essential services and resources required for the
   * overlay to function properly. It sets up file system mounts, initializes
   * network services, and performs other necessary tasks.
   */
  virtual void initServices() override {
    tsl::overrideBackButton = true; // for properly overriding the always go
                                    // back functionality of KEY_B

    // constexpr SocketInitConfig socketInitConfig = {
    //     // TCP buffers
    //     .tcp_tx_buf_size     = 16 * 1024,   // 16 KB default
    //     .tcp_rx_buf_size     = 16 * 1024*2,   // 16 KB default
    //     .tcp_tx_buf_max_size = 64 * 1024,   // 64 KB default max
    //     .tcp_rx_buf_max_size = 64 * 1024*2,   // 64 KB default max
    //
    //     // UDP buffers
    //     .udp_tx_buf_size     = 512,         // 512 B default
    //     .udp_rx_buf_size     = 512,         // 512 B default
    //
    //     // Socket buffer efficiency
    //     .sb_efficiency       = 1,           // 0 = default, balanced memory
    //     vs CPU
    //                                         // 1 = prioritize memory
    //                                         efficiency (smaller internal
    //                                         allocations)
    //     .bsd_service_type    = BsdServiceType_Auto // Auto-select service
    // };
    // socketInitialize(&socketInitConfig);

    // read commands from root package's boot_package.ini
    if (firstBoot) {
      if (!isFile(RELOADING_FLAG_FILEPATH)) {
        // Delete all pending notification jsons
        {
          std::lock_guard<std::mutex> jsonLock(tsl::notificationJsonMutex);
          deleteFileOrDirectoryByPattern(ult::NOTIFICATIONS_PATH + "*.notify");
        }

        // Load and execute "initial_boot" commands if they exist
        executeIniCommands(PACKAGE_PATH + BOOT_PACKAGE_FILENAME, "boot");

        const bool disableFuseReload =
            (parseValueFromIniSection(FUSE_DATA_INI_PATH, FUSE_STR,
                                      "disable_reload") == TRUE_STR);
        if (!disableFuseReload)
          deleteFileOrDirectory(FUSE_DATA_INI_PATH);

        // if (tsl::notification)
        //     tsl::notification->show("  Testing first boot.");
      } else {
        reloadingBoot = true;
      }
    }

    deleteFileOrDirectory(RELOADING_FLAG_FILEPATH);
    unpackDeviceInfo();

    // Initialize dmntcht and ns
    dmntchtInitialize();
    nsInitialize();

    // CheckButtons thread removed
  }

  virtual void exitServices() override {
    StopSearchWorker();
    dmntchtExit();
    nsExit();

    closeInterpreterThread(); // just in case ¯\_(ツ)_/¯

    if (exitingUltrahand.load(std::memory_order_acquire) && !reloadingBoot)
      executeIniCommands(PACKAGE_PATH + EXIT_PACKAGE_FILENAME, "exit");

    curl_global_cleanup(); // safe cleanup
  }
};
#endif

#ifdef EDITCHEAT_OVL
class EditCheatOverlay : public tsl::Overlay {
public:
  virtual void onShow() override {}
  virtual void onHide() override {}

  virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
    initializeSettingsAndDirectories();

    // Manual launch: try to find a default cheat if none specified
    if (g_cheatIdToEdit == 0) {
      u64 count = 0;
      if (R_SUCCEEDED(dmntchtGetCheatCount(&count)) && count > 0) {
        std::vector<DmntCheatEntry> cheats(count);
        if (R_SUCCEEDED(dmntchtGetCheats(cheats.data(), count, 0, &count))) {
          if (count > 0) {
            g_cheatIdToEdit = cheats[0].cheat_id;
            g_cheatNameToEdit = cheats[0].definition.readable_name;
            g_cheatEnabledToEdit = cheats[0].enabled;
          }
        }
      }
    }

    return std::make_unique<CheatEditMenu>(g_cheatIdToEdit, g_cheatNameToEdit,
                                           g_cheatEnabledToEdit);
  }

  virtual void initServices() override {
    initializeSettingsAndDirectories();
    deleteFileOrDirectory(RELOADING_FLAG_FILEPATH);
    unpackDeviceInfo();
    dmntchtInitialize();
    nsInitialize();

    // Set settingsInitialized so background event loop can process overlay
    // launch requests
    ult::settingsInitialized.store(true, std::memory_order_release);

    // Clear the flag so Breezehand doesn't think it's still in an overlay loop
    // if we crash
    // ult::setIniFileValue(ult::ULTRAHAND_CONFIG_INI_PATH,
    //                      ult::ULTRAHAND_PROJECT_NAME, ult::IN_OVERLAY_STR,
    //                      ult::FALSE_STR);
  }

  virtual void exitServices() override {
    StopSearchWorker();
    dmntchtExit();
    nsExit();

    closeInterpreterThread(); // just in case ¯\_(ツ)_/¯

    if (exitingUltrahand.load(std::memory_order_acquire) && !reloadingBoot)
      executeIniCommands(PACKAGE_PATH + EXIT_PACKAGE_FILENAME, "exit");

    curl_global_cleanup(); // safe cleanup
  }
};
#endif

/**
 * @brief The entry point of the application.
 *
 * This function serves as the entry point for the application. It takes
 * command-line arguments, initializes necessary services, and starts the main
 * loop of the overlay. The `argc` parameter represents the number of
 * command-line arguments, and `argv` is an array of C-style strings containing
 * the actual arguments.
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of C-style strings representing command-line arguments.
 * @return The application's exit code.
 */
bool UltrahandSettingsMenu::handleInput(u64 keysDown, u64 keysHeld,
                                        touchPosition touchInput,
                                        JoystickPosition leftJoyStick,
                                        JoystickPosition rightJoyStick) {

  // Handle delete item continuous hold behavior
  if (isHolding) {
    processHold(keysDown, keysHeld, holdStartTick, isHolding, [this]() {
      if (requestOverlayExit()) {
        ult::launchingOverlay.store(true, std::memory_order_release);
        tsl::Overlay::get()->close();
      }
    });
  }

  const bool isRunningInterp = runningInterpreter.load(acquire);

  if (isRunningInterp) {
    return handleRunningInterpreter(keysDown, keysHeld);
  }

  if (lastRunningInterpreter.exchange(false, std::memory_order_acq_rel)) {
    isDownloadCommand.store(false, release);
    if (lastSelectedListItem) {
      lastSelectedListItem->setValue(
          commandSuccess.load(acquire) ? CHECKMARK_SYMBOL : CROSSMARK_SYMBOL);
      lastSelectedListItem->enableClickAnimation();
      lastSelectedListItem = nullptr;
    }

    closeInterpreterThread();
    resetPercentages();

    if (!commandSuccess.load()) {
      triggerRumbleDoubleClick.store(true, std::memory_order_release);
    }

    if (!limitedMemory && useSoundEffects) {
      reloadSoundCacheNow.store(true, std::memory_order_release);
      // ult::Audio::initialize();
    }
    // lastRunningInterpreter.store(false, std::memory_order_release);
    return true;
  }

  if (goBackAfter.exchange(false, std::memory_order_acq_rel)) {
    disableSound.store(true, std::memory_order_release);
    simulatedBack.store(true, std::memory_order_release);
    return true;
  }

  // if (refreshPage.load(acquire)) {
  //     //tsl::goBack();tsl::changeTo<UltrahandSettingsMenu>(targetMenu);
  //     tsl::swapTo<UltrahandSettingsMenu>("languageMenu");
  //     refreshPage.store(false, release);
  // }

  if (inSettingsMenu && !inSubSettingsMenu) {
    if (!returningToSettings) {
      // if (simulatedNextPage.load(acquire))
      //     simulatedNextPage.store(false, release);
      // if (simulatedMenu.load(acquire))
      //     simulatedMenu.store(false, release);
      simulatedNextPage.exchange(false, std::memory_order_acq_rel);
      simulatedMenu.exchange(false, std::memory_order_acq_rel);

      const bool isTouching = stillTouching.load(acquire);
      const bool backKeyPressed =
          !isTouching &&
          (((keysDown & KEY_B) && !(keysHeld & ~KEY_B & ALL_KEYS_MASK)));

      if (backKeyPressed) {

        allowSlide.exchange(false, std::memory_order_acq_rel);
        unlockedSlide.exchange(false, std::memory_order_acq_rel);
        inSettingsMenu = false;
        returningToMain = (lastMenu != "hiddenMenuMode");
        returningToHiddenMain = !returningToMain;
        lastMenu = "settingsMenu";

        if (reloadMenu) {
          // sl::pop(2);
          tsl::swapTo<MainMenu>(SwapDepth(3), lastMenuMode);
          reloadMenu = false;
        } else {
          tsl::goBack();
        }
        return true;
      }
    } else {
      returningToSettings = false;
    }
  } else if (inSubSettingsMenu) {
    simulatedNextPage.exchange(false, std::memory_order_acq_rel);
    simulatedMenu.exchange(false, std::memory_order_acq_rel);

    const bool isTouching = stillTouching.load(acquire);
    const bool backKeyPressed =
        !isTouching &&
        (((keysDown & KEY_B) && !(keysHeld & ~KEY_B & ALL_KEYS_MASK)));

    if (backKeyPressed) {
      if (exitOnBack) {
        ult::launchingOverlay.store(true, std::memory_order_release);
        tsl::Overlay::get()->close();
        return true;
      }

      if (softwareHasUpdated) {
        // Instead of going back, trigger the reload directly
        // disableSound.store(true, std::memory_order_release);

        setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
                        IN_OVERLAY_STR, TRUE_STR);

        ult::launchingOverlay.store(true, std::memory_order_release);
        tsl::setNextOverlay(OVERLAY_PATH + "ovlmenu.ovl",
                            "--skipCombo --comboReturn");
        tsl::Overlay::get()->close();
        return true;
      }

      allowSlide.exchange(false, std::memory_order_acq_rel);
      unlockedSlide.exchange(false, std::memory_order_acq_rel);
      inSubSettingsMenu = false;
      returningToSettings = true;

      if (reloadMenu2) {
        // tsl::goBack(2);
        tsl::swapTo<UltrahandSettingsMenu>(SwapDepth(3));
        reloadMenu2 = false;
      } else {
        tsl::goBack();
      }
      return true;
    }
  }

  if (returningToSettings && !(keysDown & KEY_B)) {
    returningToSettings = false;
    inSettingsMenu = true;
    tsl::impl::parseOverlaySettings();
  }

  if (triggerExit.exchange(false, std::memory_order_acq_rel)) {
    ult::launchingOverlay.store(true, std::memory_order_release);

    if (softwareHasUpdated && requestOverlayExit()) {
    } else {
      tsl::setNextOverlay(OVERLAY_PATH + "ovlmenu.ovl");
    }
    tsl::Overlay::get()->close(); // Close the overlay
  }

  return false;
}

bool SettingsMenu::handleInput(u64 keysDown, u64 keysHeld,
                               touchPosition touchInput,
                               JoystickPosition leftJoyStick,
                               JoystickPosition rightJoyStick) {

  // const bool isRunningInterp = runningInterpreter.load(acquire);
  //
  // if (isRunningInterp) {
  //     return handleRunningInterpreter(keysDown, keysHeld);
  // }
  //
  // if (lastRunningInterpreter.load(acquire)) {
  //     isDownloadCommand.store(false, release);
  //     if (lastSelectedListItem)
  //         lastSelectedListItem->setValue(commandSuccess.load(acquire) ?
  //         CHECKMARK_SYMBOL : CROSSMARK_SYMBOL);
  //     closeInterpreterThread();
  //     lastRunningInterpreter.store(false, std::memory_order_release);
  //     return true;
  // }

  if (goBackAfter.exchange(false, std::memory_order_acq_rel)) {
    disableSound.store(true, std::memory_order_release);
    simulatedBack.store(true, std::memory_order_release);
    return true;
  }

  static bool runAfter = false;
  if (runAfter) {
    runAfter = false;
    // Reset navigation state properly
    inSettingsMenu = false;
    inSubSettingsMenu = false;

    // Clear the current selection
    ////g_overlayFilename = "";
    // entryName = "";

    // Determine return destination
    if (lastMenu != "hiddenMenuMode")
      returningToMain = true;
    else
      returningToHiddenMain = true;

    // Determine pop count and hidden mode settings
    int popCount;
    if (lastMenu == "hiddenMenuMode") {
      popCount = 3;
      inMainMenu.store(false, std::memory_order_release);
      inHiddenMode.store(true, std::memory_order_release);
      if (entryMode == OVERLAY_STR)
        setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
                        IN_HIDDEN_OVERLAY_STR, TRUE_STR);
      else
        popCount = 2;
    } else {
      popCount = 2;
    }

    runningInterpreter.store(false, release);
    jumpItemName = rootTitle;
    jumpItemValue = rootVersion;
    // g_overlayFilename = "";
    jumpItemExactMatch.store(false, release);
    skipJumpReset.store(true, release);

    tsl::swapTo<MainMenu>(SwapDepth(popCount), lastMenuMode);
    return true;
  }

  // Handle delete item continuous hold behavior
  if (isHolding) {
    processHold(
        keysDown, keysHeld, holdStartTick, isHolding,
        [this]() {
          std::string targetPath;
          bool hasTarget = false;

          if (!entryName.empty() && entryMode == OVERLAY_STR) {
            targetPath = OVERLAY_PATH + entryName;
            hasTarget = true;
          } else if (!entryName.empty()) {
            targetPath = PACKAGE_PATH + entryName + "/";
            hasTarget = true;
          }

          if (hasTarget) {
            deleteFileOrDirectory(targetPath);
            removeIniSection(settingsIniPath, entryName);

            if (lastSelectedListItem) {
              // lastSelectedListItem->enableClickAnimation();
              // lastSelectedListItem->triggerClickAnimation();
              lastSelectedListItem->setValue(CHECKMARK_SYMBOL);
              // lastSelectedListItem->disableClickAnimation();
              lastSelectedListItem = nullptr;
            }

            triggerRumbleDoubleClick.store(true, std::memory_order_release);
            triggerMoveSound.store(true, std::memory_order_release);
            runAfter = true;
          } else if (lastSelectedListItem) {
            lastSelectedListItem->setValue(CROSSMARK_SYMBOL);
            lastSelectedListItem = nullptr;
          }
        },
        nullptr, false); // false = do NOT reset storedCommands
  }

  if (inSettingsMenu && !inSubSettingsMenu) {
    if (!returningToSettings) {
      if (simulatedNextPage.load(acquire))
        simulatedNextPage.store(false, release);
      if (simulatedMenu.load(acquire))
        simulatedMenu.store(false, release);

      const bool isTouching = stillTouching.load(acquire);
      const bool backKeyPressed =
          !isTouching &&
          (((keysDown & KEY_B) && !(keysHeld & ~KEY_B & ALL_KEYS_MASK)));

      // Note: Original code uses !stillTouching without .load() - preserving
      // this exactly
      if (backKeyPressed) {
        if (allowSlide.load(acquire))
          allowSlide.store(false, release);
        if (unlockedSlide.load(acquire))
          unlockedSlide.store(false, release);
        inSettingsMenu = false;

        // Determine return destination
        if (lastMenu != "hiddenMenuMode")
          returningToMain = true;
        else
          returningToHiddenMain = true;

        if (reloadMenu) {
          reloadMenu = false;

          // Determine pop count and hidden mode settings
          size_t popCount;
          if (lastMenu == "hiddenMenuMode") {
            popCount = 3;
            inMainMenu.store(false, std::memory_order_release);
            inHiddenMode.store(true, std::memory_order_release);
            if (entryMode == OVERLAY_STR)
              setIniFileValue(ULTRAHAND_CONFIG_INI_PATH, ULTRAHAND_PROJECT_NAME,
                              IN_HIDDEN_OVERLAY_STR, TRUE_STR);
            else
              popCount = 2;
          } else {
            popCount = 2;
          }

          // tsl::pop(popCount);
          //{
          //     //std::lock_guard<std::mutex> lock(jumpItemMutex);
          jumpItemName = rootTitle;
          jumpItemValue = rootVersion;
          // g_overlayFilename = "";
          jumpItemExactMatch.store(false, release);
          skipJumpReset.store(true, release);
          //}

          tsl::swapTo<MainMenu>(SwapDepth(popCount), lastMenuMode);
        } else {
          tsl::goBack();
        }

        lastMenu = "settingsMenu";
        return true;
      }
    }
  } else if (inSubSettingsMenu) {
    simulatedNextPage.exchange(false, std::memory_order_acq_rel);
    simulatedMenu.exchange(false, std::memory_order_acq_rel);

    // Note: Original code uses stillTouching.load() here - preserving this
    // difference
    const bool isTouching = stillTouching.load(acquire);
    const bool backKeyPressed =
        !isTouching &&
        (((keysDown & KEY_B) && !(keysHeld & ~KEY_B & ALL_KEYS_MASK)));

    if (backKeyPressed) {
      allowSlide.exchange(false, std::memory_order_acq_rel);
      unlockedSlide.exchange(false, std::memory_order_acq_rel);

      if (dropdownSelection == MODE_STR) {
        modeTitle = LAUNCH_MODES;
        reloadMenu2 = true;
      }

      else if (dropdownSelection.rfind("mode_combo_", 0) != 0) {
        inSubSettingsMenu = false;
        returningToSettings = true;
      }

      // Step 1: Go back one menu level
      // Step 2: If reload is needed, change to SettingsMenu with focus
      if (reloadMenu2) {
        reloadMenu2 = false;
        // tsl::goBack(2);

        //{
        //    //std::lock_guard<std::mutex> lock(jumpItemMutex);
        // Provide jump target context
        jumpItemName = modeTitle;
        jumpItemValue = "";
        jumpItemExactMatch.store(true, release);
        // g_overlayFilename = "";
        // }

        tsl::swapTo<SettingsMenu>(SwapDepth(2), rootEntryName, rootEntryMode,
                                  rootTitle, rootVersion);
      } else {
        if (modeComboModified) {
          // Go back to MODE_STR screen to refresh the combo display
          jumpItemName = modeTitle; // Focus on the mode that was just edited
          jumpItemValue = "";
          jumpItemExactMatch.store(true, release);
          // g_overlayFilename = "";

          tsl::swapTo<SettingsMenu>(SwapDepth(2), rootEntryName, rootEntryMode,
                                    rootTitle, rootVersion,
                                    MODE_STR // Go back to the modes list screen
          );
          // Keep modeComboModified true for the next level up
        } else {
          tsl::goBack();
        }
      }

      if (modeComboModified) {
        modeComboModified = false;
        // jumpItemName = MODES;
        // jumpItemValue = "";
        // jumpItemExactMatch.store(true, release);
        ////g_overlayFilename = "";

        // reloadMenu2 = true;
      }

      return true;
    }
  }

  if (returningToSettings && !(keysDown & KEY_B)) {
    returningToSettings = false;
    inSettingsMenu = true;
  }

  if (triggerExit.exchange(false, std::memory_order_acq_rel)) {
    ult::launchingOverlay.store(true, std::memory_order_release);
    tsl::setNextOverlay(OVERLAY_PATH + "ovlmenu.ovl");
    tsl::Overlay::get()->close();
  }

  return false;
}

void TransitionToMainMenu(const std::string &arg1, const std::string &arg2) {
  tsl::changeTo<MainMenu>(arg1, arg2);
}

void SwapToMainMenu() { tsl::swapTo<MainMenu>(); }

// --- CheatMenu Implementation ---

tsl::elm::Element *CheatMenu::createUI() {
  setFooterBackLabel(false);
  auto *rootFrame = new tsl::elm::OverlayFrame("Breezehand", "Cheat Options");
  auto *list = new tsl::elm::List();

  list->addItem(new tsl::elm::CategoryHeader("Load Cheats"));

  auto *loadAmsItem = new tsl::elm::ListItem("Load from AMS");
  loadAmsItem->setClickListener([](u64 keys) {
    if (keys & KEY_A) {
      std::string bid = CheatUtils::GetBuildIdString();
      std::string tid = CheatUtils::GetTitleIdString();
      std::string path =
          "sdmc:/atmosphere/contents/" + tid + "/cheats/" + bid + ".txt";
      std::string toggles =
          "sdmc:/atmosphere/contents/" + tid + "/cheats/toggles.txt";

      if (CheatUtils::ParseCheats(path)) {
        CheatUtils::LoadToggles(toggles);
        tsl::notification->show("Loaded AMS Cheats");
      } else {
        tsl::notification->show("Cheat file not found\n(AMS)");
      }
      refreshPage.store(true, std::memory_order_release);
      tsl::goBack();
      return true;
    }
    return false;
  });
  list->addItem(loadAmsItem);

  auto *loadFileItem = new tsl::elm::ListItem("Load from File");
  loadFileItem->setClickListener([](u64 keys) {
    if (keys & KEY_A) {
      std::string bid = CheatUtils::GetBuildIdString();
      std::string tid = CheatUtils::GetTitleIdString();
      std::string path =
          "sdmc:/switch/breeze/cheats/" + tid + "/" + bid + ".txt";
      std::string toggles =
          "sdmc:/switch/breeze/cheats/" + tid + "/toggles.txt";

      if (CheatUtils::ParseCheats(path)) {
        CheatUtils::LoadToggles(toggles);
        tsl::notification->show("Loaded File Cheats");
      } else {
        tsl::notification->show("Cheat file not found\n(Breeze)");
      }
      refreshPage.store(true, std::memory_order_release);
      tsl::goBack();
      return true;
    }
    return false;
  });
  list->addItem(loadFileItem);

  auto *downloadItem = new tsl::elm::ListItem("Download from URL");
  downloadItem->setClickListener([](u64 keys) {
    if (keys & KEY_A) {
      // Check if we have sufficient memory for download operations (6MB required)
      if (!checkOverlayMemory(6)) {
        // User didn't increase memory, abort the operation
        return true;
      }
      
      if (CheatUtils::TryDownloadCheats(true)) {
        refreshPage.store(true, std::memory_order_release);
        tsl::goBack();
      }
      return true;
    }
    return false;
  });
  list->addItem(downloadItem);

  auto *convertToFolderItem = new tsl::elm::ListItem("Convert to folder");
  convertToFolderItem->setClickListener([](u64 keys) {
    if (keys & KEY_A) {
      const u32 converted = CheatUtils::ConvertTripleZeroCheatsToFolders();
      if (converted > 0) {
        tsl::notification->show("Converted " + std::to_string(converted) +
                                " marker cheats");
        refreshPage.store(true, std::memory_order_release);
        tsl::goBack();
      } else {
        tsl::notification->show("No 00000000 marker cheats found");
      }
      return true;
    }
    return false;
  });
  list->addItem(convertToFolderItem);

  list->addItem(new tsl::elm::CategoryHeader("Combo Keys"));

  if (this->cheat_id != 0) {
    // Context-aware "Set Combo Key" using new Hold Item
    auto *setComboItem = new CheatUtils::ComboSetItem(
        "Set Combo Key (Hold 0.5s)", this->cheat_id);
    setComboItem->setClickListener([](u64 keys) { return false; });
    list->addItem(setComboItem);

    // Context-aware "Remove Combo Key"
    auto *removeComboItem = new tsl::elm::ListItem("Remove Combo Key");
    removeComboItem->setClickListener([this](u64 keys) {
      if (keys & KEY_A) {
        CheatUtils::RemoveComboKeyFromCheat(this->cheat_id);
        refreshPage.store(true, std::memory_order_release);
        tsl::goBack();
        return true;
      }
      return false;
    });
    list->addItem(removeComboItem);

    // Save to File (Breeze)
    auto *saveToBreezeItem = new tsl::elm::ListItem("Save to File (Breeze)");
    saveToBreezeItem->setClickListener([this](u64 keys) {
      if (keys & KEY_A) {
        CheatUtils::SaveCheatsToDir("sdmc:/switch/breeze/cheats/" +
                                    CheatUtils::GetTitleIdString() + "/");
        tsl::notification->show("Saved to Breeze directory");
        refreshPage.store(true, std::memory_order_release);
        tsl::goBack();
        return true;
      }
      return false;
    });
    list->addItem(saveToBreezeItem);

    // Save to AMS (Atmosphere)
    auto *saveToAmsItem = new tsl::elm::ListItem("Save to AMS");
    saveToAmsItem->setClickListener([this](u64 keys) {
      if (keys & KEY_A) {
        CheatUtils::SaveCheatsToDir("sdmc:/atmosphere/contents/" +
                                    CheatUtils::GetTitleIdString() +
                                    "/cheats/");
        tsl::notification->show("Saved to Atmosphere directory");
        refreshPage.store(true, std::memory_order_release);
        tsl::goBack();
        return true;
      }
      return false;
    });
    list->addItem(saveToAmsItem);
  } else {
    auto *item = new tsl::elm::ListItem("Select a cheat to set combo!");
    item->setClickListener([](u64 keys) {
      if (keys & KEY_A) {
        tsl::goBack();
        tsl::notification->show("Press X on a cheat in the list to configure.");
        return true;
      }
      return false;
    });
    list->addItem(item);
  }

  rootFrame->setContent(list);
  return rootFrame;
}

bool CheatMenu::handleInput(u64 keysDown, u64 keysHeld,
                            touchPosition touchInput,
                            JoystickPosition leftJoyStick,
                            JoystickPosition rightJoyStick) {
  if (keysDown & KEY_B) {
    tsl::goBack();
    return true;
  }
  return false;
}

MainComboSetItem::MainComboSetItem(const std::string &text,
                                   const std::string &value)
    : tsl::elm::ListItem(text) {
  this->setValue(value);
  this->setNote("Press A to start capture");
  this->setAlwaysShowNote(true);
}

bool MainComboSetItem::handleInput(u64 keysDown, u64 keysHeld,
                                   const HidTouchState &touchState,
                                   HidAnalogStickState leftJoyStick,
                                   HidAnalogStickState rightJoyStick) {
  if (capturing) {
    u64 keys =
        keysHeld & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L | KEY_R | KEY_ZL |
                    KEY_ZR | KEY_PLUS | KEY_MINUS | KEY_DLEFT | KEY_DUP |
                    KEY_DRIGHT | KEY_DDOWN | KEY_LSTICK | KEY_RSTICK);

    // Cancel capture if B is pressed alone
    if (keys == 0 && (keysDown & KEY_B)) {
      capturing = false;
      holdStartTick = 0;
      capturedKeys = 0;
      this->setNote("Press A to start capture");
      return true;
    }

    if (keys != 0) {
      if (holdStartTick == 0) {
        holdStartTick = armGetSystemTick();
        capturedKeys = keys;
        this->setNote("Capture: " + tsl::hlp::keysToComboString(keys) +
                      " (0.5s)");
      } else {
        if (keys == capturedKeys) {
          u64 diff = armGetSystemTick() - holdStartTick;
          if (armTicksToNs(diff) >= 500000000ULL) { // 0.5 second
            if (capturedKeys == KEY_A) {
              this->setNote("A alone not allowed!");
              return true;
            }

            std::string comboStr = tsl::hlp::keysToComboString(capturedKeys);
            tsl::impl::updateCombo(capturedKeys);

            removeKeyComboFromOthers(comboStr, "");
            tsl::hlp::loadEntryKeyCombos();

            tsl::notification->show("Key Combo Set: " + comboStr);

            std::string displayCombo = comboStr;
            convertComboToUnicode(displayCombo);
            this->setValue(displayCombo);

            capturing = false;
            holdStartTick = 0;
            capturedKeys = 0;
            this->setNote("Press A to start capture");

            // Refresh current menu immediately
            reloadMenu = true;
            tsl::swapTo<UltrahandSettingsMenu>();
            return true;
          } else {
            // Countdown/feedback
            float elapsed = armTicksToNs(diff) / 500000000.0f;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Capture: %s (%.1fs)",
                          tsl::hlp::keysToComboString(capturedKeys).c_str(),
                          0.5f - (elapsed * 0.5f));
            this->setNote(buf);
          }
        } else {
          // Keys changed, reset timer
          holdStartTick = armGetSystemTick();
          capturedKeys = keys;
          this->setNote("Capture: " + tsl::hlp::keysToComboString(keys) +
                        " (0.5s)");
        }
      }
    } else {
      holdStartTick = 0;
      capturedKeys = 0;
      this->setNote("Hold keys for 0.5s");
    }
    // Consume EVERYTHING including B while capturing
    return true;
  }

  if (!this->hasFocus()) {
    holdStartTick = 0;
    capturedKeys = 0;
    capturing = false;
    this->setNote("Press A to start capture");
  }

  return tsl::elm::ListItem::handleInput(keysDown, keysHeld, touchState,
                                         leftJoyStick, rightJoyStick);
}

bool MainComboSetItem::onClick(u64 keys) {
  if (keys & KEY_A) {
    if (!capturing) {
      capturing = true;
      holdStartTick = 0;
      capturedKeys = 0;
      this->setValue("");
      tsl::impl::updateCombo(0);
      this->setNote("Hold keys for 0.5s");
      return true;
    }
  }
  return tsl::elm::ListItem::onClick(keys);
}

int main(int argc, char *argv[]) {
  for (u8 arg = 0; arg < argc; arg++) {
    if (argv[arg][0] != '-')
      continue;

    if (strcmp(argv[arg], "--package") == 0 && arg + 1 < argc) {
      selectedPackage.clear();
      selectedPackage.reserve(64); // Reserve reasonable amount

      for (u8 nextArg = arg + 1; nextArg < argc && argv[nextArg][0] != '-';
           nextArg++) {
        if (!selectedPackage.empty())
          selectedPackage += ' ';
        selectedPackage += argv[nextArg];
        arg = nextArg;
      }
    }
#ifdef EDITCHEAT_OVL
    else if (strcmp(argv[arg], "--cheat_id") == 0 && arg + 1 < argc) {
      g_cheatIdToEdit = std::stoul(argv[++arg]);
    } else if (strcmp(argv[arg], "--cheat_name") == 0 && arg + 1 < argc) {
      g_cheatNameToEdit = argv[++arg];
    } else if (strcmp(argv[arg], "--enabled") == 0 && arg + 1 < argc) {
      g_cheatEnabledToEdit = std::stoi(argv[++arg]) != 0;
    } else if (strcmp(argv[arg], "--focus_cheat_name") == 0 && arg + 1 < argc) {
      g_focusCheatName = argv[++arg];
    } else if (strcmp(argv[arg], "--folder_indices") == 0) {
      while (arg + 1 < argc && argv[arg + 1][0] != '-') {
        g_focusFolderIndices.push_back(std::stoul(argv[++arg]));
      }
    } else if (strcmp(argv[arg], "--folder_names") == 0) {
      while (arg + 1 < argc && argv[arg + 1][0] != '-') {
        g_focusFolderNames.push_back(argv[++arg]);
      }
    }
#else
    else if (strcmp(argv[arg], "--focus_cheat_name") == 0 && arg + 1 < argc) {
      returnJumpItemName = argv[++arg];
    } else if (strcmp(argv[arg], "--folder_indices") == 0) {
      g_cheatFolderIndexStack.clear();
      while (arg + 1 < argc && argv[arg + 1][0] != '-') {
        g_cheatFolderIndexStack.push_back(std::stoul(argv[++arg]));
      }
    } else if (strcmp(argv[arg], "--folder_names") == 0) {
      g_cheatFolderNameStack.clear();
      while (arg + 1 < argc && argv[arg + 1][0] != '-') {
        g_cheatFolderNameStack.push_back(argv[++arg]);
      }
    }
#endif
  }

  // If launched with arguments (manual/forwarder launch), ensure we start
  // visible
  if (argc > 1) {
    ult::setIniFileValue(ult::ULTRAHAND_CONFIG_INI_PATH,
                         ult::ULTRAHAND_PROJECT_NAME, ult::IN_OVERLAY_STR,
                         ult::TRUE_STR);
  }

  // Clear download log
  {
    ult::createDirectory("sdmc:/config/breezehand/");
    FILE *logFile = fopen("sdmc:/config/breezehand/cheat_download.log", "w");
    if (logFile)
      fclose(logFile);
  }

#ifdef EDITCHEAT_OVL
  return tsl::loop<EditCheatOverlay, tsl::impl::LaunchFlags::None>(argc, argv);
#else
  return tsl::loop<Overlay, tsl::impl::LaunchFlags::None>(argc, argv);
#endif
}

// ==========================================
// KEYBOARD IMPLEMENTATION (Merged)
// ==========================================

#include "keyboard.hpp"
#include <algorithm>
#include <switch.h>

namespace {

// Helper classes are internal to this compilation unit.
std::once_flag g_keyboardInitOnce;

void initializePhysicalKeyboardInput() {
  std::call_once(g_keyboardInitOnce, []() {
    hidInitializeKeyboard();
    hidEnableUsbFullKeyController(true);
  });
}

bool isKeyJustPressed(const HidKeyboardState &currentState,
                      const HidKeyboardState &previousState,
                      HidKeyboardKey key) {
  return hidKeyboardStateGetKey(&currentState, key) &&
         !hidKeyboardStateGetKey(&previousState, key);
}

bool isKeyboardShiftActive(const HidKeyboardState &state) {
  return (state.modifiers & HidKeyboardModifier_Shift) != 0;
}

char mapPhysicalKeyToChar(const HidKeyboardState &state, HidKeyboardKey key) {
  const bool shift = isKeyboardShiftActive(state);
  if (key >= HidKeyboardKey_A && key <= HidKeyboardKey_Z) {
    const char letter = static_cast<char>('A' + (key - HidKeyboardKey_A));
    return shift ? letter
                 : static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
  }

  if (key >= HidKeyboardKey_D1 && key <= HidKeyboardKey_D0) {
    static constexpr char digits[] = "1234567890";
    static constexpr char shiftedDigits[] = "!@#$%^&*()";
    const size_t idx = static_cast<size_t>(key - HidKeyboardKey_D1);
    return shift ? shiftedDigits[idx] : digits[idx];
  }

  if (key >= HidKeyboardKey_NumPad1 && key <= HidKeyboardKey_NumPad0) {
    static constexpr char digits[] = "1234567890";
    return digits[key - HidKeyboardKey_NumPad1];
  }

  switch (key) {
  case HidKeyboardKey_Space:
    return ' ';
  case HidKeyboardKey_Tab:
    return '\t';
  case HidKeyboardKey_Minus:
    return shift ? '_' : '-';
  case HidKeyboardKey_Plus:
    return shift ? '+' : '=';
  case HidKeyboardKey_Period:
    return shift ? '>' : '.';
  case HidKeyboardKey_Comma:
    return shift ? '<' : ',';
  case HidKeyboardKey_OpenBracket:
    return shift ? '{' : '[';
  case HidKeyboardKey_CloseBracket:
    return shift ? '}' : ']';
  case HidKeyboardKey_Backslash:
  case HidKeyboardKey_Pipe:
    return shift ? '|' : '\\';
  case HidKeyboardKey_Semicolon:
    return shift ? ':' : ';';
  case HidKeyboardKey_Quote:
    return shift ? '"' : '\'';
  case HidKeyboardKey_Backquote:
  case HidKeyboardKey_Tilde:
    return shift ? '~' : '`';
  case HidKeyboardKey_Slash:
    return shift ? '?' : '/';
  case HidKeyboardKey_NumPadDivide:
    return '/';
  case HidKeyboardKey_NumPadMultiply:
    return '*';
  case HidKeyboardKey_NumPadSubtract:
    return '-';
  case HidKeyboardKey_NumPadAdd:
    return '+';
  case HidKeyboardKey_NumPadDot:
    return '.';
  case HidKeyboardKey_NumPadComma:
  case HidKeyboardKey_NumPadCommaPc98:
    return ',';
  default:
    break;
  }

  return '\0';
}

bool allowsTextCharacter(searchType_t type, const std::string &title, char c) {
  if (c == '\0')
    return false;

  if (type == SEARCH_TYPE_HEX) {
    if (c >= 'a' && c <= 'f')
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || c == ' ';
  }

  if (type == SEARCH_TYPE_TEXT) {
    (void)title;
    // Accept direct physical keyboard text, including symbols not present on the virtual keyboard.
    return (c >= 32 && c <= 126) || c == '\t';
  }

  const bool allowMinus =
      (type == SEARCH_TYPE_SIGNED_8BIT || type == SEARCH_TYPE_SIGNED_16BIT ||
       type == SEARCH_TYPE_SIGNED_32BIT || type == SEARCH_TYPE_SIGNED_64BIT ||
       type == SEARCH_TYPE_FLOAT || type == SEARCH_TYPE_DOUBLE);
  const bool allowDot = (type == SEARCH_TYPE_FLOAT || type == SEARCH_TYPE_DOUBLE);

  if (c >= '0' && c <= '9')
    return true;
  if (allowMinus && c == '-')
    return true;
  if (allowDot && c == '.')
    return true;
  return false;
}

// --- KeyboardFrame ---
class KeyboardFrame : public tsl::elm::OverlayFrame {
public:
  KeyboardFrame(const std::string &title, const std::string &subtitle)
      : tsl::elm::OverlayFrame(title, subtitle) {}

  virtual void layout(u16 parentX, u16 parentY, u16 parentWidth,
                      u16 parentHeight) override {
    setBoundaries(parentX, parentY, parentWidth, parentHeight);

    // We do the heavy lifting in draw() to support dynamic content resizing
    // based on subtitle height, but we set initial bounds here to avoid null
    // rects.
    if (m_contentElement != nullptr) {
      m_contentElement->setBoundaries(parentX + 25, parentY + 115,
                                      parentWidth - 50,
                                      parentHeight - 73 - 110);
      m_contentElement->layout(parentX + 25, parentY + 115, parentWidth - 50,
                               parentHeight - 73 - 110);
      m_contentElement->invalidate();
    }
  }

  virtual void draw(tsl::gfx::Renderer *renderer) override {
    // 1. Temporarily clear content and subtitle so base draw doesn't render
    // them
    tsl::elm::Element *content = m_contentElement;
    std::string sub = m_subtitle;
    m_contentElement = nullptr;
    m_subtitle = "";

    // 2. Call base draw (background, title, buttons)
    tsl::elm::OverlayFrame::draw(renderer);

    // 3. Restore
    m_contentElement = content;
    m_subtitle = sub;

    // 4. Draw Custom Wrapped Subtitle
    s32 startY = this->getY() + 75;
    s32 effectiveHeight = 0;

    if (!m_subtitle.empty()) {
      // Manually draw subtitle with wrapping.
      // Note: WrapNote logic is already applied in m_subtitle (contains \n) if
      // passed from KeyboardGui But Renderer::drawString ignores \n usually. We
      // need to split manually.

      std::stringstream ss(m_subtitle);
      std::string line;
      int yOffset = 0;
      while (std::getline(ss, line, '\n')) {
        renderer->drawString(line.c_str(), false, this->getX() + 20,
                             startY + yOffset, 16,
                             tsl::style::color::ColorText);
        yOffset += 20;
      }
      effectiveHeight = yOffset;
    }

    // 5. Draw Content with dynamic adjustment
    if (m_contentElement != nullptr) {
      // Adjust Y based on subtitle height. Base reserved space was ~something.
      // We want content to start below subtitle.
      s32 contentY = startY + effectiveHeight + 10;
      // Clamp contentY to minimum 115 to match original look if no subtitle
      if (contentY < this->getY() + 115)
        contentY = this->getY() + 115;

      s32 availableHeight = (this->getY() + this->getHeight() - 73) - contentY;

      // Only re-layout if boundaries changed significantly (to avoid spamming
      // layout)
      if (m_contentElement->getY() != contentY ||
          m_contentElement->getHeight() != availableHeight) {
        m_contentElement->setBoundaries(this->getX() + 25, contentY,
                                        this->getWidth() - 50, availableHeight);
        m_contentElement->layout(this->getX() + 25, contentY,
                                 this->getWidth() - 50, availableHeight);
        // m_contentElement->invalidate(); // Avoid full invalidate in draw
      }

      m_contentElement->frame(renderer);
    }

    // 6. Keyboard-specific footer: show only "X Cancel" and "A Select".
    const s32 footerY = tsl::cfg::FramebufferHeight - 73;
    renderer->drawRect(15, footerY + 1, tsl::cfg::FramebufferWidth - 30, 72,
                       a(tsl::defaultBackgroundColor));
    renderer->drawRect(15, footerY, tsl::cfg::FramebufferWidth - 30, 1,
                       a(tsl::bottomSeparatorColor));

    const std::string bottomLine =
        std::string("\uE0E2") + ult::GAP_2 + "Cancel" + ult::GAP_1 +
        std::string("\uE0E0") + ult::GAP_2 + "Select";
    static const std::vector<std::string> specialChars = {"\uE0E2", "\uE0E0"};
    renderer->drawStringWithColoredSections(bottomLine, false, specialChars, 30,
                                            693, 23, tsl::bottomTextColor,
                                            tsl::buttonColor);
  }
};

// --- KeyboardButton ---
class KeyboardButton : public tsl::elm::Element {
public:
  KeyboardButton(char c, std::function<void(char)> onClick, u8 fontSize = 25)
      : m_char(c), m_label(1, c), m_fontSize(fontSize), m_onClick(onClick) {
    m_isItem = true;
  }

  KeyboardButton(const std::string &label, std::function<void()> onClickAction,
                 u8 fontSize = 25)
      : m_char(0), m_label(label), m_fontSize(fontSize),
        m_onClickAction(onClickAction) {
    ult::convertComboToUnicode(m_label);
    m_isItem = true;
  }

  void setCharacter(char c) {
    m_char = c;
    m_label.assign(1, c);
  }

  virtual s32 getHeight() override { return 60; }

  virtual void draw(tsl::gfx::Renderer *renderer) override {
    auto color = m_focused ? tsl::style::color::ColorHighlight
                           : tsl::style::color::ColorText;
    if (m_focused) {
      renderer->drawRoundedRect(this->getX(), this->getY(), this->getWidth(),
                                this->getHeight(), 8.0f,
                                a(tsl::style::color::ColorClickAnimation));
    }
    renderer->drawRect(this->getX(), this->getY(), this->getWidth(),
                       this->getHeight(), a(tsl::style::color::ColorFrame));

    const s32 textW = renderer->getTextDimensions(m_label, false, m_fontSize).first;
    s32 textX = this->getX() + (this->getWidth() - textW) / 2;
    s32 textY = this->getY() + (this->getHeight() / 2) + (m_fontSize / 2) - 1;
    renderer->drawString(m_label.c_str(), false, textX, textY, m_fontSize, a(color));
  }

  virtual void layout(u16 parentX, u16 parentY, u16 parentWidth,
                      u16 parentHeight) override {
    setBoundaries(parentX, parentY, parentWidth, parentHeight);
  }

  virtual inline tsl::elm::Element *
  requestFocus(tsl::elm::Element *oldFocus,
               tsl::FocusDirection direction) override {
    return this;
  }

  virtual inline bool onClick(u64 keys) override {
    if (keys & KEY_A) {
      if (m_onClick)
        m_onClick(m_char);
      else if (m_onClickAction)
        m_onClickAction();
      return true;
    }
    return false;
  }

  virtual bool onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY,
                       s32 prevX, s32 prevY, s32 initialX,
                       s32 initialY) override {
    if (event == tsl::elm::TouchEvent::Touch) {
      if (inBounds(currX, currY))
        return true;
    }
    if (event == tsl::elm::TouchEvent::Release) {
      if (inBounds(currX, currY) && inBounds(initialX, initialY)) {
        this->triggerClickAnimation();
        this->onClick(KEY_A);
        return true;
      }
    }
    return false;
  }

private:
  char m_char;
  std::string m_label;
  u8 m_fontSize = 25;
  std::function<void(char)> m_onClick;
  std::function<void()> m_onClickAction;
};

// --- KeyboardRow ---
class KeyboardRow : public tsl::elm::Element {
public:
  KeyboardRow() { m_isItem = true; }
  virtual ~KeyboardRow() {
    for (auto *btn : m_buttons)
      delete btn;
  }

  void addButton(KeyboardButton *btn) {
    btn->setParent(this);
    m_buttons.push_back(btn);
  }

  virtual s32 getHeight() override { return 60; }

  virtual void draw(tsl::gfx::Renderer *renderer) override {
    this->layout(this->getX(), this->getY(), this->getWidth(),
                 this->getHeight());
    for (auto *btn : m_buttons)
      btn->frame(renderer);
  }

  virtual void layout(u16 parentX, u16 parentY, u16 parentWidth,
                      u16 parentHeight) override {
    if (m_buttons.empty())
      return;

    u16 btnWidth = this->getWidth() / m_buttons.size();
    u16 btnHeight = 60;
    u16 yOffset = (this->getHeight() - btnHeight) / 2;

    for (size_t i = 0; i < m_buttons.size(); ++i) {
      u16 width = (i == m_buttons.size() - 1)
                      ? (this->getWidth() - i * btnWidth)
                      : btnWidth;
      m_buttons[i]->layout(parentX + i * btnWidth, parentY + yOffset, width,
                           btnHeight);
    }
  }

  virtual inline tsl::elm::Element *
  requestFocus(tsl::elm::Element *oldFocus,
               tsl::FocusDirection direction) override {
    if (m_buttons.empty())
      return nullptr;

    if (oldFocus && (direction == tsl::FocusDirection::Up ||
                     direction == tsl::FocusDirection::Down)) {
      s32 targetX = oldFocus->getX() + (oldFocus->getWidth() / 2);
      auto it = std::min_element(
          m_buttons.begin(), m_buttons.end(),
          [targetX](tsl::elm::Element *a, tsl::elm::Element *b) {
            return std::abs(a->getX() + (s32)(a->getWidth() / 2) - targetX) <
                   std::abs(b->getX() + (s32)(b->getWidth() / 2) - targetX);
          });
      return (*it)->requestFocus(oldFocus, direction);
    }

    if (oldFocus && (direction == tsl::FocusDirection::Left ||
                     direction == tsl::FocusDirection::Right)) {
      for (size_t i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons[i] == oldFocus) {
          m_lastFocusedIndex = i;
          if (direction == tsl::FocusDirection::Left) {
            if (i > 0)
              return m_buttons[i - 1]->requestFocus(oldFocus, direction);
          } else {
            if (i < m_buttons.size() - 1)
              return m_buttons[i + 1]->requestFocus(oldFocus, direction);
          }
          return oldFocus;
        }
      }
    }

    if (direction == tsl::FocusDirection::Left)
      return m_buttons.back()->requestFocus(oldFocus, direction);
    if (direction == tsl::FocusDirection::Right)
      return m_buttons.front()->requestFocus(oldFocus, direction);

    // Default focus (None or from vertical/other)
    if (m_lastFocusedIndex < m_buttons.size()) {
      return m_buttons[m_lastFocusedIndex]->requestFocus(oldFocus, direction);
    }
    return m_buttons.front()->requestFocus(oldFocus, direction);
  }

  virtual bool onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY,
                       s32 prevX, s32 prevY, s32 initialX,
                       s32 initialY) override {
    for (size_t i = 0; i < m_buttons.size(); ++i) {
      if (m_buttons[i]->onTouch(event, currX, currY, prevX, prevY, initialX,
                                initialY)) {
        if (event == tsl::elm::TouchEvent::Touch ||
            event == tsl::elm::TouchEvent::Release) {
          m_lastFocusedIndex = i;
        }
        return true;
      }
    }
    return false;
  }

private:
  size_t m_lastFocusedIndex = 0;
  std::vector<KeyboardButton *> m_buttons;
};

// --- ValueDisplay ---
class ValueDisplay : public tsl::elm::Element {
public:
  ValueDisplay(tsl::KeyboardGui *gui, const std::string &title,
               std::string &value, size_t &cursorPos)
      : m_gui(gui), m_title(title), m_value(value), m_cursorPos(cursorPos) {
    m_isItem = true;
  }

  virtual s32 getHeight() override { return 70; }

  virtual void draw(tsl::gfx::Renderer *renderer) override {
    std::string val;
    size_t pos;
    {
      std::lock_guard<std::recursive_mutex> lock(m_gui->getMutex());
      val = m_value;
      pos = m_cursorPos;
    }

    renderer->drawRect(this->getX(), this->getY(), this->getWidth(),
                       this->getHeight(), a(tsl::style::color::ColorFrame));

    s32 maxW = this->getWidth() - 30;
    int effectiveSize = m_fontSize;

    // Auto-reduce font size if text is too wide
    while (
        effectiveSize > 10 &&
        renderer->getTextDimensions(val.c_str(), false, effectiveSize).first >
            maxW) {
      effectiveSize--;
    }

    s32 textY = this->getY() + (this->getHeight() / 2) +
                (effectiveSize / 2); // Dynamic centering using effective size

    // Removing title rendering per user request
    // renderer->drawString(displayTitle.c_str(), false, this->getX() + 15,
    // textY, 25, a(tsl::style::color::ColorText));

    // Draw Value with effective size
    renderer->drawString(val.c_str(), false, this->getX() + 15, textY,
                         effectiveSize, a(tsl::style::color::ColorText));

    // Cursor logic
    s32 prefixWidth = renderer
                          ->getTextDimensions(val.substr(0, pos).c_str(), false,
                                              effectiveSize)
                          .first;
    s32 cursorX = this->getX() + 15 + prefixWidth;

    if (m_gui->isOvertypeMode()) {
      // Underline Cursor for Overtype (User request: lighten/underline to see
      // text)
      s32 charWidth = 12; // Fallback default
      if (pos < val.length()) {
        charWidth = renderer
                        ->getTextDimensions(val.substr(pos, 1).c_str(), false,
                                            effectiveSize)
                        .first;
      }
      // Draw underline below the text position
      // textY is centered baseline-ish. Let's position underline just below it.
      s32 underlineY = textY + (effectiveSize / 2) + 2;
      // Ensure it doesn't go out of bounds of the element
      if (underlineY > this->getY() + this->getHeight() - 5)
        underlineY = this->getY() + this->getHeight() - 5;

      renderer->drawRect(cursorX, underlineY, charWidth, 3,
                         a(tsl::style::color::ColorHighlight));
    } else {
      // Line Cursor for Insert
      renderer->drawRect(cursorX, this->getY() + 15, 2, effectiveSize + 15,
                         a(tsl::style::color::ColorHighlight));
    }
  }

  void changeFontSize(int delta) {
    m_fontSize += delta;
    if (m_fontSize < 10)
      m_fontSize = 10;
    if (m_fontSize > 60)
      m_fontSize = 60;
  }

  virtual void layout(u16 parentX, u16 parentY, u16 parentWidth,
                      u16 parentHeight) override {
    setBoundaries(parentX, parentY, parentWidth, parentHeight);
  }

  virtual inline tsl::elm::Element *
  requestFocus(tsl::elm::Element *oldFocus,
               tsl::FocusDirection direction) override {
    return nullptr;
  }

private:
  tsl::KeyboardGui *m_gui;
  std::string m_title;
  std::string &m_value;
  size_t &m_cursorPos;
  int m_fontSize = 25; // Default font size
};
} // namespace

namespace tsl {

// --- KeyboardGui ---
KeyboardGui::KeyboardGui(searchType_t type, const std::string &initialValue,
                         const std::string &title,
                         std::function<void(std::string)> onComplete,
                         std::function<std::string(std::string&, size_t&)> onNoteUpdate,
                         bool constrained,
                         std::function<std::string(std::string&, size_t&)> onGetSignedEditValue,
                         std::function<std::string(std::string&, size_t&)> onGetUnsignedEditValue,
                         std::function<std::string(std::string&, size_t&)> onGetFloatEditValue,
                         std::function<bool(std::string&, size_t&, const std::string&)> onApplySignedEdit,
                         std::function<bool(std::string&, size_t&, const std::string&)> onApplyUnsignedEdit,
                         std::function<bool(std::string&, size_t&, const std::string&)> onApplyFloatEdit,
                         std::function<std::string(std::string&, size_t&)> onGetAsmEditValue,
                         std::function<bool(std::string&, size_t&, const std::string&)> onApplyAsmEdit,
                         std::function<bool(std::string&, size_t&)> onClearStoredValue,
                         std::function<u32(std::string&, size_t&)> onGetCodeType,
                         std::function<bool(std::string&, size_t&, u32, u64)> onApplyComboType,
                         std::function<bool(std::string&, size_t&, u32)> onSetComboCodeType,
                         std::function<bool(std::string&, size_t&)> onToggleC4AutoRepeat)
    : m_type(type), m_value(initialValue), m_title(title),
      m_onComplete(onComplete), m_onNoteUpdate(onNoteUpdate),
      m_onGetSignedEditValue(onGetSignedEditValue),
      m_onGetUnsignedEditValue(onGetUnsignedEditValue),
      m_onGetFloatEditValue(onGetFloatEditValue),
      m_onApplySignedEdit(onApplySignedEdit),
      m_onApplyUnsignedEdit(onApplyUnsignedEdit),
      m_onApplyFloatEdit(onApplyFloatEdit),
      m_onGetAsmEditValue(onGetAsmEditValue),
      m_onApplyAsmEdit(onApplyAsmEdit),
      m_onClearStoredValue(onClearStoredValue),
      m_onGetCodeType(onGetCodeType),
      m_onApplyComboType(onApplyComboType),
      m_onSetComboCodeType(onSetComboCodeType),
      m_onToggleC4AutoRepeat(onToggleC4AutoRepeat),
      m_isConstrained(constrained) {
  initializePhysicalKeyboardInput();
  m_isNumpad = (type != SEARCH_TYPE_POINTER && type != SEARCH_TYPE_NONE);
  m_cursorPos = m_value.length();
  tsl::disableJumpTo = true;
}

KeyboardGui::~KeyboardGui() { tsl::disableJumpTo = false; }

elm::Element *KeyboardGui::createUI() {
  m_onToggleCapsVisual = nullptr;

  std::string initialNote = "";
  if (m_onNoteUpdate)
    initialNote = m_onNoteUpdate(m_value, m_cursorPos);
  auto *frame = new KeyboardFrame(m_title, initialNote);
  m_frame = frame;
  auto *list = new elm::List();

  auto *valItem = new ValueDisplay(this, "", m_value, m_cursorPos);
  m_valueDisplay = valItem;
  list->addItem(valItem);

  auto keyPress = [this](char c) { this->handleKeyPress(c); };

  if (m_type == SEARCH_TYPE_NONE) {
    auto *typeRow1 = new KeyboardRow();
    typeRow1->addButton(new KeyboardButton(
        "U8", [this] { this->switchType(SEARCH_TYPE_UNSIGNED_8BIT); }));
    typeRow1->addButton(new KeyboardButton(
        "U16", [this] { this->switchType(SEARCH_TYPE_UNSIGNED_16BIT); }));
    typeRow1->addButton(new KeyboardButton(
        "U32", [this] { this->switchType(SEARCH_TYPE_UNSIGNED_32BIT); }));
    typeRow1->addButton(new KeyboardButton(
        "U64", [this] { this->switchType(SEARCH_TYPE_UNSIGNED_64BIT); }));
    list->addItem(typeRow1);
  }

  if (m_type == SEARCH_TYPE_HEX) {
    u32 currentCodeType = 0xFFFFFFFF;
    if (m_onGetCodeType) {
      std::lock_guard<std::recursive_mutex> lock(m_mutex);
      currentCodeType = m_onGetCodeType(m_value, m_cursorPos);
    }
    const bool isType8OrC4 = (currentCodeType == 0x8 || currentCodeType == 0xC4);

    if (isType8OrC4) {
      m_comboCaptureTargetType = currentCodeType;
      auto *comboRow = new KeyboardRow();
      comboRow->addButton(new KeyboardButton("type 8", [this] {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_comboCaptureTargetType = 0x8;
        if (m_onSetComboCodeType &&
            m_onSetComboCodeType(m_value, m_cursorPos, 0x8) &&
            m_onNoteUpdate && m_frame) {
          m_frame->setSubtitle(m_onNoteUpdate(m_value, m_cursorPos));
        }
      }));
      comboRow->addButton(new KeyboardButton("type C4", [this] {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_comboCaptureTargetType = 0xC4;
        if (m_onSetComboCodeType &&
            m_onSetComboCodeType(m_value, m_cursorPos, 0xC4) &&
            m_onNoteUpdate && m_frame) {
          m_frame->setSubtitle(m_onNoteUpdate(m_value, m_cursorPos));
        }
      }));
      comboRow->addButton(new KeyboardButton("repeat", [this] {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_comboCaptureTargetType != 0xC4) {
          return;
        }
        if (m_onToggleC4AutoRepeat &&
            m_onToggleC4AutoRepeat(m_value, m_cursorPos) &&
            m_onNoteUpdate && m_frame) {
          m_frame->setSubtitle(m_onNoteUpdate(m_value, m_cursorPos));
        }
      }));
      list->addItem(comboRow);
    }

    if (!isType8OrC4 &&
        (m_onGetSignedEditValue || m_onGetUnsignedEditValue || m_onGetFloatEditValue || m_onGetAsmEditValue || m_onClearStoredValue)) {
      auto *topRow = new KeyboardRow();
      topRow->addButton(new KeyboardButton("[s]", [this] {
        if (!m_onGetSignedEditValue || !m_onApplySignedEdit) return;
        std::string initial = "0";
        {
          std::lock_guard<std::recursive_mutex> lock(m_mutex);
          initial = m_onGetSignedEditValue(m_value, m_cursorPos);
        }
        tsl::changeTo<tsl::KeyboardGui>(
            SEARCH_TYPE_SIGNED_64BIT, initial, "Edit Signed",
            [this](std::string result) {
              std::lock_guard<std::recursive_mutex> lock(m_mutex);
              if (m_onApplySignedEdit &&
                  m_onApplySignedEdit(m_value, m_cursorPos, result) &&
                  m_onNoteUpdate && m_frame) {
                if (m_isConstrained) {
                  m_frame->setSubtitle(m_onNoteUpdate(m_value, m_cursorPos));
                } else {
                  std::string valCopy = m_value;
                  size_t cursorCopy = m_cursorPos;
                  m_frame->setSubtitle(m_onNoteUpdate(valCopy, cursorCopy));
                }
              }
              tsl::goBack();
            },
            nullptr, false);
      }));
      topRow->addButton(new KeyboardButton("[u]", [this] {
        if (!m_onGetUnsignedEditValue || !m_onApplyUnsignedEdit) return;
        std::string initial = "0";
        {
          std::lock_guard<std::recursive_mutex> lock(m_mutex);
          initial = m_onGetUnsignedEditValue(m_value, m_cursorPos);
        }
        tsl::changeTo<tsl::KeyboardGui>(
            SEARCH_TYPE_UNSIGNED_64BIT, initial, "Edit Unsigned",
            [this](std::string result) {
              std::lock_guard<std::recursive_mutex> lock(m_mutex);
              if (m_onApplyUnsignedEdit &&
                  m_onApplyUnsignedEdit(m_value, m_cursorPos, result) &&
                  m_onNoteUpdate && m_frame) {
                if (m_isConstrained) {
                  m_frame->setSubtitle(m_onNoteUpdate(m_value, m_cursorPos));
                } else {
                  std::string valCopy = m_value;
                  size_t cursorCopy = m_cursorPos;
                  m_frame->setSubtitle(m_onNoteUpdate(valCopy, cursorCopy));
                }
              }
              tsl::goBack();
            },
            nullptr, false);
      }));
      topRow->addButton(new KeyboardButton("float", [this] {
        if (!m_onGetFloatEditValue || !m_onApplyFloatEdit) return;
        std::string initial = "0";
        {
          std::lock_guard<std::recursive_mutex> lock(m_mutex);
          initial = m_onGetFloatEditValue(m_value, m_cursorPos);
        }
        tsl::changeTo<tsl::KeyboardGui>(
            SEARCH_TYPE_DOUBLE, initial, "Edit Float",
            [this](std::string result) {
              std::lock_guard<std::recursive_mutex> lock(m_mutex);
              if (m_onApplyFloatEdit &&
                  m_onApplyFloatEdit(m_value, m_cursorPos, result) &&
                  m_onNoteUpdate && m_frame) {
                if (m_isConstrained) {
                  m_frame->setSubtitle(m_onNoteUpdate(m_value, m_cursorPos));
                } else {
                  std::string valCopy = m_value;
                  size_t cursorCopy = m_cursorPos;
                  m_frame->setSubtitle(m_onNoteUpdate(valCopy, cursorCopy));
                }
              }
              tsl::goBack();
            },
            nullptr, false);
      }));
      topRow->addButton(new KeyboardButton("ASM", [this] {
        if (!m_onGetAsmEditValue || !m_onApplyAsmEdit) return;
#if HAVE_KEYSTONE_ASM
        if (!checkOverlayMemory(6)) {
          return;
        }
#endif
        std::string initial;
        {
          std::lock_guard<std::recursive_mutex> lock(m_mutex);
          initial = m_onGetAsmEditValue(m_value, m_cursorPos);
        }
        if (initial.empty()) {
          tsl::notification->show("ASM edit: width must be 4");
          return;
        }
        tsl::changeTo<tsl::KeyboardGui>(
            SEARCH_TYPE_TEXT, initial, "Edit ASM",
            [this](std::string result) {
              std::lock_guard<std::recursive_mutex> lock(m_mutex);
              if (!m_onApplyAsmEdit ||
                  !m_onApplyAsmEdit(m_value, m_cursorPos, result)) {
                tsl::notification->show("ASM assemble failed");
                return;
              }
              if (m_onNoteUpdate && m_frame) {
                if (m_isConstrained) {
                  m_frame->setSubtitle(m_onNoteUpdate(m_value, m_cursorPos));
                } else {
                  std::string valCopy = m_value;
                  size_t cursorCopy = m_cursorPos;
                  m_frame->setSubtitle(m_onNoteUpdate(valCopy, cursorCopy));
                }
              }
              tsl::goBack();
            },
            nullptr, false);
      }));
      topRow->addButton(new KeyboardButton("AC", [this] {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_onClearStoredValue &&
            m_onClearStoredValue(m_value, m_cursorPos) &&
            m_onNoteUpdate && m_frame) {
          if (m_isConstrained) {
            m_frame->setSubtitle(m_onNoteUpdate(m_value, m_cursorPos));
          } else {
            std::string valCopy = m_value;
            size_t cursorCopy = m_cursorPos;
            m_frame->setSubtitle(m_onNoteUpdate(valCopy, cursorCopy));
          }
        }
      }));
      list->addItem(topRow);
    }

    auto *row1 = new KeyboardRow();
    row1->addButton(new KeyboardButton('1', keyPress));
    row1->addButton(new KeyboardButton('2', keyPress));
    row1->addButton(new KeyboardButton('3', keyPress));
    row1->addButton(new KeyboardButton('A', keyPress));
    list->addItem(row1);

    auto *row2 = new KeyboardRow();
    row2->addButton(new KeyboardButton('4', keyPress));
    row2->addButton(new KeyboardButton('5', keyPress));
    row2->addButton(new KeyboardButton('6', keyPress));
    row2->addButton(new KeyboardButton('B', keyPress));
    list->addItem(row2);

    auto *row3 = new KeyboardRow();
    row3->addButton(new KeyboardButton('7', keyPress));
    row3->addButton(new KeyboardButton('8', keyPress));
    row3->addButton(new KeyboardButton('9', keyPress));
    row3->addButton(new KeyboardButton('C', keyPress));
    list->addItem(row3);

    auto *row4 = new KeyboardRow();
    row4->addButton(new KeyboardButton('0', keyPress));
    row4->addButton(new KeyboardButton('D', keyPress));
    row4->addButton(new KeyboardButton('E', keyPress));
    row4->addButton(new KeyboardButton('F', keyPress));
    list->addItem(row4);

    auto *row5 = new KeyboardRow();
    row5->addButton(
        new KeyboardButton("BS \uE0E1", [this] { this->handleBackspace(); }));
    row5->addButton(
        new KeyboardButton("SPACE", [this] { this->handleKeyPress(' '); }));

    // Toggle Insert/Overtype
    row5->addButton(new KeyboardButton("INS", [this] {
      this->toggleManualOvertype();
      // Force redraw
      if (m_valueDisplay)
        m_valueDisplay->invalidate();
    }));

    row5->addButton(
        new KeyboardButton("OK \uE0F1", [this] { this->handleConfirm(); }));
    list->addItem(row5);

    if (isType8OrC4) {
      auto *comboBox = new tsl::elm::ListItem("Combo key programming");
      comboBox->setUseWrapping(true);
      comboBox->setAlwaysShowNote(true);
      comboBox->setNote("Press A then hold combo keys for 0.5s");
      comboBox->setClickListener([this](u64 keys) {
        if (keys & KEY_A) {
          m_comboCaptureActive = true;
          m_comboCaptureArmedTick = armGetSystemTick();
          m_comboCaptureStartTick = 0;
          m_comboCapturedKeys = 0;
          tsl::notification->show("Hold combo for 0.5s");
          return true;
        }
        return false;
      });
      list->addItem(comboBox);
    }
  } else if (m_type == SEARCH_TYPE_TEXT) {
    const bool isAsmKeyboard = (m_title == "Edit ASM");
    // Row 1: Numbers
    auto *row1 = new KeyboardRow();
    for (char c = '1'; c <= '9'; c++)
      row1->addButton(new KeyboardButton(c, keyPress));
    row1->addButton(new KeyboardButton('0', keyPress));
    list->addItem(row1);

    std::vector<std::pair<KeyboardButton *, char>> letterButtons;

    // Row 2: QWERTYUIOP
    auto *row2 = new KeyboardRow();
    const std::string r2 = "qwertyuiop";
    for (char c : r2) {
      const char display = m_capsMode
                               ? static_cast<char>(
                                     std::toupper(static_cast<unsigned char>(c)))
                               : c;
      auto *btn = new KeyboardButton(display, keyPress);
      letterButtons.emplace_back(btn, c);
      row2->addButton(btn);
    }
    list->addItem(row2);

    // Row 3: ASDFGHJKL
    auto *row3 = new KeyboardRow();
    const std::string r3 = "asdfghjkl";
    for (char c : r3) {
      const char display = m_capsMode
                               ? static_cast<char>(
                                     std::toupper(static_cast<unsigned char>(c)))
                               : c;
      auto *btn = new KeyboardButton(display, keyPress);
      letterButtons.emplace_back(btn, c);
      row3->addButton(btn);
    }
    list->addItem(row3);

    // Row 4: ZXCVBNM
    auto *row4 = new KeyboardRow();
    const std::string r4 = "zxcvbnm";
    for (char c : r4) {
      const char display = m_capsMode
                               ? static_cast<char>(
                                     std::toupper(static_cast<unsigned char>(c)))
                               : c;
      auto *btn = new KeyboardButton(display, keyPress);
      letterButtons.emplace_back(btn, c);
      row4->addButton(btn);
    }
    list->addItem(row4);

    m_onToggleCapsVisual = [this, letterButtons] {
      m_capsMode = !m_capsMode;
      for (const auto &[btn, baseLower] : letterButtons) {
        if (!btn)
          continue;
        const char nextChar =
            m_capsMode
                ? static_cast<char>(
                      std::toupper(static_cast<unsigned char>(baseLower)))
                : baseLower;
        btn->setCharacter(nextChar);
      }
      if (m_frame)
        m_frame->invalidate();
      if (m_valueDisplay)
        m_valueDisplay->invalidate();
    };

    // Row 5: Action Keys (part 1)
    auto *row5 = new KeyboardRow();
    row5->addButton(new KeyboardButton("Cap \uE104", [this] {
      if (m_onToggleCapsVisual) {
        m_onToggleCapsVisual();
      } else {
        m_capsMode = !m_capsMode;
      }
    }, 20));
    row5->addButton(
        new KeyboardButton("BS \uE0E1", [this] { this->handleBackspace(); }, 20));
    if (isAsmKeyboard) {
      row5->addButton(
          new KeyboardButton(",", [this] { this->handleKeyPress(','); }, 20));
      row5->addButton(
          new KeyboardButton("[", [this] { this->handleKeyPress('['); }, 20));
      row5->addButton(
          new KeyboardButton("]", [this] { this->handleKeyPress(']'); }, 20));
      row5->addButton(
          new KeyboardButton("#", [this] { this->handleKeyPress('#'); }, 20));
    } else {
      row5->addButton(
          new KeyboardButton("-", [this] { this->handleKeyPress('-'); }, 20));
      row5->addButton(
          new KeyboardButton(".", [this] { this->handleKeyPress('.'); }, 20));
    }
    list->addItem(row5);

    // Row 6: Action Keys (part 2)
    auto *row6 = new KeyboardRow();
    if (isAsmKeyboard) {
      row6->addButton(
          new KeyboardButton("-", [this] { this->handleKeyPress('-'); }, 20));
      row6->addButton(
          new KeyboardButton(".", [this] { this->handleKeyPress('.'); }, 20));
    }
    row6->addButton(
        new KeyboardButton("SP \uE0E3", [this] { this->handleKeyPress(' '); }, 20));
    row6->addButton(
        new KeyboardButton("OK \uE0F1", [this] { this->handleConfirm(); }, 20));
    list->addItem(row6);
  } else if (m_isNumpad) {
    const bool allowMinus =
        (m_type == SEARCH_TYPE_SIGNED_8BIT || m_type == SEARCH_TYPE_SIGNED_16BIT ||
         m_type == SEARCH_TYPE_SIGNED_32BIT || m_type == SEARCH_TYPE_SIGNED_64BIT ||
         m_type == SEARCH_TYPE_FLOAT || m_type == SEARCH_TYPE_DOUBLE);
    const bool allowDot =
        (m_type == SEARCH_TYPE_FLOAT || m_type == SEARCH_TYPE_DOUBLE);

    auto *row1 = new KeyboardRow();
    row1->addButton(new KeyboardButton('1', keyPress));
    row1->addButton(new KeyboardButton('2', keyPress));
    row1->addButton(new KeyboardButton('3', keyPress));
    list->addItem(row1);

    auto *row2 = new KeyboardRow();
    row2->addButton(new KeyboardButton('4', keyPress));
    row2->addButton(new KeyboardButton('5', keyPress));
    row2->addButton(new KeyboardButton('6', keyPress));
    list->addItem(row2);

    auto *row3 = new KeyboardRow();
    row3->addButton(new KeyboardButton('7', keyPress));
    row3->addButton(new KeyboardButton('8', keyPress));
    row3->addButton(new KeyboardButton('9', keyPress));
    list->addItem(row3);

    auto *row4 = new KeyboardRow();
    row4->addButton(
        new KeyboardButton("BS \uE0E1", [this] { this->handleBackspace(); }));
    if (allowMinus)
      row4->addButton(
          new KeyboardButton("-", [this] { this->handleKeyPress('-'); }));
    row4->addButton(new KeyboardButton('0', keyPress));
    if (allowDot)
      row4->addButton(
          new KeyboardButton(".", [this] { this->handleKeyPress('.'); }));
    row4->addButton(
        new KeyboardButton("OK \uE0F1", [this] { this->handleConfirm(); }));
    list->addItem(row4);
  } else {
    // HEX logic moved up
    {
      auto *row1 = new KeyboardRow();
      for (char c : std::string("1234567890"))
        row1->addButton(new KeyboardButton(c, keyPress));
      list->addItem(row1);

      auto *row2 = new KeyboardRow();
      for (char c : std::string("QWERTYUIOP"))
        row2->addButton(new KeyboardButton(c, keyPress));
      list->addItem(row2);

      auto *row3 = new KeyboardRow();
      for (char c : std::string("ASDFGHJKL"))
        row3->addButton(new KeyboardButton(c, keyPress));
      list->addItem(row3);

      auto *row4 = new KeyboardRow();
      row4->addButton(
          new KeyboardButton("BS \uE0E1", [this] { this->handleBackspace(); }));
      for (char c : std::string("ZXCVBNM"))
        row4->addButton(new KeyboardButton(c, keyPress));
      row4->addButton(
          new KeyboardButton("SPACE", [this] { this->handleKeyPress(' '); }));
      row4->addButton(
          new KeyboardButton("OK \uE0F1", [this] { this->handleConfirm(); }));
      list->addItem(row4);
    }
  }

  frame->setContent(list);
  return frame;
}

void KeyboardGui::update() {}

bool KeyboardGui::handleInput(u64 keysDown, u64 keysHeld,
                              const HidTouchState &touchPos,
                              HidAnalogStickState leftJoyStick,
                              HidAnalogStickState rightJoyStick) {
  // Enter is globally mapped to KEY_A for menu navigation.
  // In KeyboardGui:
  // - plain Enter should keep acting as KEY_A
  // - NumPadEnter and Shift+Enter should be reserved for "end edit"
  {
    HidKeyboardState currentKeyboardState{};
    if (hidGetKeyboardStates(&currentKeyboardState, 1) > 0) {
      const bool shiftHeld = (currentKeyboardState.modifiers & HidKeyboardModifier_Shift) != 0;
      const bool mainEnterHeld =
          hidKeyboardStateGetKey(&currentKeyboardState, HidKeyboardKey_Return);
      const bool numpadEnterHeld =
          hidKeyboardStateGetKey(&currentKeyboardState, HidKeyboardKey_NumPadEnter);
      if (numpadEnterHeld || (mainEnterHeld && shiftHeld)) {
        keysDown &= ~KEY_A;
        keysHeld &= ~KEY_A;
      }
    }
  }

  if (handlePhysicalKeyboardInput())
    return true;

  if (m_comboCaptureActive) {
    constexpr u64 captureMask =
        KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L | KEY_R | KEY_ZL | KEY_ZR |
        KEY_PLUS | KEY_MINUS | KEY_DLEFT | KEY_DUP | KEY_DRIGHT | KEY_DDOWN |
        KEY_LSTICK | KEY_RSTICK;
    const u64 comboKeys = keysHeld & captureMask;

    if (comboKeys != 0) {
      if (m_comboCaptureStartTick == 0 || comboKeys != m_comboCapturedKeys) {
        m_comboCaptureStartTick = armGetSystemTick();
        m_comboCapturedKeys = comboKeys;
      } else {
        const u64 diff = armGetSystemTick() - m_comboCaptureStartTick;
        if (armTicksToNs(diff) >= 500000000ULL) {
          bool applied = false;
          {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (m_onApplyComboType) {
              applied = m_onApplyComboType(m_value, m_cursorPos,
                                           m_comboCaptureTargetType,
                                           m_comboCapturedKeys);
            }
            if (applied && m_onNoteUpdate && m_frame) {
              if (m_isConstrained) {
                m_frame->setSubtitle(m_onNoteUpdate(m_value, m_cursorPos));
              } else {
                std::string valCopy = m_value;
                size_t cursorCopy = m_cursorPos;
                m_frame->setSubtitle(m_onNoteUpdate(valCopy, cursorCopy));
              }
            }
          }
          if (applied) {
            if (m_valueDisplay) m_valueDisplay->invalidate();
          } else {
            tsl::notification->show("Combo apply failed");
          }
          m_comboCaptureActive = false;
          m_comboCaptureArmedTick = 0;
          m_comboCaptureStartTick = 0;
          m_comboCapturedKeys = 0;
        }
      }
    } else {
      const u64 idleElapsed =
          (m_comboCaptureArmedTick == 0)
              ? 0
              : armTicksToNs(armGetSystemTick() - m_comboCaptureArmedTick);
      if (idleElapsed >= 1500000000ULL) {
        m_comboCaptureActive = false;
        m_comboCaptureArmedTick = 0;
        m_comboCaptureStartTick = 0;
        m_comboCapturedKeys = 0;
      } else {
        // Keep capture mode active; B is intentionally ignored here.
        m_comboCaptureStartTick = 0;
        m_comboCapturedKeys = 0;
      }
    }
    return true;
  }

  if (keysDown & KEY_R) {
    if (keysHeld & KEY_ZL) {
      if (m_valueDisplay)
        static_cast<ValueDisplay *>(m_valueDisplay)->changeFontSize(2);
      return true;
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_cursorPos < m_value.length())
      m_cursorPos++;
    return true;
  }
  if (keysDown & KEY_L) {
    if (keysHeld & KEY_ZL) {
      if (m_valueDisplay)
        static_cast<ValueDisplay *>(m_valueDisplay)->changeFontSize(-2);
      return true;
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_cursorPos > 0)
      m_cursorPos--;
    return true;
  }
  if (keysDown & KEY_ZR) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_cursorPos += 9;
    if (m_cursorPos > m_value.length())
      m_cursorPos = m_value.length();
    return true;
  }
  if (keysDown & KEY_ZL) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_cursorPos >= 9)
      m_cursorPos -= 9;
    else
      m_cursorPos = 0;
    return true;
  }
  if (m_type == SEARCH_TYPE_HEX && (keysHeld & (KEY_L | KEY_R | KEY_ZL | KEY_ZR)))
    return true;
  if (keysHeld & (KEY_L | KEY_R))
    return true;

  if (keysDown & KEY_B) {
    handleBackspace();
    return true;
  }
  if (keysDown & KEY_X) {
    handleCancel();
    return true;
  }
  if (keysDown & KEY_PLUS) {
    handleConfirm();
    return true;
  }
  if (keysDown & KEY_LSTICK) {
    if (m_type == SEARCH_TYPE_TEXT) {
      if (m_onToggleCapsVisual) {
        m_onToggleCapsVisual();
      } else {
        m_capsMode = !m_capsMode;
      }
      return true;
    }
  }
  if (keysDown & KEY_Y) {
    if (m_type == SEARCH_TYPE_TEXT) {
      handleKeyPress(' ');
      return true;
    }

    s_noteMinimalMode = !s_noteMinimalMode;
    if (m_onNoteUpdate && m_frame) {
      if (m_isConstrained) {
        m_frame->setSubtitle(m_onNoteUpdate(m_value, m_cursorPos));
      } else {
        std::string valCopy = m_value;
        size_t cursorCopy = m_cursorPos;
        m_frame->setSubtitle(m_onNoteUpdate(valCopy, cursorCopy));
      }
    }
    return true;
  }
  return false;
}

bool KeyboardGui::handlePhysicalKeyboardInput() {
  HidKeyboardState currentKeyboardState{};
  if (hidGetKeyboardStates(&currentKeyboardState, 1) <= 0) {
    m_hasPrevKeyboardState = false;
    return false;
  }

  if (!m_hasPrevKeyboardState) {
    m_prevKeyboardState = currentKeyboardState;
    m_hasPrevKeyboardState = true;
    return false;
  }

  bool handled = false;
  auto keyDown = [&](HidKeyboardKey key) {
    return isKeyJustPressed(currentKeyboardState, m_prevKeyboardState, key);
  };
  auto acceptChar = [&](char c) {
    if (!allowsTextCharacter(m_type, m_title, c))
      return;
    handleKeyPress(c, true);
    handled = true;
  };

  if (keyDown(HidKeyboardKey_Backspace)) {
    handleBackspace();
    handled = true;
  }
  const bool shiftHeld =
      (currentKeyboardState.modifiers & HidKeyboardModifier_Shift) != 0;
  if (keyDown(HidKeyboardKey_NumPadEnter) ||
      (keyDown(HidKeyboardKey_Return) && shiftHeld)) {
    m_prevKeyboardState = currentKeyboardState;
    handleConfirm();
    return true;
  }
  if (keyDown(HidKeyboardKey_Escape)) {
    m_prevKeyboardState = currentKeyboardState;
    handleCancel();
    return true;
  }

  if (keyDown(HidKeyboardKey_LeftArrow)) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_cursorPos > 0)
      m_cursorPos--;
    handled = true;
  }
  if (keyDown(HidKeyboardKey_RightArrow)) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_cursorPos < m_value.length())
      m_cursorPos++;
    handled = true;
  }
  if (keyDown(HidKeyboardKey_Insert)) {
    toggleManualOvertype();
    if (m_valueDisplay)
      m_valueDisplay->invalidate();
    handled = true;
  }
  if (m_type == SEARCH_TYPE_TEXT && keyDown(HidKeyboardKey_CapsLock)) {
    if (m_onToggleCapsVisual) {
      m_onToggleCapsVisual();
    } else {
      m_capsMode = !m_capsMode;
    }
    handled = true;
  }

  for (HidKeyboardKey key = HidKeyboardKey_A; key <= HidKeyboardKey_Z;
       key = static_cast<HidKeyboardKey>(key + 1)) {
    if (keyDown(key))
      acceptChar(mapPhysicalKeyToChar(currentKeyboardState, key));
  }

  for (HidKeyboardKey key = HidKeyboardKey_D1; key <= HidKeyboardKey_D0;
       key = static_cast<HidKeyboardKey>(key + 1)) {
    if (keyDown(key))
      acceptChar(mapPhysicalKeyToChar(currentKeyboardState, key));
  }

  constexpr HidKeyboardKey symbolKeys[] = {
      HidKeyboardKey_Space, HidKeyboardKey_Minus,        HidKeyboardKey_Period,
      HidKeyboardKey_Comma, HidKeyboardKey_OpenBracket,  HidKeyboardKey_CloseBracket,
  };
  for (HidKeyboardKey key : symbolKeys) {
    if (keyDown(key))
      acceptChar(mapPhysicalKeyToChar(currentKeyboardState, key));
  }

  m_prevKeyboardState = currentKeyboardState;
  return handled;
}

void KeyboardGui::handleKeyPress(char c, bool directPhysicalInput) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);

  if (!directPhysicalInput && m_type == SEARCH_TYPE_TEXT &&
      std::isalpha(static_cast<unsigned char>(c))) {
    c = m_capsMode
            ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
            : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }

  // Overtype mode check
  if (isOvertypeMode() && m_cursorPos < m_value.length()) {
    m_value[m_cursorPos] = c;
    m_cursorPos++;
  } else {
    m_value.insert(m_cursorPos, 1, c);
    m_cursorPos++;
  }

  if (m_onNoteUpdate && m_frame) {
    if (m_isConstrained) {
      m_frame->setSubtitle(m_onNoteUpdate(m_value, m_cursorPos));
    } else {
      std::string valCopy = m_value;
      size_t cursorCopy = m_cursorPos;
      m_frame->setSubtitle(m_onNoteUpdate(valCopy, cursorCopy));
    }
  }
}

void KeyboardGui::handleBackspace() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);

  // Disable backspace in Overtype Mode
  if (isOvertypeMode())
    return;

  if (m_cursorPos > 0) {
    m_value.erase(m_cursorPos - 1, 1);
    m_cursorPos--;
    if (m_onNoteUpdate && m_frame) {
      if (m_isConstrained) {
        m_frame->setSubtitle(m_onNoteUpdate(m_value, m_cursorPos));
      } else {
        std::string valCopy = m_value;
        size_t cursorCopy = m_cursorPos;
        m_frame->setSubtitle(m_onNoteUpdate(valCopy, cursorCopy));
      }
    }
  }
}

void KeyboardGui::handleConfirm() {
  if (m_onComplete) {
    m_onComplete(m_value);
  } else {
    tsl::goBack();
  }
}

void KeyboardGui::handleCancel() { tsl::goBack(); }

void KeyboardGui::switchType(searchType_t newType) {
  {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_type = newType;
    m_isNumpad = (newType != SEARCH_TYPE_POINTER);
  }
  tsl::swapTo<KeyboardGui>(m_type, m_value, m_title, m_onComplete,
                           m_onNoteUpdate, m_isConstrained,
                           m_onGetSignedEditValue, m_onGetUnsignedEditValue, m_onGetFloatEditValue,
                           m_onApplySignedEdit, m_onApplyUnsignedEdit, m_onApplyFloatEdit,
                           m_onGetAsmEditValue, m_onApplyAsmEdit,
                           m_onClearStoredValue, m_onGetCodeType,
                           m_onApplyComboType, m_onSetComboCodeType,
                           m_onToggleC4AutoRepeat);
}

// Implement helper
bool KeyboardGui::isOvertypeMode() const {
  if (!m_isConstrained) return m_manualOvertype;
  return (m_type == SEARCH_TYPE_HEX && m_cursorPos < 8) || m_manualOvertype;
}
} // namespace tsl
