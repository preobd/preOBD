/*
 * bus_manager.h - Bus Initialization Manager
 *
 * Manages initialization of the active I2C, SPI, and CAN buses.
 * Uses the simplified "pick one" model - each bus type has one active instance.
 *
 * Key Features:
 * - Platform-specific initialization (Teensy 4.x, 3.6, ESP32, Mega)
 * - Pin conflict validation before bus initialization
 * - Simple accessor functions for the active bus instance
 *
 * Usage:
 *   1. Call initConfiguredBuses() during setup()
 *   2. Sensors use getActiveI2C() to get the active Wire object
 *   3. SPI devices use getActiveSPI() to get the active SPI object
 */

#ifndef BUS_MANAGER_H
#define BUS_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "../storage/bus_config.h"
#include "../hal/bus_defaults.h"

// Forward declarations
class TwoWire;
class SPIClass;

// ============================================================================
// BUS INITIALIZATION
// ============================================================================

/**
 * Initialize all configured buses
 *
 * Called once during setup(). Reads SystemConfig.buses and initializes
 * the active I2C, SPI, and CAN bus with configured speeds.
 */
void initConfiguredBuses();

/**
 * Initialize a specific I2C bus
 *
 * @param bus_id Bus number (0-2 for Wire/Wire1/Wire2)
 * @param clock_khz I2C clock speed in kHz (100, 400, 1000)
 * @return true if successfully initialized, false on error
 */
bool initI2CBus(uint8_t bus_id, uint16_t clock_khz);

/**
 * Initialize a specific SPI bus
 *
 * @param bus_id Bus number (0-2 for SPI/SPI1/SPI2)
 * @param clock_hz SPI clock speed in Hz
 * @return true if successfully initialized, false on error
 */
bool initSPIBus(uint8_t bus_id, uint32_t clock_hz);

/**
 * Initialize a specific CAN bus
 *
 * @param bus_id Bus number (0-2 for CAN1/CAN2/CAN3)
 * @param baudrate CAN baudrate in bps
 * @return true if successfully initialized, false on error
 */
bool initCANBus(uint8_t bus_id, uint32_t baudrate);

/**
 * True if initCANBus() claimed this bus's pins at boot. CAN input/output must
 * not start a controller on a bus it didn't: the claim is refused when a
 * higher-ranked function (e.g. Serial1 on Teensy 4 CAN2's pins 0/1) holds them.
 */
bool canBusClaimed(uint8_t bus_id);

// ============================================================================
// ACTIVE BUS ACCESS
// ============================================================================

/**
 * Get the currently active I2C bus
 * @return Pointer to the active TwoWire object, or nullptr if no bus is up
 *
 * Pure accessor — never brings a bus up. Use acquireI2C() if you need one.
 */
TwoWire* getActiveI2C();

/**
 * Acquire the I2C bus on behalf of a client that needs it (LCD, I2C sensors).
 *
 * Brings the bus up on first use and registers SDA/SCL as PIN_RESERVED. Pin
 * reservation is a property of the bus being *up*, so bring-up must happen here
 * and nowhere else: a client that calls Wire.begin() itself leaves the peripheral
 * live while the registry still shows SDA/SCL free, which is how an input could be
 * assigned to SCL and hang the bus (#351).
 *
 * Honors an explicit `BUS I2C <n>` selection; absent one, honors a build-time
 * `-D I2C_WIRED_BUS=<n>` WIRING declaration (see bus_wiring.h). Absent BOTH,
 * this REFUSES rather than guessing — no DEFAULT_I2C_BUS fallback (#377: that
 * fallback is what silently claimed Wire out from under configured inputs on
 * 18/19). A refusal prints actionable guidance via reportNoI2CBus().
 *
 * @return the active TwoWire, or nullptr if no bus could be brought up
 */
TwoWire* acquireI2C();

/**
 * Print the "no I2C bus available" guidance, once per boot.
 *
 * Shared by acquireI2C() (called there when it refuses) and by any client that
 * wants to explain why an I2C-needing operation cannot proceed right now (the
 * boot diagnosis in main.cpp, the runtime guidance in setInputSensor()) so the
 * wording cannot drift between them. De-duplicated within one episode, because
 * several boot-time paths can reach the same refusal and must not print it once
 * per stored sensor — see rearmBusDiagnostics().
 *
 * @return true if the guidance was actually emitted, false if this episode had
 *         already said it. Callers ignore it; it is what makes the episode rule
 *         observable to the native tests.
 */
bool reportNoI2CBus();

/**
 * End the current de-duplication episode for reportNo{I2C,SPI}Bus().
 *
 * Called once at the end of setup(). Boot is one episode; everything after it is
 * interactive, and the CLI is precisely where this guidance has to land — a user
 * configuring an I2C input with no bus available must be told what to run, even
 * on a board whose boot already said it once.
 */
