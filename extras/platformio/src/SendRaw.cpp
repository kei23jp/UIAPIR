// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

// PlatformIO entry point for the SendRaw sketch. See ../README.md.
//
// The include has to be written out rather than passed in as a macro: SCons
// follows a literal include and rebuilds when the sketch changes, and does not
// expand a macro to find one.
#include "../../../examples/SendRaw/SendRaw.ino"
