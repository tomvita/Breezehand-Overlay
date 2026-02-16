#include <switch.h>

#include <malloc.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define INNER_HEAP_SIZE 0x80000

#define AURORA_VID 0x1A2C
#define AURORA_PID 0x8FFF
#define MAX_LINKS 4
#define EVENT_QUEUE_SIZE 32
#define EVENT_TYPE_UP 0
#define EVENT_TYPE_DOWN 1
#define AURKBD_SERVICE_NAME "bhkbd01"
#define AURKBD_SHMEM_SIZE 0x1000

#define LOG_PATH "sdmc:/config/breezehand/aurora_kbd_bridge.log"

#ifndef AURORA_LOGGING
#define AURORA_LOGGING 0
#endif

#ifndef AURORA_EXIT_AFTER_INIT
#define AURORA_EXIT_AFTER_INIT 0
#endif

typedef struct {
    u32 magic;             // 'AKBD'
    u32 version;           // protocol version
    u64 system_tick;       // armGetSystemTick()
    u32 seq;               // incremented when state is updated
    u32 last_result;       // last libnx Result from USB path
    u8 connected;          // endpoint currently active
    u8 endpoint_address;   // IN endpoint currently read
    u8 modifiers;          // USB keyboard modifier bits
    u8 reserved0;
    u8 keys[6];            // USB boot keyboard usages from current report
    u32 last_report_size;  // bytes copied to last_report
    u8 last_report[64];    // raw report prefix for debugging
    u32 event_seq;         // increments per key-down event
    u8 event_usages[EVENT_QUEUE_SIZE];
    u8 event_modifiers[EVENT_QUEUE_SIZE];
    u8 event_types[EVENT_QUEUE_SIZE]; // EVENT_TYPE_*
} AuroraKeyboardState;

typedef struct {
    bool active;
    UsbHsClientIfSession if_session;
    UsbHsClientEpSession ep_session;
    u8 *io_buf;
    u32 io_buf_size;
    u8 endpoint_address;
    u32 endpoint_packet_size;
    s32 interface_id;
    char pathstr[0x40];
    u64 report_count;
    u64 last_log_tick;
    bool pending_async;
    u32 pending_xfer_id;
    u64 async_token;
    bool decode_boot;
    bool key_down[256];
    u64 key_last_seen_ns[256];
    u8 prev_modifiers;
    bool has_prev_modifiers;
} UsbKeyboardLink;

static FILE *g_log = NULL;
static UsbKeyboardLink g_links[MAX_LINKS];
static UsbHsInterface g_interfaces[32];
static SharedMemory g_state_shmem;
static AuroraKeyboardState *g_state_shmem_ptr = NULL;
static Handle g_srv_port = INVALID_HANDLE;
static Handle g_srv_session = INVALID_HANDLE;
static Handle g_srv_reply_target = INVALID_HANDLE;
static Thread g_ipc_thread;
static bool g_ipc_thread_running = false;

static void log_line(const char *fmt, ...) {
    if (!g_log)
        return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}

static const char *usage_to_name(u8 usage) {
    // Letters
    static const char *letters[] = {
        "key_a","key_b","key_c","key_d","key_e","key_f","key_g","key_h","key_i","key_j",
        "key_k","key_l","key_m","key_n","key_o","key_p","key_q","key_r","key_s","key_t",
        "key_u","key_v","key_w","key_x","key_y","key_z"
    };
    if (usage >= 0x04 && usage <= 0x1D) {
        return letters[usage - 0x04];
    }

    // Digits row
    static const char *digits[] = {
        "key_1","key_2","key_3","key_4","key_5","key_6","key_7","key_8","key_9","key_0"
    };
    if (usage >= 0x1E && usage <= 0x27) {
        return digits[usage - 0x1E];
    }

    switch (usage) {
        case 0x28: return "key_enter";
        case 0x29: return "key_escape";
        case 0x2A: return "key_backspace";
        case 0x2B: return "key_tab";
        case 0x2C: return "key_space";
        case 0x2D: return "key_minus";
        case 0x2E: return "key_equal";
        case 0x2F: return "key_left_bracket";
        case 0x30: return "key_right_bracket";
        case 0x31: return "key_backslash";
        case 0x33: return "key_semicolon";
        case 0x34: return "key_quote";
        case 0x35: return "key_backtick";
        case 0x36: return "key_comma";
        case 0x37: return "key_period";
        case 0x38: return "key_slash";
        case 0x39: return "key_capslock";
        case 0x49: return "key_insert";
        case 0x4F: return "key_right";
        case 0x50: return "key_left";
        case 0x58: return "key_numpad_enter";
        case 0x54: return "key_numpad_divide";
        case 0x55: return "key_numpad_multiply";
        case 0x56: return "key_numpad_minus";
        case 0x57: return "key_numpad_plus";
        case 0x63: return "key_numpad_dot";
        case 0x85: return "key_numpad_comma";
        case 0xE0: return "key_left_ctrl";
        case 0xE1: return "key_left_shift";
        case 0xE2: return "key_left_alt";
        case 0xE3: return "key_left_gui";
        case 0xE4: return "key_right_ctrl";
        case 0xE5: return "key_right_shift";
        case 0xE6: return "key_right_alt";
        case 0xE7: return "key_right_gui";
        default:   return "key_unknown";
    }
}

