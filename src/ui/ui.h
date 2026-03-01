#pragma once

#ifndef NATIVE_TEST_BUILD
void ui_init(void);
void set_label1(const char* text);
void set_label2(const char* text);
void set_label3(const char* text);
#else
inline void ui_init(void) {}
inline void set_label1(const char*) {}
inline void set_label2(const char*) {}
inline void set_label3(const char*) {}
#endif
