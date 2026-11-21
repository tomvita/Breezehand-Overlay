// Code-patch cheats with their originals in a block that never runs.
// Copied from Breeze (core/code_patch.hpp); keep the two the same.
//
//   [Name]
//   C046E5E0                       if R14 != R14: never true, so the block never runs
//   04000000 AAAAAAAA ORIGINAL_A   the game's instruction at A
//   04000000 BBBBBBBB ORIGINAL_B
//   20000000                       end of the block
//   04000000 AAAAAAAA HACK_A       what the cheat writes
//   04000000 BBBBBBBB HACK_B
//
// The block comes first (a keypress conditional may wrap the whole cheat). It holds only
// 4-byte code stores, main relative (04000000) or R1 relative (04010000), each address once,
// and every address in it is written again later in the cheat. Breeze reads the originals to
// put the game's code back when the cheat is turned off; every cheat VM skips the block, so
// the original is never written while the cheat runs.
//
// The marker is C046E5E0 and not C046F5F0 on purpose: Breeze's dmntcht.c hands old-style ASM
// cheats to dmnt as "C046F5F0 | original | 20000000 | ... | hook" and turns that shape back
// when it reads them, so a cheat stored with C046F5F0 could not be told apart from it.
//
// Header only, no dependencies past the standard library: Breeze, emuhost and the PC tests
// include it as it is.
#pragma once

#include <cstdint>
#include <vector>