static void ensure_paths(void) {
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/breezehand", 0777);
}

static void build_cmif_response(Result result, Handle copy_handle) {
    void *tls = armGetTls();
    HipcRequest hipc = hipcMakeRequestInline(tls,
        .type = 0,
        .num_data_words = (16 + sizeof(CmifOutHeader) + 3) / 4,
        .num_copy_handles = (copy_handle != INVALID_HANDLE) ? 1 : 0
    );
    CmifOutHeader *out = (CmifOutHeader *)cmifGetAlignedDataStart(hipc.data_words, tls);
    *out = (CmifOutHeader){
        .magic = CMIF_OUT_HEADER_MAGIC,
        .version = 0,
        .result = result,
        .token = 0,
    };
    if (copy_handle != INVALID_HANDLE) {
        *hipc.copy_handles = copy_handle;
    }
}

static Result handle_service_request(bool *out_close_session, bool *out_reply_ready) {
    if (out_close_session) *out_close_session = false;
    if (out_reply_ready) *out_reply_ready = false;

    HipcParsedRequest req = hipcParseRequest(armGetTls());
    if (req.meta.type == CmifCommandType_Close) {
        if (out_close_session) *out_close_session = true;
        return 0;
    }

    if (req.meta.type != CmifCommandType_Request &&
        req.meta.type != CmifCommandType_RequestWithContext &&
        req.meta.type != CmifCommandType_Control &&
        req.meta.type != CmifCommandType_ControlWithContext) {
        build_cmif_response(MAKERESULT(Module_Libnx, LibnxError_BadInput), INVALID_HANDLE);
        if (out_reply_ready) *out_reply_ready = true;
        return 0;
    }

    CmifInHeader *in = (CmifInHeader *)cmifGetAlignedDataStart(req.data.data_words, armGetTls());
    if (in->magic != CMIF_IN_HEADER_MAGIC) {
        build_cmif_response(MAKERESULT(Module_Libnx, LibnxError_BadInput), INVALID_HANDLE);
        if (out_reply_ready) *out_reply_ready = true;
        return 0;
    }

    switch (in->command_id) {
        case 0: // GetSharedMemory
            log_line("IPC cmd=0 get_shmem");
            build_cmif_response(0, g_state_shmem.handle);
            if (out_reply_ready) *out_reply_ready = true;
            return 0;
        case 1: // Ping
            log_line("IPC cmd=1 ping");
            build_cmif_response(0, INVALID_HANDLE);
            if (out_reply_ready) *out_reply_ready = true;
            return 0;
        default:
            log_line("IPC cmd=%u unknown", in->command_id);
            build_cmif_response(MAKERESULT(Module_Libnx, LibnxError_NotFound), INVALID_HANDLE);
            if (out_reply_ready) *out_reply_ready = true;
            return 0;
    }
}

