/*
 * registry_display.h - CLI listers over the sensor and application registries.
 *
 * These walk the static sensor/application preset tables and touch inputs[]
 * not at all. Moved out of inputs/input_manager.cpp (#402 step 1), where
 * they lived only because the CLI commands calling them were nearby.
 */

#ifndef REGISTRY_DISPLAY_H
#define REGISTRY_DISPLAY_H

void listApplicationPresets();       // List available Applications
void listSensors(const char* filter = nullptr);  // List sensors (categories, by category, or by measurement type)

#endif // REGISTRY_DISPLAY_H
