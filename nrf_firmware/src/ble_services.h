// ble_services.h -- GATT service / characteristic registration.
//
// Serial-port channels exposed (all carry text/ELM327 ASCII to the host MCU):
//   Control Service CMD_TX/CMD_RX  -- preOBD webapp CLI            (text, 0x01)
//   NUS RX/TX  (6e400…)            -- generic BLE terminal apps    (text, 0x01)
//   HM-10 FFE0/FFE1                -- phone OBD apps (ELM327)       (ELM,  0x30)
//   System Status (32-byte struct) -- pushed on change
//
// In framed mode the UART frame TYPE separates these (CMD_TX/NUS -> 0x01 text,
// FFE1 -> 0x30 ELM327). In transparent mode there are no frame types, so the
// device is a single-client raw bridge and which channel a client uses is
// indistinguishable on the wire (see ARCHITECTURE.md §3.1).

#ifndef BLE_SERVICES_H
#define BLE_SERVICES_H

#include <stdint.h>
#include <stddef.h>

#include <bluefruit.h>

// Maximum simultaneous peripheral links; must match Bluefruit.begin(2) in
// main.cpp. Connection handles double as compact slot indices [0, N) in the
// notify fan-out masks and the slot-generation tracking below.
constexpr uint16_t BLE_SERVICES_MAX_CONN_SLOTS = 2;

void bleServicesInit();

// Notify helpers. Each pushes one chunk to a set of characteristics, but only
// those a client has actually subscribed to (CCCD enabled) -- unsubscribed
// chars are skipped so a fan-out can't be mistaken for back-pressure. Return
// false only when a *subscribed* characteristic's HVN TX queue is full (caller
// must retry the same bytes on a later poll, or the data is silently dropped).
// Return true when delivered to all subscribers, when nobody is subscribed
// (nothing to do -- caller may discard), or when no client is connected.
// Caller guarantees `len` <= bleServicesMaxNotifyChunk().
//
//   Text: framed type 0x02 responses          -> CMD_RX + NUS TX
//   Hm10: framed type 0x30 ELM327 responses    -> FFE1
//   Transparent: raw passthrough (single client) -> CMD_RX + NUS TX + FFE1
//
// Targets are (characteristic x connected-central) pairs: each chunk fans out
// to every central subscribed to the characteristic, not just one. (Bluefruit's
// plain notify() targets only the most-recently connected central, so the
// explicit per-handle fan-out is what makes two concurrent centrals work.)
//
// `doneMask` is per-chunk progress tracked by the caller: it records which
// (char, central) pairs have already accepted *this* chunk so that a retry
// (after a partial back-pressure) only re-sends to the pairs that haven't,
// never duplicating a notify to a link that already took it. The caller seeds
// it to 0 for each new chunk and passes the same variable back on every retry
// of that chunk; the helper sets one bit per accepted/absent/unsubscribed pair.
bool bleServicesNotifyText(const uint8_t* data, size_t len, uint8_t& doneMask);
bool bleServicesNotifyHm10(const uint8_t* data, size_t len, uint8_t& doneMask);
bool bleServicesNotifyTransparent(const uint8_t* data, size_t len,
                                  uint8_t& doneMask);

// Per-payload target snapshots: return a doneMask seed with bits pre-set for
// every (char, central) pair that is currently absent or unsubscribed on the
// Text (CMD_RX + NUS TX) or HM-10 (FFE1) fan-out set. Seeding a multi-chunk
// payload's per-chunk mask with this (instead of 0) freezes the target set at
// stash time, so a central that connects and subscribes mid-drain is excluded
// until the next clean payload instead of receiving a fragmented tail.
uint8_t bleServicesTextTargetSeed();
uint8_t bleServicesHm10TargetSeed();

// Per-slot connection generation: bumps each time a peripheral slot
// transitions disconnected -> connected. A payload drain compares the
// generation recorded at stash time against the current one to detect that
// the central at a handle is no longer the one it snapshotted (same-handle
// disconnect + reconnect mid-drain) and exclude the newcomer from the rest
// of that payload -- the targetSeed snapshot alone can't tell the two
// centrals apart because the eligibility probes (connected / subscribed) key
// on the handle. Polled from loop() (single-context, like ble_bridge's
// connection-count edge detection); connection setup spans many loop
// iterations, so a transition can't slip between polls.
void bleServicesPollSlots();
uint8_t bleServicesSlotGeneration(uint16_t connHandle);

// Retryable single-characteristic fan-out: like bleServicesNotifyAll but with
// the notifyText/Hm10 doneMask contract, so a caller can re-invoke with the
// same mask until every subscribed central has accepted the payload. Used for
// edge-triggered events (alarms) where a silently dropped notify would be
// missed until the next state transition.
bool bleServicesNotifyRetry(BLECharacteristic& chr, const uint8_t* data,
                            uint16_t len, uint8_t& doneMask);

// Best-effort fan-out of a single fixed-size notify to every connected central
// subscribed to `chr`. For the binary snapshot characteristics owned by the
// alarm / relay / OTA handlers, which must reach all centrals -- Bluefruit's
// plain chr.notify(data,len) only targets the most-recently connected one.
// Drops silently under back-pressure; callers re-push on the next change.
void bleServicesNotifyAll(BLECharacteristic& chr, const uint8_t* data,
                          uint16_t len);

// Largest CMD_RX notify payload the current link can carry in a single ATT
// packet: negotiated ATT MTU minus the 3-byte ATT header, floored at 20
// (default-MTU link) and capped at 244 (the characteristic's max len).
// Re-query per drain rather than caching -- the MTU is renegotiated on each
// reconnect and may differ between centrals.
size_t bleServicesMaxNotifyChunk();

// Update the System Status characteristic with a fresh 32-byte snapshot
// pushed by the host MCU via framed transport (type 0x03). Writes the cache
// (so subsequent GATT reads return the latest value) and pushes a notify
// to any subscribed centrals. Caller guarantees `len == PREOBD_STATUS_SIZE`.
// No-op (silent) when no client is connected — the write still updates the
// cache so a future connect's first read returns current data.
void bleServicesUpdateStatus(const uint8_t* payload, uint16_t len);

// References to service objects so main.cpp can hand them to the advertiser.
// Control Service rides the (full) advertising packet; NUS / HM-10 go in the
// scan response so OBD/terminal apps that filter on those UUIDs discover us.
BLEService& bleServicesControlService();
BLEService& bleServicesNusService();
BLEService& bleServicesHm10Service();

#endif // BLE_SERVICES_H