static void poll_service_once(u64 timeout_ns) {
    Handle handles[2];
    s32 handle_count = 0;
    if (g_srv_port != INVALID_HANDLE)
        handles[handle_count++] = g_srv_port;
    if (g_srv_session != INVALID_HANDLE)
        handles[handle_count++] = g_srv_session;
    if (handle_count <= 0)
        return;

    s32 idx = -1;
    Result rc = svcReplyAndReceive(&idx, handles, handle_count, g_srv_reply_target, timeout_ns);
    g_srv_reply_target = INVALID_HANDLE;

    if (rc == KERNELRESULT(TimedOut))
        return;
    if (R_FAILED(rc)) {
        log_line("svcReplyAndReceive failed rc=0x%x", rc);
        if (g_srv_session != INVALID_HANDLE) {
            svcCloseHandle(g_srv_session);
            g_srv_session = INVALID_HANDLE;
        }
        return;
    }

    const Handle signaled = handles[idx];
    if (signaled == g_srv_port) {
        Handle new_session = INVALID_HANDLE;
        rc = svcAcceptSession(&new_session, g_srv_port);
        if (R_FAILED(rc)) {
            log_line("svcAcceptSession failed rc=0x%x", rc);
            return;
        }
        if (g_srv_session != INVALID_HANDLE)
            svcCloseHandle(g_srv_session);
        g_srv_session = new_session;
        return;
    }

    if (signaled == g_srv_session) {
        bool close_session = false;
        bool reply_ready = false;
        rc = handle_service_request(&close_session, &reply_ready);
        if (R_FAILED(rc)) {
            log_line("handle_service_request failed rc=0x%x", rc);
            svcCloseHandle(g_srv_session);
            g_srv_session = INVALID_HANDLE;
            return;
        }
        if (close_session) {
            svcCloseHandle(g_srv_session);
            g_srv_session = INVALID_HANDLE;
            return;
        }
        if (reply_ready)
            g_srv_reply_target = g_srv_session;
    }
}

static void ipc_thread_func(void *arg) {
    (void)arg;
    while (g_ipc_thread_running) {
        poll_service_once(1000000000ULL);
    }
}

static void close_keyboard_link(UsbKeyboardLink *link) {
    if (!link)
        return;
    if (usbHsIfIsActive(&link->if_session)) {
        usbHsEpClose(&link->ep_session);
        usbHsIfClose(&link->if_session);
    }
    if (link->io_buf) {
        free(link->io_buf);
    }
    memset(link, 0, sizeof(*link));
}

static bool endpoint_is_interrupt_in(const struct usb_endpoint_descriptor *ep) {
    if (!ep || ep->bLength == 0)
        return false;
    if ((ep->bEndpointAddress & USB_ENDPOINT_IN) == 0)
        return false;
    return (ep->bmAttributes & USB_TRANSFER_TYPE_MASK) == USB_TRANSFER_TYPE_INTERRUPT;
}

static void configure_hid_interface(UsbHsClientIfSession *if_session) {
    if (!if_session || !usbHsIfIsActive(if_session))
        return;

    const u8 if_num = if_session->inf.inf.interface_desc.bInterfaceNumber;
    const u8 bmRequestType = 0x21; // Host->Device | Class | Interface
    u32 transferred = 0;

    // HID SetProtocol(boot). Some receivers ignore this; that's fine.
    Result rc = usbHsIfCtrlXfer(if_session, bmRequestType, 0x0B, 0x0000, if_num, 0, NULL, &transferred);
    if (R_FAILED(rc)) {
        log_line("SetProtocol(boot) failed if=%u rc=0x%x", if_num, rc);
    } else {
        log_line("SetProtocol(boot) ok if=%u", if_num);
    }

    // HID SetIdle(duration=0, report_id=0) to request immediate report updates.
    rc = usbHsIfCtrlXfer(if_session, bmRequestType, 0x0A, 0x0000, if_num, 0, NULL, &transferred);
    if (R_FAILED(rc)) {
        log_line("SetIdle failed if=%u rc=0x%x", if_num, rc);
    } else {
        log_line("SetIdle ok if=%u", if_num);
    }
}

