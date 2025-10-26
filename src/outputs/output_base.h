/*
 * output_base.h - Base interface for output modules
 */

#ifndef OUTPUT_BASE_H
#define OUTPUT_BASE_H

#include "sensor_types.h"

// Output module structure
typedef struct {
    const char* name;
    bool enabled;
    void (*init)(void);
    void (*send)(Sensor*);
    void (*update)(void);  // Called each loop iteration
} OutputModule;

// Output module initialization
void initOutputModules();
void sendToOutputs(Sensor* sensor);
void updateOutputs();

#endif
