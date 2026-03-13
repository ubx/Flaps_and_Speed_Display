#pragma once

#include "flaputils.hpp"

void ui_init();
void set_label1(const char* text);
void set_label2(const char* text);
void set_label3(const char* text);

// Global data access wrappers
float get_ias_kmh();
float get_weight_kg();
flaputils::FlapSymbolResult get_flap_actual();
flaputils::FlapSymbolResult get_flap_target();
bool is_stale();