static struct usb_endpoint_descriptor *pick_input_endpoint(UsbHsClientIfSession *if_session) {
    struct usb_endpoint_descriptor *best = NULL;
    for (int i = 0; i < 15; ++i) {
        struct usb_endpoint_descriptor *cur = &if_session->inf.inf.input_endpoint_descs[i];
        if (!endpoint_is_interrupt_in(cur))
            continue;

        if (!best) {
            best = cur;
            continue;
        }

        // Prefer smaller packet sizes (boot-style 8-byte reports), then endpoint 0x81.
        if (cur->wMaxPacketSize < best->wMaxPacketSize) {
            best = cur;
            continue;
        }
        if (cur->wMaxPacketSize == best->wMaxPacketSize && cur->bEndpointAddress == (USB_ENDPOINT_IN | 0x01)) {
            best = cur;
        }
    }
    return best;
}

static int score_interface_endpoint(const UsbHsInterface *inf) {
    if (!inf)
        return -1;

    int best = -1;
    for (int i = 0; i < 15; ++i) {
        const struct usb_endpoint_descriptor *ep = &inf->inf.input_endpoint_descs[i];
        if (!endpoint_is_interrupt_in(ep))
            continue;

        int score = 0;
        if (ep->bEndpointAddress == (USB_ENDPOINT_IN | 0x01))
            score += 100;
        if (ep->wMaxPacketSize == 8)
            score += 80;
        else if (ep->wMaxPacketSize <= 16)
            score += 40;
        else if (ep->wMaxPacketSize <= 64)
            score += 10;

        // Prefer boot keyboard-ish interface metadata.
        if (inf->inf.interface_desc.bInterfaceProtocol == 1)
            score += 20;
        if (inf->inf.interface_desc.bInterfaceSubClass == 1)
            score += 10;

        if (score > best)
            best = score;
    }
    return best;
}

static Result acquire_keyboard_links(UsbKeyboardLink *links, int *out_count) {
    Result rc = 0;
    UsbHsInterfaceFilter filter;
    s32 total_entries = 0;

    memset(&filter, 0, sizeof(filter));
    memset(g_interfaces, 0, sizeof(g_interfaces));
    for (int i = 0; i < MAX_LINKS; ++i) {
        close_keyboard_link(&links[i]);
    }
    if (out_count)
        *out_count = 0;

    filter.Flags = UsbHsInterfaceFilterFlags_idVendor |
                   UsbHsInterfaceFilterFlags_idProduct |
                   UsbHsInterfaceFilterFlags_bInterfaceClass;
    filter.idVendor = AURORA_VID;
    filter.idProduct = AURORA_PID;
    filter.bInterfaceClass = USB_CLASS_HID;

    rc = usbHsQueryAvailableInterfaces(&filter, g_interfaces, sizeof(g_interfaces), &total_entries);
    if (R_FAILED(rc)) {
        log_line("usbHsQueryAvailableInterfaces failed: 0x%x", rc);
        return rc;
    }
    if (total_entries <= 0) {
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    }

    int order[32];
    int used = total_entries > 32 ? 32 : total_entries;
    for (int i = 0; i < used; ++i)
        order[i] = i;

    for (int i = 0; i < used; ++i) {
        int best_pos = i;
        int best_score = score_interface_endpoint(&g_interfaces[order[i]]);
        for (int j = i + 1; j < used; ++j) {
            int s = score_interface_endpoint(&g_interfaces[order[j]]);
            if (s > best_score) {
                best_score = s;
                best_pos = j;
            }
        }
        if (best_pos != i) {
            int tmp = order[i];
            order[i] = order[best_pos];
            order[best_pos] = tmp;
        }
    }

    int acquired = 0;
    for (int oi = 0; oi < used && acquired < MAX_LINKS; ++oi) {
        int i = order[oi];
        UsbHsClientIfSession if_session;
        UsbHsClientEpSession ep_session;
        struct usb_endpoint_descriptor *ep_desc = NULL;
        u8 *io_buf = NULL;

        memset(&if_session, 0, sizeof(if_session));
        memset(&ep_session, 0, sizeof(ep_session));

        rc = usbHsAcquireUsbIf(&if_session, &g_interfaces[i]);
        if (R_FAILED(rc))
            continue;

        const u8 if_prot = g_interfaces[i].inf.interface_desc.bInterfaceProtocol;
        const u8 if_sub = g_interfaces[i].inf.interface_desc.bInterfaceSubClass;
        if (if_prot == 1) {
            configure_hid_interface(&if_session);
        }

        ep_desc = pick_input_endpoint(&if_session);
        if (!ep_desc) {
            usbHsIfClose(&if_session);
            continue;
        }

        io_buf = memalign(0x1000, 0x1000);
        if (!io_buf) {
            usbHsIfClose(&if_session);
            return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        }
        memset(io_buf, 0, 0x1000);

        rc = usbHsIfOpenUsbEp(&if_session, &ep_session, 1, ep_desc->wMaxPacketSize, ep_desc);
        if (R_FAILED(rc)) {
            free(io_buf);
            usbHsIfClose(&if_session);
            continue;
        }

        UsbKeyboardLink *link = &links[acquired];
        memset(link, 0, sizeof(*link));
        link->active = true;
        link->if_session = if_session;
        link->ep_session = ep_session;
        link->io_buf = io_buf;
        link->io_buf_size = 0x1000;
        link->endpoint_address = ep_desc->bEndpointAddress;
        link->endpoint_packet_size = ep_desc->wMaxPacketSize;
        link->interface_id = if_session.ID;
        // Some receivers expose keyboard protocol with non-boot subclass metadata.
        // Accept protocol keyboard + 8-byte endpoint as boot-report source.
        link->decode_boot = (ep_desc->wMaxPacketSize == 8 && if_prot == 1);
        snprintf(link->pathstr, sizeof(link->pathstr), "%s", g_interfaces[i].pathstr);

        log_line("Acquired interface %d id=%d path=%s ep=0x%02X packet=%u score=%d ifprot=%u ifsub=%u decode=%u",
                 i, if_session.ID, g_interfaces[i].pathstr, ep_desc->bEndpointAddress, ep_desc->wMaxPacketSize,
                 score_interface_endpoint(&g_interfaces[i]),
                 if_prot, if_sub, link->decode_boot ? 1 : 0);
        acquired++;
    }

    if (acquired <= 0)
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    if (out_count)
        *out_count = acquired;
    return 0;
}

