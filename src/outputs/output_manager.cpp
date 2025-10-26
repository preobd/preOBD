/*
 * output_manager.cpp - Manages all output modules
 */

#include "output_base.h"
#include "../config.h"

// Declare external functions from output modules
extern void initCAN();
extern void sendCAN(Sensor*);
extern void updateCAN();

extern void initRealdash();
extern void sendRealdash(Sensor*);
extern void updateRealdash();

extern void initSerialOutput();
extern void sendSerialOutput(Sensor*);
extern void updateSerialOutput();

extern void initSDLog();
extern void sendSDLog(Sensor*);
extern void updateSDLog();

// Define output modules array
OutputModule outputModules[] = {
    #ifdef ENABLE_CAN
    {"CAN", true, initCAN, sendCAN, updateCAN},
    #endif
    
    #ifdef ENABLE_REALDASH
    {"RealDash", true, initRealdash, sendRealdash, updateRealdash},
    #endif
    
    #ifdef ENABLE_SERIAL_OUTPUT
    {"Serial", true, initSerialOutput, sendSerialOutput, updateSerialOutput},
    #endif
    
    #ifdef ENABLE_SD_LOGGING
    {"SD_Log", true, initSDLog, sendSDLog, updateSDLog},
    #endif
};

const int numOutputModules = sizeof(outputModules) / sizeof(outputModules[0]);

void initOutputModules() {
    for (int i = 0; i < numOutputModules; i++) {
        if (outputModules[i].enabled && outputModules[i].init != nullptr) {
            outputModules[i].init();
        }
    }
}

void sendToOutputs(Sensor* sensor) {
    if (!sensor->isEnabled || isnan(sensor->value)) {
        return;
    }
    
    for (int i = 0; i < numOutputModules; i++) {
        if (outputModules[i].enabled && outputModules[i].send != nullptr) {
            outputModules[i].send(sensor);
        }
    }
}

void updateOutputs() {
    for (int i = 0; i < numOutputModules; i++) {
        if (outputModules[i].enabled && outputModules[i].update != nullptr) {
            outputModules[i].update();
        }
    }
}
