#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "search_types.hpp"

namespace breeze {
namespace search_exec {

template <typename T> struct TypeTag {
  using type = T;
};

template <typename T>
inline T ReadUnaligned(const void *ptr) {
  T out{};
  std::memcpy(&out, ptr, sizeof(T));
  return out;
}

template <typename T>
inline T SearchValueAs(const searchValue_t &value) {
  static_assert(sizeof(T) <= sizeof(searchValue_t), "T too large for searchValue_t");
  return ReadUnaligned<T>(&value);
}

template <typename Fn>
auto DispatchBySearchType(searchType_t type, Fn &&fn)
    -> decltype(fn(TypeTag<u8>{})) {
  switch (type) {
  case SEARCH_TYPE_UNSIGNED_8BIT:
    return fn(TypeTag<u8>{});
  case SEARCH_TYPE_SIGNED_8BIT:
    return fn(TypeTag<s8>{});
  case SEARCH_TYPE_UNSIGNED_16BIT:
    return fn(TypeTag<u16>{});
  case SEARCH_TYPE_SIGNED_16BIT:
    return fn(TypeTag<s16>{});
  case SEARCH_TYPE_UNSIGNED_32BIT:
    return fn(TypeTag<u32>{});
  case SEARCH_TYPE_SIGNED_32BIT:
    return fn(TypeTag<s32>{});
  case SEARCH_TYPE_UNSIGNED_64BIT:
  case SEARCH_TYPE_POINTER:
    return fn(TypeTag<u64>{});
  case SEARCH_TYPE_SIGNED_64BIT:
    return fn(TypeTag<s64>{});
  case SEARCH_TYPE_FLOAT_32BIT:
    return fn(TypeTag<float>{});
  case SEARCH_TYPE_FLOAT_64BIT:
    return fn(TypeTag<double>{});
  case SEARCH_TYPE_UNSIGNED_40BIT:
    return fn(TypeTag<u64>{});
  default:
    return fn(TypeTag<u32>{});
  }
}

inline bool IsPointerLike(u64 value, u64 heapBase, u64 heapEnd, u64 mainBase,
                          u64 mainEnd) {
  const bool inHeap = (value >= heapBase) && (value < heapEnd);
  const bool inMain = (value >= mainBase) && (value < mainEnd);
  return inHeap || inMain;
}

template <typename T>
inline bool MatchModeTyped(searchMode_t mode, T current,
                           const Search_condition &condition,
                           const T *previousA = nullptr,
                           const T *previousB = nullptr,
                           u64 heapBase = 0, u64 heapEnd = 0, u64 mainBase = 0,
                           u64 mainEnd = 0) {
  const T a = SearchValueAs<T>(condition.searchValue_1);
  const T b = SearchValueAs<T>(condition.searchValue_2);

  switch (mode) {
  case SM_EQ:
    return current == a;
  case SM_NE:
    return current != a;
  case SM_GT:
    return current > a;
  case SM_LT:
    return current < a;
  case SM_GE:
    return current >= a;
  case SM_LE:
    return current <= a;
  case SM_RANGE_EQ:
    return (current >= a) && (current <= b);
  case SM_RANGE_LT:
    return (current > a) && (current < b);
  case SM_BMEQ:
    if constexpr (std::is_integral<T>::value) {
      return (static_cast<u64>(current) & static_cast<u64>(b)) ==
             static_cast<u64>(a);
    }
    return false;
  case SM_MORE:
    return previousA ? (current > *previousA) : false;
  case SM_LESS:
    return previousA ? (current < *previousA) : false;
  case SM_DIFF:
    return previousA ? (current != *previousA) : false;
  case SM_SAME:
    return previousA ? (current == *previousA) : false;
  case SM_INC_BY:
    if (previousA == nullptr)
      return false;
    return (current > (*previousA + a - static_cast<T>(1))) &&
           (current < (*previousA + a + static_cast<T>(1)));
  case SM_DEC_BY:
    if (previousA == nullptr)
      return false;
    return (current > (*previousA - a - static_cast<T>(1))) &&
           (current < (*previousA - a + static_cast<T>(1)));
  case SM_MOREB:
    return previousB ? (current > *previousB) : false;
  case SM_LESSB:
    return previousB ? (current < *previousB) : false;
  case SM_DIFFB:
    return previousB ? (current != *previousB) : false;
  case SM_SAMEB:
    return previousB ? (current == *previousB) : false;
  case SM_NOTAB:
    return previousA && previousB ? (current != *previousA && current != *previousB)
                                  : false;
  case SM_PTR:
    return IsPointerLike(static_cast<u64>(current), heapBase, heapEnd, mainBase,
                         mainEnd);
  case SM_NPTR:
    return !IsPointerLike(static_cast<u64>(current), heapBase, heapEnd, mainBase,
                          mainEnd);
  case SM_NoDecimal:
    if constexpr (std::is_floating_point<T>::value) {
      return (current >= a) && (current <= b) && (std::trunc(current) == current);
    }
    return false;
  default:
    return false;
  }
}

inline bool MatchEqPlus(u32 aAsU32, const void *valuePtr) {
  const u32 vU32 = ReadUnaligned<u32>(valuePtr);
  const float vF32 = ReadUnaligned<float>(valuePtr);
  const double vF64 = ReadUnaligned<double>(valuePtr);
  return (aAsU32 == vU32) || (static_cast<float>(aAsU32) == vF32) ||
         (static_cast<double>(aAsU32) == vF64);
}

inline bool MatchEqPlusPlus(u32 aAsU32, const void *valuePtr) {
  const u32 vU32 = ReadUnaligned<u32>(valuePtr);
  const float vF32 = ReadUnaligned<float>(valuePtr);
  const double vF64 = ReadUnaligned<double>(valuePtr);
  const float aF32 = static_cast<float>(aAsU32);
  const double aF64 = static_cast<double>(aAsU32);
  const bool matchU32 = (aAsU32 == vU32);
  const bool matchF32 = (vF32 > (aF32 - 1.0f)) && (vF32 < (aF32 + 1.0f));
  const bool matchF64 = (vF64 > (aF64 - 1.0)) && (vF64 < (aF64 + 1.0));
  return matchU32 || matchF32 || matchF64;
}

inline u32 ConditionValue1AsU32(const Search_condition &condition) {
  return DispatchBySearchType(condition.searchType, [&](auto tag) -> u32 {
    using T = typename decltype(tag)::type;
    const T typed = SearchValueAs<T>(condition.searchValue_1);
    if constexpr (std::is_floating_point<T>::value) {
      return static_cast<u32>(typed);
    } else if constexpr (std::is_signed<T>::value) {
      return static_cast<u32>(static_cast<s64>(typed));
    } else {
      return static_cast<u32>(typed);
    }
  });
}

inline bool MatchEqPlus(const Search_condition &condition, const void *valuePtr) {
  return MatchEqPlus(ConditionValue1AsU32(condition), valuePtr);
}

inline bool MatchEqPlusPlus(const Search_condition &condition,
                            const void *valuePtr) {
  return MatchEqPlusPlus(ConditionValue1AsU32(condition), valuePtr);
}

} // namespace search_exec
} // namespace breeze