static void push_key_event(AuroraKeyboardState *state, u8 usage, u8 modifiers, u8 type) {
    if (!state || usage == 0)
        return;
    state->event_seq++;
    const u32 idx = state->event_seq % EVENT_QUEUE_SIZE;
    state->event_usages[idx] = usage;
    state->event_modifiers[idx] = modifiers;
    state->event_types[idx] = type;
    log_line("Event seq=%u %s %s usage=0x%02X mod=0x%02X",
             state->event_seq,
             usage_to_name(usage),
             (type == EVENT_TYPE_DOWN) ? "down" : "up",
             usage,
             modifiers);
}

static bool decode_boot_report(UsbKeyboardLink *link, AuroraKeyboardState *state, const u8 *buf, u32 size) {
    if (!link || !state || !buf || size < 8)
        return false;

    bool changed = false;
    if (state->modifiers != buf[0]) {
        state->modifiers = buf[0];
        changed = true;
    }

    const u8 *raw_keys = &buf[2];
    const u64 now_ns = armTicksToNs(armGetSystemTick());
    const u8 curr_modifiers = buf[0];
    bool present[256] = {0};
    bool emitted_this_report[256] = {0};

    // Emit modifier up/down events (LeftCtrl..RightGui map to 0xE0..0xE7).
    if (!link->has_prev_modifiers) {
        link->prev_modifiers = curr_modifiers;
        link->has_prev_modifiers = true;
    } else {
        const u8 diff = (u8)(link->prev_modifiers ^ curr_modifiers);
        if (diff) {
            for (int i = 0; i < 8; ++i) {
                const u8 bit = (u8)(1u << i);
                if ((diff & bit) == 0)
                    continue;
                const u8 usage = (u8)(0xE0 + i);
                const u8 type = (curr_modifiers & bit) ? EVENT_TYPE_DOWN : EVENT_TYPE_UP;
                push_key_event(state, usage, curr_modifiers, type);
                changed = true;
            }
        }
        link->prev_modifiers = curr_modifiers;
    }

    for (int i = 0; i < 6; ++i) {
        const u8 usage = raw_keys[i];
        if (usage == 0 || present[usage])
            continue;
        present[usage] = true;

        // Normal path: usage transitions up->down.
        if (!link->key_down[usage]) {
            link->key_down[usage] = true;
            push_key_event(state, usage, curr_modifiers, EVENT_TYPE_DOWN);
            emitted_this_report[usage] = true;
            changed = true;
        } else {
            // Recovery path: if a release was missed, allow a new press event
            // after idle gap so the key is not "stuck" forever.
            const u64 age = now_ns - link->key_last_seen_ns[usage];
            if (age >= 180000000ULL) { // 180ms
                push_key_event(state, usage, curr_modifiers, EVENT_TYPE_DOWN);
                emitted_this_report[usage] = true;
                changed = true;
            }
        }
        link->key_last_seen_ns[usage] = now_ns;
    }

    // Keys not present in this report are considered released.
    for (int usage = 1; usage < 256; ++usage) {
        if (!present[usage]) {
            if (link->key_down[usage]) {
                link->key_down[usage] = false;
                push_key_event(state, (u8)usage, curr_modifiers, EVENT_TYPE_UP);
                changed = true;
            }
            continue;
        }

        // If keyboard spams identical reports while key is held, throttle repeats.
        if (emitted_this_report[usage]) {
            link->key_last_seen_ns[usage] = now_ns;
        }
    }

    if (memcmp(state->keys, raw_keys, 6) != 0) {
        memcpy(state->keys, raw_keys, 6);
        changed = true;
    }
    return changed;
}

