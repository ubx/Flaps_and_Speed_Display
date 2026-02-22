#pragma once

#ifndef NATIVE_TEST_BUILD
void ui_init(void);
#else
inline void ui_init(void) {}
#endif
