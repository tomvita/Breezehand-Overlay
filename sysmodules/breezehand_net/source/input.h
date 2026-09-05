#pragma once

#include <switch.h>

void   bhnet_input_init(void);
u64    bhnet_buttons(void);
s64    bhnet_wait_press(u64 mask, u32 timeout_ms);
Result bhnet_freeze(void);
Result bhnet_resume(void);