static void write_state(const AuroraKeyboardState *state) {
    if (!g_state_shmem_ptr || !state)
        return;
    __builtin_memcpy(g_state_shmem_ptr, state, sizeof(*state));
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static bool poll_keyboard_once_async(UsbKeyboardLink *link, AuroraKeyboardState *state) {
    if (!link->active)
        return false;

    if (!link->pending_async) {
        Result rc = 0;
        u32 xfer_id = 0;
        link->async_token++;
        const u32 req_size = (link->endpoint_packet_size > 0 && link->endpoint_packet_size <= link->io_buf_size)
                                 ? link->endpoint_packet_size
                                 : link->io_buf_size;
        rc = usbHsEpPostBufferAsync(&link->ep_session, link->io_buf, req_size, link->async_token, &xfer_id);
        state->last_result = rc;
        if (R_FAILED(rc)) {
            log_line("usbHsEpPostBufferAsync failed ep=0x%02X id=%d req=%u rc=0x%x",
                     link->endpoint_address, link->interface_id, req_size, rc);
            close_keyboard_link(link);
            return false;
        }
        link->pending_xfer_id = xfer_id;
        link->pending_async = true;
        return false;
    }

    // Non-blocking check.
    Result rc_wait = eventWait(usbHsEpGetXferEvent(&link->ep_session), 0);
    if (R_FAILED(rc_wait))
        return false;

    eventClear(usbHsEpGetXferEvent(&link->ep_session));

    UsbHsXferReport reports[8];
    u32 count = 0;
    Result rc = usbHsEpGetXferReport(&link->ep_session, reports, 8, &count);
    state->last_result = rc;
    if (R_FAILED(rc)) {
        log_line("usbHsEpGetXferReport failed ep=0x%02X id=%d rc=0x%x", link->endpoint_address, link->interface_id, rc);
        close_keyboard_link(link);
        return false;
    }

    link->pending_async = false;
    bool changed = false;
    for (u32 i = 0; i < count; ++i) {
        const UsbHsXferReport *r = &reports[i];
        if (R_FAILED(r->res))
            continue;
        if (r->transferredSize == 0)
            continue;

        link->report_count++;

        bool report_changed = false;
        if (link->decode_boot && r->transferredSize >= 8) {
            const u32 copy_size =
                r->transferredSize > sizeof(state->last_report) ? sizeof(state->last_report) : r->transferredSize;

            if (state->connected == 0) {
                state->connected = 1;
                report_changed = true;
            }
            if (state->endpoint_address != link->endpoint_address) {
                state->endpoint_address = link->endpoint_address;
                report_changed = true;
            }
            if (state->last_report_size != copy_size ||
                memcmp(state->last_report, link->io_buf, copy_size) != 0) {
                memset(state->last_report, 0, sizeof(state->last_report));
                memcpy(state->last_report, link->io_buf, copy_size);
                state->last_report_size = copy_size;
                report_changed = true;
            }

            if (decode_boot_report(link, state, link->io_buf, r->transferredSize))
                report_changed = true;
        }

        if (report_changed) {
            const u32 copy_size =
                r->transferredSize > sizeof(state->last_report) ? sizeof(state->last_report) : r->transferredSize;
            changed = true;
            state->seq++;
            state->system_tick = armGetSystemTick();
            const u64 now = state->system_tick;
            if (armTicksToNs(now - link->last_log_tick) >= 200000000ULL) {
                link->last_log_tick = now;
                char hexbuf[3 * 16 + 1];
                const u32 n = copy_size < 16 ? copy_size : 16;
                for (u32 j = 0; j < n; ++j) {
                    snprintf(&hexbuf[j * 3], 4, "%02X ", state->last_report[j]);
                }
                hexbuf[n * 3] = 0;
                log_line("Report ep=0x%02X id=%d len=%u cnt=%llu data=%s",
                         link->endpoint_address, link->interface_id, copy_size,
                         (unsigned long long)link->report_count, hexbuf);
            }
        }
    }

    return changed;
}

#ifdef __cplusplus
extern "C" {
#endif

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void) {
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void *fake_heap_start;
    extern void *fake_heap_end;
    fake_heap_start = inner_heap;
    fake_heap_end = inner_heap + sizeof(inner_heap);
}

void __appInit(void) {
    Result rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    fsdevMountSdmc();

    rc = usbHsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    smExit();
}

void __appExit(void) {
    usbHsExit();
    fsdevUnmountAll();
    fsExit();
}

#ifdef __cplusplus
}
#endif

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    ensure_paths();
#if AURORA_LOGGING
    g_log = fopen(LOG_PATH, "a");
    if (g_log) {
        log_line("---- aurora_kbd_bridge start ----");
    }
