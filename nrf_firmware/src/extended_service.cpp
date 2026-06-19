// extended_service.cpp -- See header for the why.

#include "extended_service.h"

#include "ble_gatt_defs.h"
#include "uuid_util.h"

namespace {

uint8_t g_zeroUuid[16] = {};

BLEService g_extService(g_zeroUuid);

} // namespace

void extendedServiceInit() {
    // BLEUuid stores a pointer, not a copy; needs persistent backing storage.
    static uint8_t kExtServiceUuid[16];
    if (parseUuid128(PREOBD_EXT_SERVICE_UUID, kExtServiceUuid)) {
        g_extService = BLEService(kExtServiceUuid);
    }
    g_extService.begin();
}
