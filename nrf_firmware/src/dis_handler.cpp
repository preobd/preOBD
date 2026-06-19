// dis_handler.cpp -- Device Information Service registration.

#include "dis_handler.h"

#include <bluefruit.h>

#include "version.h"

namespace {

BLEDis g_dis;

} // namespace

void disHandlerInit() {
    g_dis.setManufacturer("preOBD");
    g_dis.setModel("nRF52840 coprocessor");
    g_dis.setFirmwareRev(NRF_FW_VERSION_STRING);
    g_dis.begin();
}
