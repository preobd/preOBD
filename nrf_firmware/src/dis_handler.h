// dis_handler.h -- Standard BLE Device Information Service (0x180A).
//
// Exposes the nRF firmware's own identity so clients can verify which
// coprocessor build is flashed -- complementing the host firmware version
// already carried in PreobdSystemStatus. nRF and host ship independently
// (DFU vs USB upload) so a single version row isn't sufficient.
//
// Three characteristics are registered via Bluefruit's BLEDis wrapper:
//   - Manufacturer Name (0x2A29): "preOBD"
//   - Model Number      (0x2A24): "nRF52840 coprocessor"
//   - Firmware Revision (0x2A26): NRF_FW_VERSION_STRING, e.g.
//                                 "0.1.0-beta (b237 @abc1234)"

#ifndef DIS_HANDLER_H
#define DIS_HANDLER_H

void disHandlerInit();

#endif // DIS_HANDLER_H
