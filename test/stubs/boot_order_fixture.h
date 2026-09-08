// boot_order_fixture.h — shared "table not live yet" fixture for the
// broadcaster native test suites (#352, #369).
//
// main.cpp brings every broadcaster's init() up (StatusBroadcaster,
// AlarmBroadcaster, ActionBroadcaster, LiveValueBroadcaster) BEFORE
// initInputManager() and the control-table load. Each broadcaster's init()
// must therefore NOT read inputs[]/rules[]/actions[] — it primes with a
// constant instead — because at init() time those tables are raw BSS, not
// yet the sentinel-filled, loaded ground truth (a BSS-zeroed inputs[] slot
// reads pin == 0, indistinguishable from a real Input on digital pin 0; a
// BSS-zeroed rules[]/actions[] slot reads id == 0, a valid record id). That
// was #352: LiveValueBroadcaster::init() primed occupancy from the table and
// read every slot as occupied, arming a phantom heartbeat for slots nobody
// had configured.
//
// Nothing enforced this invariant beyond a comment, and no test could catch a
// NEW init() that started reading a table, because every broadcaster suite
// configured its fixture BEFORE calling init() — the opposite of the real
// boot order. That is exactly why #352 survived 221 passing tests.
//
// input_manager.cpp exposes the real "is the table live" flag as
// inputsTableIsLiveForAssert() / the ASSERT_INPUTS_TABLE_LIVE() macro
// (input_manager.h), compiled in for DEBUG and NATIVE_TEST builds only, and
// called from each broadcaster's table-reading function (packKey/evalSlot/
// packState/packStatus). These broadcaster test binaries link the
// broadcaster .cpp directly and do NOT link input_manager.cpp, so each one
// needs the symbol supplied — this header is that stub, shared so every
// broadcaster suite reuses the SAME mechanism instead of re-deriving one.
//
// Usage: include this header once (after unity.h), then in the suite's
// bootAsFirmwareDoes(): call setInputsTableLive(false) BEFORE the broadcaster's
// init(), and setInputsTableLive(true) once the fixture "loads" the table —
// mirroring loadInputConfig() flipping inputsTableIsLive at the end of
// initInputManager(). See test_live_value_broadcaster.cpp
// (test_boot_order_never_clears_never_configured_slots, PR #367) for the
// original instance of this pattern.
//
// Defaults to "live" (true) so every pre-existing test in a suite — which
// configures its table BEFORE calling init(), same as before this file
// existed — keeps behaving exactly as it did.
#ifndef TEST_BOOT_ORDER_FIXTURE_H
#define TEST_BOOT_ORDER_FIXTURE_H

static bool g_inputsTableIsLive = true;

bool inputsTableIsLiveForAssert() { return g_inputsTableIsLive; }

static void setInputsTableLive(bool live) { g_inputsTableIsLive = live; }

#endif // TEST_BOOT_ORDER_FIXTURE_H
