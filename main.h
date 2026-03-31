#pragma once

#include <stdint.h>

extern uint8_t main_taskid;

#define MAIN_EVT_START (1 << 0)
#define MAIN_EVT_PERIODIC1S (1 << 1)
#define MAIN_EVT_FORTH (1 << 2)
