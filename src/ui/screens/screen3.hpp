#pragma once

#ifndef NATIVE_TEST_BUILD
#include "lvgl.h"
void screen3_create();
lv_obj_t* screen3_get();
#else
inline void screen3_create() {}
inline lv_obj_t* screen3_get() { return nullptr; }
#endif
