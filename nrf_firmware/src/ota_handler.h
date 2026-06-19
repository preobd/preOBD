// ota_handler.h -- host MCU OTA delivery (Phase 1 stub).
//
// Phase 1: registers the Extended Service's OTA_CTRL / OTA_DATA
// characteristics so the phone app's OTA UI is exercisable, but accepts no
// real image data -- START is acknowledged, chunks are discarded, COMPLETE
// reports crc32_ok=false. Real SPI push to the host MCU lands in Phase 3.

#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

void otaHandlerInit();

#endif // OTA_HANDLER_H