#endif

    Event available_event;
    memset(&available_event, 0, sizeof(available_event));

    UsbHsInterfaceFilter event_filter;
    memset(&event_filter, 0, sizeof(event_filter));
    event_filter.Flags = UsbHsInterfaceFilterFlags_idVendor |
                         UsbHsInterfaceFilterFlags_idProduct |
                         UsbHsInterfaceFilterFlags_bInterfaceClass;
    event_filter.idVendor = AURORA_VID;
    event_filter.idProduct = AURORA_PID;
    event_filter.bInterfaceClass = USB_CLASS_HID;

    bool event_created = R_SUCCEEDED(usbHsCreateInterfaceAvailableEvent(&available_event, true, 0, &event_filter));
    if (!event_created) {
        log_line("usbHsCreateInterfaceAvailableEvent failed, polling fallback only");
    }

    AuroraKeyboardState state;
    memset(&state, 0, sizeof(state));
    state.magic = 0x44424B41; // AKBD
    state.version = 2;
    state.system_tick = armGetSystemTick();

    Result rc = shmemCreate(&g_state_shmem, AURKBD_SHMEM_SIZE, Perm_Rw, Perm_R);
    if (R_FAILED(rc)) {
        log_line("shmemCreate failed rc=0x%x", rc);
    } else {
        rc = shmemMap(&g_state_shmem);
        if (R_FAILED(rc)) {
            log_line("shmemMap failed rc=0x%x", rc);
            shmemClose(&g_state_shmem);
        } else {
            g_state_shmem_ptr = (AuroraKeyboardState *)shmemGetAddr(&g_state_shmem);
            memset(g_state_shmem_ptr, 0, AURKBD_SHMEM_SIZE);
            log_line("Shared memory ready handle=0x%x addr=%p", g_state_shmem.handle, g_state_shmem_ptr);
        }
    }

    rc = svcManageNamedPort(&g_srv_port, AURKBD_SERVICE_NAME, 4);
    if (R_FAILED(rc)) {
        log_line("svcManageNamedPort(%s) failed rc=0x%x", AURKBD_SERVICE_NAME, rc);
    } else {
        log_line("Named port ready: %s", AURKBD_SERVICE_NAME);
        g_ipc_thread_running = true;
        rc = threadCreate(&g_ipc_thread, ipc_thread_func, NULL, NULL, 0x4000, 30, 0);
        if (R_FAILED(rc)) {
            g_ipc_thread_running = false;
            log_line("threadCreate(ipc) failed rc=0x%x", rc);
        } else {
            rc = threadStart(&g_ipc_thread);
            if (R_FAILED(rc)) {
                g_ipc_thread_running = false;
                log_line("threadStart(ipc) failed rc=0x%x", rc);
                threadClose(&g_ipc_thread);
            } else {
                log_line("IPC thread started");
            }
        }
    }

    UsbKeyboardLink *links = g_links;
    memset(links, 0, sizeof(g_links));
    int active_links = 0;

    u64 last_acquire_attempt_tick = 0;
    u64 last_heartbeat_tick = 0;
    u64 last_stats_tick = 0;

    write_state(&state);

