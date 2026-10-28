// SPDX-License-Identifier: MIT
#pragma once
#include "controller_state.h"
#include "trigger_engine.h"
void effects_input(const ControllerState &input);
void effects_rumble(uint8_t left, uint8_t right);
void effects_native(unsigned side, const uint8_t effect[11]);
void effects_overlay(uint8_t left[11], uint8_t right[11]);
void effects_task();
void effects_diagnostics(uint8_t out[12]);
void effects_stop();
void effects_new_session();
const TriggerSettings &effects_settings();
void effects_load(const TriggerSettings &settings);
bool effects_configure(const TriggerSettings &settings);
