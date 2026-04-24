/*
 * profile_debug.h — Debug build (Teensy 4.1, -Og -g).
 * Features and sizing identical to profile_teensy41; separate file so
 * debug-specific tuning has a dedicated home if it diverges later.
 */
#pragma message "Building with profile: profile_debug"
#include "profile_teensy41.h"
