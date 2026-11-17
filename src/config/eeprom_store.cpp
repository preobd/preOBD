/*
 * eeprom_store.cpp - see eeprom_store.h
 */

#include "eeprom_store.h"
#include "system_config.h"      // EEPROM_WDT_WINDOW_MS
#include "../core/watchdog.h"
#include "../core/message_api.h"

#ifndef FLASHMEM
#define FLASHMEM
#endif

#if defined(ARDUINO_ARCH_STM32)

#include <Arduino.h>

// The flash sector holding the store. board.h names it: it must be one the
// linker never places code in (board.ini caps the image below it).
#if !defined(EEPROM_FLASH_SECTOR) || !defined(EEPROM_FLASH_ADDR)
#error "eeprom_store.cpp: STM32 boards must define EEPROM_FLASH_SECTOR and EEPROM_FLASH_ADDR in board.h"
#endif

FlashEeprom EEPROM;

void FlashEeprom::load() {
    if (loaded_) return;
    memcpy(image_, (const void*)EEPROM_FLASH_ADDR, EEPROM_TOTAL_BYTES);
    loaded_ = true;
}

FLASHMEM bool FlashEeprom::commit() {
    if (!dirty_) return true;

    // An erase stalls every flash fetch, ISRs included, for ~1-2 s on a
    // 128 KB sector, while the watchdog's own clock keeps counting.
    WatchdogScope wd(EEPROM_WDT_WINDOW_MS, "FLASH");

    bool ok = false;
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    FLASH_EraseInitTypeDef erase = {};
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;   // 2.7-3.6 V: 32-bit program
    erase.Sector = EEPROM_FLASH_SECTOR;
    erase.NbSectors = 1;
    uint32_t badSector = 0;
    if (HAL_FLASHEx_Erase(&erase, &badSector) == HAL_OK) {
        ok = true;
        for (uint32_t off = 0; off < EEPROM_TOTAL_BYTES; off += 4) {
            uint32_t word;
            memcpy(&word, image_ + off, 4);
            if (word == 0xFFFFFFFFUL) continue;   // erased flash already reads so
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, EEPROM_FLASH_ADDR + off, word) != HAL_OK) {
                ok = false;
                break;
            }
        }
    }
    HAL_FLASH_Lock();

    ok = ok && memcmp((const void*)EEPROM_FLASH_ADDR, image_, EEPROM_TOTAL_BYTES) == 0;
    if (ok) dirty_ = false;   // a failed write stays dirty: the next save retries
    return ok;
}

static uint8_t s_batchDepth = 0;
static bool s_commitFailed = false;

EepromBatch::EepromBatch() {
    if (s_batchDepth++ == 0) s_commitFailed = false;
}

EepromBatch::~EepromBatch() {
    if (--s_batchDepth == 0 && !EEPROM.commit()) {
        s_commitFailed = true;
        msg.control.println(F("ERROR: flash write failed, configuration NOT saved"));
    }
}

bool eepromCommitFailed() { return s_commitFailed; }

#endif
