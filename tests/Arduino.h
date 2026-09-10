// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#pragma once

#include <stdint.h>

// Minimal host-test stand-in for the declarations UIAPIR.h exposes in member
// types. Hardware behavior remains covered by the on-device test sketches.
struct GPIO_TypeDef {};
unsigned long millis();
void delay(unsigned long ms);
