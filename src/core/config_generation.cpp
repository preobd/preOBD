/*
 * config_generation.cpp - see config_generation.h
 */

#include "config_generation.h"

#if INCLUDE_FRAMED_TRANSPORT

static uint16_t g_configGeneration = 0;

void configGenerationBump() { g_configGeneration++; }
uint16_t configGeneration() { return g_configGeneration; }

#endif
