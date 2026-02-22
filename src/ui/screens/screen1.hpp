#pragma once

#ifndef NATIVE_TEST_BUILD
#include "lvgl.h"
void screen1_create();
lv_obj_t* screen1_get();
#else
inline void screen1_create() {}
inline lv_obj_t* screen1_get() { return nullptr; }
#endif