void rearmBusDiagnostics();

/**
 * Get the currently active SPI bus
 * @return Pointer to the active SPIClass object, or nullptr if no bus is up
 *
 * Pure accessor — never brings a bus up. Use acquireSPI() if you need one.
 */
SPIClass* getActiveSPI();

/**
 * Acquire the SPI bus on behalf of a client that needs it (thermocouples, ...).
 *
 * The I2C contract, for SPI: brings the bus up on first use, reserving
 * MOSI/MISO/SCK, and honors an explicit `BUS SPI <n>` selection; absent one,
 * honors a build-time `-D SPI_WIRED_BUS=<n>` WIRING declaration. Absent BOTH,
 * this REFUSES rather than guessing — no DEFAULT_SPI_BUS fallback (#377).
 *
 * @return the active SPIClass, or nullptr if no bus could be brought up
 */
SPIClass* acquireSPI();

/** SPI counterpart of reportNoI2CBus() — see its doc comment. */
bool reportNoSPIBus();

/**
 * The OTHER refusal: a bus WAS named (selected or declared) and could not start.
 *
 * Distinct from reportNo{I2C,SPI}Bus() on purpose — opposite problems, opposite
 * fixes. Reporting "not declared or selected" for a declared bus that lost a pin
 * fight tells the user their declaration does not exist, which is the exact
 * misdirection #377 exists to remove. Latched separately from the no-bus
 * guidance, so neither can suppress the other; returns whether it emitted.
 */
bool reportI2CBusUnavailable(uint8_t bus_id);
bool reportSPIBusUnavailable(uint8_t bus_id);

/**
 * Bring up every bus the stored configuration needs, in one pass.
 *
 * Called ONCE at boot with the OR of every voter's needs (stored input records
 * and the display — see bus_needs.h), BEFORE any client initializes: before
 * initLCD() and before loadInputConfig() validates a single stored pin. Bus
 * ownership therefore exists before anything can be assigned over it, and no
 * client's init order can change the outcome (#358).
 *
 * A bus that cannot be acquired is reported and skipped; boot always continues.
 */
void acquireRequiredBuses(uint8_t needs);

/**
 * Get the currently active I2C bus ID
 * @return Bus ID (0-2)
 */
uint8_t getActiveI2CId();

/**
 * Get the currently active SPI bus ID
 * @return Bus ID (0-2)
 */
uint8_t getActiveSPIId();

/**
 * Get the currently active CAN bus ID
 * @return Bus ID (0-2)
 */
uint8_t getActiveCANId();

// ============================================================================
// BUS INFORMATION
// ============================================================================

/**
 * Get human-readable name for I2C bus
 * @param bus_id Bus number (0-2)
 * @return String like "Wire", "Wire1", "Wire2"
 */
const char* getI2CBusName(uint8_t bus_id);

/**
 * Get human-readable name for SPI bus
 * @param bus_id Bus number (0-2)
 * @return String like "SPI", "SPI1", "SPI2"
 */
const char* getSPIBusName(uint8_t bus_id);

/**
 * Get human-readable name for CAN bus
 * @param bus_id Bus number (0-2)
 * @return String like "CAN1", "CAN2", "CAN3"
 */
const char* getCANBusName(uint8_t bus_id);

/**
 * The stored-bus-selection PinOwnerBusProbe (pin_owner.h, #379).
 *
 * Answers pinsConflict()'s third source: a stored `BUS I2C/SPI/CAN <n>`
 * selection whose bring-up is deferred to reboot still names pins nobody
 * should be able to steal in the meantime. Consults systemConfig.buses
 * directly (I2C, SPI, and both CAN directions) — NOT whether the bus has
 * actually started, since an already-started bus already appears in the pin
 * registry and is judged there instead; this probe only covers the gap
 * between "selected" and "started".
 *
 * `excludeOwner`, when it names the very bus a stored selection would also
 * describe (getI2CBusName()/getSPIBusName()/getCANBusName() — the same
 * strings initI2CBus()/initSPIBus()/initCANBus() claim under), is treated as
 * "that's not a conflict, that's the bus's own bring-up asking" — see
 * pin_owner.h's PinOwnerBusProbe contract.
 *
 * Registered via setPinOwnerBusProbe() once systemConfig is loaded (main.cpp).
 */
bool storedBusHoldsPin(uint8_t pin, const char* excludeOwner, const char** descOut);

// ============================================================================
// BUS STATUS DISPLAY
// ============================================================================

/**
 * Display I2C bus configuration status
 */
void displayI2CStatus();

/**
 * Display SPI bus configuration status
 */
void displaySPIStatus();

/**
 * Display CAN bus configuration status
 */
void displayCANStatus();

#endif // BUS_MANAGER_H