namespace breeze_core::codepatch {

constexpr uint32_t kNever = 0xC046E5E0u;  // C0 width 4, cond !=, R14 against R14
constexpr uint32_t kEnd = 0x20000000u;

struct Patch {
    uint32_t header = 0;    // the store's first word: 04000000, R1 bit 16, address bits 32-39
    uint32_t offset = 0;    // address bits 0-31
    uint32_t original = 0;  // the game's instruction
    uint32_t hack = 0;      // what the cheat writes there
};

inline bool is_code_store(uint32_t w) {
    // 0TMR00AA with T = 4, M = 0 (main), R = 0 or 1.
    return (w & 0xFFFEFF00u) == 0x04000000u;
}
inline bool uses_r1(const Patch &p) { return ((p.header >> 16) & 1) != 0; }
inline uint64_t rel_address(const Patch &p) { return ((uint64_t)(p.header & 0xFF) << 32) | p.offset; }
inline uint32_t store_header(uint64_t rel, bool r1) {
    return 0x04000000u | (r1 ? 0x00010000u : 0) | (uint32_t)((rel >> 32) & 0xFF);
}
inline bool same_address(const Patch &a, const Patch &b) {
    return a.header == b.header && a.offset == b.offset;
}

// How many words the opcode at op takes, as the cheat VM decodes it; 0 for one this does not
// know or that runs past the end. Callers treat 0 as "not a cheat this can read".
inline uint32_t opcode_words(const uint32_t *op, uint32_t left) {
    if (left == 0) return 0;
    const uint32_t w = op[0];
    const uint32_t type = w >> 28;
    const uint32_t width = (w >> 24) & 0xF;
    const auto vm_int = [](uint32_t bits) -> uint32_t { return bits == 8 ? 2 : 1; };
    uint32_t len = 0;
    switch (type) {
    case 0x0: len = 2 + vm_int(width); break;                      // 0TMR00AA AAAAAAAA V(V)
    case 0x1: len = 2 + vm_int(width); break;                      // 1TMC00AA AAAAAAAA V(V)
    case 0x2: len = 1; break;                                      // 20000000 / 21000000
    case 0x3: len = (width == 0) ? 2 : 1; break;                   // 300R0000 VVVVVVVV / 310R0000
    case 0x4: len = 3; break;                                      // 400R0000 VVVVVVVV VVVVVVVV
    case 0x5: len = 2; break;                                      // 5TMRI0AA AAAAAAAA
    case 0x6: len = 3; break;                                      // 6T0RIor0 VVVVVVVV VVVVVVVV
    case 0x7: len = 2; break;                                      // 7T0RC000 VVVVVVVV
    case 0x8: len = 1; break;                                      // 8kkkkkkk
    case 0x9: len = 1 + ((((w >> 8) & 0xF) != 0) ? vm_int(width) : 0); break;  // 9TCRSIs0 (V(V))
    case 0xA: {                                                    // ATSRIOxa (aaaaaaaa)
        const uint32_t ofs = (w >> 8) & 0xF;
        len = (ofs == 2 || ofs == 4 || ofs == 5) ? 2 : 1;
        break;
    }
    case 0xC: {
        const uint32_t sub = (w >> 24) & 0xF;
        if (sub == 0) {                                            // C0TcSX##
            const uint32_t bits = (w >> 20) & 0xF;
            const uint32_t x = (w >> 8) & 0xF;
            len = (x == 4) ? 1 + vm_int(bits) : (x == 0 || x == 2) ? 2 : 1;
        } else if (sub == 1 || sub == 2 || sub == 3) {             // C1 / C2 / C3
            len = 1;
        } else if (sub == 4) {                                     // C4 extended keypress
            len = 3;
        } else {
            return 0;
        }
        break;
    }
    case 0xF: {
        if (((w >> 24) & 0xF) != 0xF) return 0;
        const uint32_t sub = (w >> 20) & 0xF;
        if (sub == 0 || sub == 1) {                                // FF0 / FF1
            len = 1;
        } else if (sub == 0xF) {                                   // FFFTIX##
            const uint32_t x = (w >> 8) & 0xF;
            len = (x == 0 || x == 2) ? 2 : 1;
        } else {
            return 0;
        }
        break;
    }
    default:
        return 0;
    }
    return len <= left ? len : 0;
}

struct Parsed {
    uint32_t key = 0;          // the keypress word wrapping the cheat, 0 for none
    uint32_t block_at = 0;     // index of kNever
    uint32_t block_end = 0;    // index just after the block's kEnd
    std::vector<Patch> patches;
};

// Reads a cheat in the form above. False for any other cheat (out is then unspecified).
inline bool parse(const uint32_t *op, uint32_t n, Parsed &out) {
    out = Parsed{};
    uint32_t i = 0;
    if (n > 0 && (op[0] >> 28) == 0x8) {
        out.key = op[0];
        i = 1;
    }
    if (i >= n || op[i] != kNever) return false;
    out.block_at = i++;
    while (i < n && op[i] != kEnd) {
        if (i + 3 > n || !is_code_store(op[i])) return false;
        Patch p;
        p.header = op[i];
        p.offset = op[i + 1];
        p.original = op[i + 2];
        for (const Patch &q : out.patches)
            if (same_address(p, q)) return false;
        out.patches.push_back(p);
        i += 3;
    }
    if (i >= n || out.patches.empty()) return false;
    out.block_end = ++i;

    std::vector<bool> written(out.patches.size(), false);
    while (i < n) {
        const uint32_t len = opcode_words(op + i, n - i);
        if (len == 0) return false;
        if (len == 3 && is_code_store(op[i])) {
            for (size_t k = 0; k < out.patches.size(); k++) {
                Patch &p = out.patches[k];
                if (p.header == op[i] && p.offset == op[i + 1]) {
                    p.hack = op[i + 2];
                    written[k] = true;
                }
            }
        }
        i += len;
    }
    for (bool w : written)
        if (!w) return false;
    return true;
}

// The cheat's opcodes for patches: block, then the hacks in the same order; key, when not 0,
// wraps the whole cheat.
inline std::vector<uint32_t> build(const std::vector<Patch> &patches, uint32_t key = 0) {
    std::vector<uint32_t> ops;
    if (key) ops.push_back(key);
    ops.push_back(kNever);
    for (const Patch &p : patches) {
        ops.push_back(p.header);
        ops.push_back(p.offset);
        ops.push_back(p.original);
    }
    ops.push_back(kEnd);
    for (const Patch &p : patches) {
        ops.push_back(p.header);
        ops.push_back(p.offset);
        ops.push_back(p.hack);
    }
    if (key) ops.push_back(kEnd);
    return ops;
}

// The "<name> - restore" cheat for players whose cheat tool does not read the block: plain
// stores of the originals, which put the game's code back when turned on once.
inline std::vector<uint32_t> restore_ops(const std::vector<Patch> &patches) {
    std::vector<uint32_t> ops;
    for (const Patch &p : patches) {
        ops.push_back(p.header);
        ops.push_back(p.offset);
        ops.push_back(p.original);
    }
    return ops;
}
inline bool is_restore_of(const std::vector<Patch> &patches, const uint32_t *op, uint32_t n) {
    const std::vector<uint32_t> want = restore_ops(patches);
    if (n != want.size()) return false;
    for (uint32_t i = 0; i < n; i++)
        if (op[i] != want[i]) return false;
    return true;
}

// Adds or replaces patches in to: an address already there takes the new hack and keeps its
// original. False, with to untouched, when an address is in both with a different original
// (the two cheats were made against different code).
inline bool merge(std::vector<Patch> &to, const std::vector<Patch> &from) {
    std::vector<Patch> out = to;
    for (const Patch &p : from) {
        bool found = false;
        for (Patch &q : out) {
            if (!same_address(p, q)) continue;
            if (q.original != p.original) return false;
            q.hack = p.hack;
            found = true;
        }
        if (!found) out.push_back(p);
    }
    to = out;
    return true;
}

}  // namespace breeze_core::codepatch
