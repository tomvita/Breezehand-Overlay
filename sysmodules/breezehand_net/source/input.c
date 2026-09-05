/*
 * Controller state and game freeze.
 *
 * Why this lives in the sysmodule rather than the client: every experiment that
 * needs "do X at the moment the player presses B" was previously coordinated by
 * a human over a network round trip, and the timing was never good enough --
 * triggers fired mid-jump instead of mid-hover. Reading the pad here means the
 * trigger is on-device and the latency is one 2 ms poll.
 *
 * The setup mirrors what the Tesla overlay does (tesla.hpp ~13650). An earlier
 * version skipped hidSetSupportedNpadIdType / padConfigureInput on the theory
 * that they might disturb the running game -- they do not, because they take an
 * AppletResourceUserId and configure *our* session's view, not the game's. And
 * without them our applet resource has no supported npad ids, so the reads come
 * back empty.
 */

#include <switch.h>
#include "dmntcht.h"
#include "input.h"

static PadState g_pad_p1;
static PadState g_pad_handheld;
static bool     g_input_ready = false;

void bhnet_input_init(void) {
    if (R_FAILED(hidInitialize())) return;

    HidNpadIdType id_list[2] = { HidNpadIdType_No1, HidNpadIdType_Handheld };
    hidSetSupportedNpadIdType(id_list, 2);
    padConfigureInput(2, HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadSystemExt);

    padInitialize(&g_pad_p1, HidNpadIdType_No1);
    padInitialize(&g_pad_handheld, HidNpadIdType_Handheld);

    /* Clear any stale input. */
    padUpdate(&g_pad_p1);
    padUpdate(&g_pad_handheld);

    g_input_ready = true;
}

u64 bhnet_buttons(void) {
    if (!g_input_ready) return 0;
    padUpdate(&g_pad_p1);
    padUpdate(&g_pad_handheld);
    return padGetButtons(&g_pad_p1) | padGetButtons(&g_pad_handheld);
}

/*
 * Waits for a fresh press (a rising edge, so a button already held when the
 * command arrives does not count) of any bit in mask. Returns milliseconds
 * waited, or -1 on timeout.
 */
s64 bhnet_wait_press(u64 mask, u32 timeout_ms) {
    if (!g_input_ready) return -1;

    u64 prev  = bhnet_buttons();
    u64 start = armTicksToNs(armGetSystemTick());

    for (;;) {
        svcSleepThread(2000000ULL);              /* 2 ms */

        u64 now  = bhnet_buttons();
        u64 edge = now & ~prev & mask;
        prev = now;

        u64 elapsed_ms = (armTicksToNs(armGetSystemTick()) - start) / 1000000ULL;
        if (edge != 0) return (s64)elapsed_ms;
        if (elapsed_ms >= (u64)timeout_ms) return -1;
    }
}

Result bhnet_freeze(void) { return dmntchtPauseCheatProcess();  }
Result bhnet_resume(void) { return dmntchtResumeCheatProcess(); }
