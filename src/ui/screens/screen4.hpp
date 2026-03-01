#pragma once

#ifndef NATIVE_TEST_BUILD
#include "lvgl.h"
void screen4_create();
lv_obj_t* screen4_get();
#else
inline void screen4_create() {}
inline lv_obj_t* screen4_get() { return nullptr; }
#endif
