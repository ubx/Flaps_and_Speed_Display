#pragma once

#ifndef NATIVE_TEST_BUILD
#include "lvgl.h"
void screen2_create();
lv_obj_t* screen2_get();
#else
inline void screen2_create() {}
inline lv_obj_t* screen2_get() { return nullptr; }
#endif
