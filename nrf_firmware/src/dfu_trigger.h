// dfu_trigger.h -- Nordic Secure DFU registration + NRF_DFU pin watchdog.

#ifndef DFU_TRIGGER_H
#define DFU_TRIGGER_H

void dfuTriggerInit();

// Poll the NRF_DFU input pin; if asserted, reboot into DFU mode.
// Call from loop().
void dfuTriggerPoll();

#endif // DFU_TRIGGER_H
