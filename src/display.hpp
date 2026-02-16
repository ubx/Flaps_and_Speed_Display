#pragma once

#include "lvgl_port_alignment.h"
#ifndef NATIVE_BUILD
void display_start();
#else
inline void display_start() {}
#endif
