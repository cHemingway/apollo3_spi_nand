#include <stdint.h>

extern volatile uint32_t g_tick_ms;  // Global tick counter in milliseconds

// Initialise the system, setup clocks, enable cache etc
void system_init(void);