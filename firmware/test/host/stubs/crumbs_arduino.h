// Host stand-in for the CRUMBS Arduino HAL. There is no Wire: host.cpp's
// init keeps the context, and the tests feed frames to the real CRUMBS core.
#pragma once
#include "crumbs.h"
#ifdef __cplusplus
extern "C" {
#endif
void crumbs_arduino_init_peripheral(crumbs_context_t *ctx, uint8_t address);
#ifdef __cplusplus
}
#endif
