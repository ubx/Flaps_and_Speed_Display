#pragma once

#ifndef NATIVE_TEST_BUILD
void display_start();
#else
inline void display_start()
{
}
#endif
