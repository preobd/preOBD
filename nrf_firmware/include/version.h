/*
 * version.h -- nRF52840 coprocessor firmware version constants.
 *
 * Mirrors the host firmware's three-layer versioning model in src/version.h:
 *   1. API/Feature Version (NRF_FW_MAJOR.MINOR.PATCH) -- user-visible, manual bump
 *   2. Build Number (FW_BUILD_NUMBER) -- git commit count, injected at build
 *   3. Commit ID (FW_GIT_HASH) -- short SHA, injected at build
 *
 * Exposed to BLE clients via the standard Device Information Service (0x180A)
 * Firmware Revision characteristic so the webapp can verify which nRF build
 * is flashed -- complementing the host firmware version that's already
 * carried in PreobdSystemStatus.
 *
 * The nRF and host are versioned independently because they ship from
 * separate codebases on independent flash cadences (DFU vs USB upload).
 */

#ifndef NRF_VERSION_H
#define NRF_VERSION_H

// =============================================================================
// API / Feature Version (manual bump per release)
// =============================================================================
#define NRF_FW_MAJOR  0
#define NRF_FW_MINOR  1
#define NRF_FW_PATCH  0

// Prerelease tag: "alpha", "beta", "rc1", or "" for release.
#define NRF_FW_PRERELEASE "beta"

// =============================================================================
// Build Number + Git Hash (injected by ../scripts/version_inject.py).
// Defaults preserve the build if the script isn't wired up.
// =============================================================================
#ifndef FW_BUILD_NUMBER
#define FW_BUILD_NUMBER 0
#endif

#ifndef FW_GIT_HASH
#define FW_GIT_HASH "unknown"
#endif

// Stringification helpers for compile-time version assembly.
#define NRF_FW_STR_(x) #x
#define NRF_FW_STR(x)  NRF_FW_STR_(x)

// Human-readable version string, e.g. "0.1.0-beta (b237 @abc1234)".
#define NRF_FW_VERSION_STRING                                   \
    NRF_FW_STR(NRF_FW_MAJOR) "." NRF_FW_STR(NRF_FW_MINOR) "."   \
    NRF_FW_STR(NRF_FW_PATCH) "-" NRF_FW_PRERELEASE              \
    " (b" NRF_FW_STR(FW_BUILD_NUMBER) " @" FW_GIT_HASH ")"

#endif // NRF_VERSION_H