#if AURORA_EXIT_AFTER_INIT
    log_line("Init-only mode active, exiting");
#else
    while (true) {
        u64 now = armGetSystemTick();
        bool should_try_acquire = (active_links <= 0);

        if (event_created && R_SUCCEEDED(eventWait(&available_event, 0))) {
            should_try_acquire = true;
        }

        if (should_try_acquire && armTicksToNs(now - last_acquire_attempt_tick) >= 500000000ULL) {
            last_acquire_attempt_tick = now;
            state.last_result = acquire_keyboard_links(links, &active_links);
            if (R_SUCCEEDED(state.last_result)) {
                state.connected = 1;
                state.endpoint_address = links[0].endpoint_address;
                state.system_tick = armGetSystemTick();
                state.seq++;
                write_state(&state);
            }
        }

        bool any_active = false;
        bool any_changed = false;
        for (int i = 0; i < MAX_LINKS; ++i) {
            if (!links[i].active)
                continue;
            any_active = true;
            if (poll_keyboard_once_async(&links[i], &state)) {
                any_changed = true;
            }
        }
        if (any_changed) {
            write_state(&state);
        }
        if (!any_active && active_links > 0) {
            active_links = 0;
            state.connected = 0;
            state.endpoint_address = 0;
            memset(state.keys, 0, sizeof(state.keys));
            state.modifiers = 0;
            state.last_report_size = 0;
            state.seq++;
            state.system_tick = armGetSystemTick();
            write_state(&state);
        }

        if (state.connected == 0 && active_links > 0) {
            // We have open links but no reports yet; mark current primary endpoint for visibility.
            state.endpoint_address = links[0].endpoint_address;
        }

        if (armTicksToNs(now - last_heartbeat_tick) >= 1000000000ULL) {
            last_heartbeat_tick = now;
            state.system_tick = now;
            write_state(&state);
        }

        if (armTicksToNs(now - last_stats_tick) >= 2000000000ULL) {
            last_stats_tick = now;
            for (int i = 0; i < MAX_LINKS; ++i) {
                if (!links[i].active)
                    continue;
                log_line("Stats slot=%d id=%d ep=0x%02X pending=%d reports=%llu last=0x%x",
                         i, links[i].interface_id, links[i].endpoint_address,
                         links[i].pending_async ? 1 : 0,
                         (unsigned long long)links[i].report_count,
                         state.last_result);
            }
        }

        svcSleepThread(1 * 1000 * 1000ULL);
    }
#endif

    for (int i = 0; i < MAX_LINKS; ++i) {
        close_keyboard_link(&links[i]);
    }
    if (g_ipc_thread_running) {
        g_ipc_thread_running = false;
        threadWaitForExit(&g_ipc_thread);
        threadClose(&g_ipc_thread);
    }
    if (g_srv_session != INVALID_HANDLE) {
        svcCloseHandle(g_srv_session);
        g_srv_session = INVALID_HANDLE;
    }
    if (g_srv_port != INVALID_HANDLE) {
        svcCloseHandle(g_srv_port);
        g_srv_port = INVALID_HANDLE;
    }
    if (g_state_shmem.handle != INVALID_HANDLE) {
        shmemClose(&g_state_shmem);
        g_state_shmem_ptr = NULL;
    }
    if (event_created) {
        usbHsDestroyInterfaceAvailableEvent(&available_event, 0);
    }
#if AURORA_LOGGING
    if (g_log) {
        log_line("---- aurora_kbd_bridge stop ----");
        fclose(g_log);
    }
#endif
    return 0;
}
