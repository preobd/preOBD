// Native unit test: Rule<->Action MEMBERSHIP (src/rules/rules_manager.cpp).
//
// The pure resolver (rulesActionDesired) is covered by test_rules_engine
// against hand-built ActionDrive literals. This suite covers the layer BELOW
// that -- the real rules[]/actions[]/actionLinks[] tables and the mutators over
// them -- because that is where #343's load-bearing invariants live:
//
//   * ruleRef == RULES_ID_NONE MUST mean "zero members". rulesActionRuleCount()
//     short-circuits on it, so a link row left behind a NONE ruleRef would strand
//     the surviving members AND hand the action back to its manual latch --
//     reopening #294 by the documented route.
//   * every membership change releases override + manual latch (#294/#329), and a
//     REJECTED change must leave both untouched.
//   * resolveActionDrive() -- the translation from live tables to the ActionDrive
//     the tested resolver consumes -- was previously only ever hand-written.
//
// The manager is glue, so the firmware deps it reaches for (inputs, pin registry,
// application presets, messaging) are stubbed here; -Wl,-dead_strip drops the
// paths a membership test never calls.
#include <unity.h>
#include <Arduino.h>
#include <string.h>
#include <math.h>

#include "../../src/rules/rules_engine.cpp"
#include "../../src/rules/rules_manager.cpp"
// The real pin registry and owner: the action pin lifecycle (§12.5, #648)
// claims, configures and releases through them.
#include "../../src/buses/pinctrl/pin_registry.cpp"
#include "../../src/buses/pinctrl/pin_owner.cpp"

// ---- messaging: the sweep logs a repair; nothing else here prints ----------
// Just enough of the message plane to link: no transport is registered, so every
// stream resolves to nullptr and writes go nowhere.
MessageAPI    msg;
LogFilter::LogFilter() {}
TransportInterface* messageSink(MessagePlane, bool) { return nullptr; }
size_t MessageStream::warn(const char* tag, const char* fmt, ...) {
    (void)tag; (void)fmt; return 0;
}
size_t MessageStream::error(const char*, const char*, ...) { return 0; }
// Board pins: real GPIOs only; parsePin()'s virtual pins (I2C, CAN) start at 0xC0.
bool boardHasPin(uint8_t pin) { return pin < 0xC0; }

// ---- stub inputs[] table: rules resolve their source by PIN ----------------
static Input  g_inputs[MAX_INPUTS];
static uint8_t g_inputCount = 0;

Input* getInputByPin(uint8_t pin) {
    for (uint8_t i = 0; i < g_inputCount; i++)
        if (g_inputs[i].pin == pin) return &g_inputs[i];
    return nullptr;
}

static void addInput(uint8_t pin, float value) {
    g_inputs[g_inputCount].pin = pin;
    g_inputs[g_inputCount].value = value;
    g_inputCount++;
}

// ---- helpers ---------------------------------------------------------------

// A rule that is ACTIVE (or not) without running the state machine: the
// membership layer keys off RuleContext.state, so set it directly.
static uint8_t addRule(uint8_t id, uint8_t pin, bool active, bool enabled = true) {
    int slot = rulesRuleFreeSlot();
    Rule& r = rules[slot];
    memset(&r, 0, sizeof(r));
    r.id = id;
    r.sourceInput = pin;
    r.condition = RULE_ABOVE;
    r.severity = RULE_SEV_NONE;
    r.limitLo = 100.0f;
    r.flags = enabled ? RULE_FLAG_ENABLED : 0;
    g_ruleContexts[slot].state = !enabled ? RULE_DISABLED
                                          : (active ? RULE_ACTIVE : RULE_READY);
    return id;
}

static uint8_t addAction(uint8_t id, uint8_t target, uint8_t failsafe = ACT_FAILSAFE_OFF) {
    int slot = rulesActionFreeSlot();
    Action& a = actions[slot];
    memset(&a, 0, sizeof(a));
    a.id = id;
    a.type = ACT_RELAY;
    a.target = target;
    a.ruleRef = RULES_ID_NONE;
    a.failsafe = failsafe;
    return id;
}

// THE invariant, asserted after every mutation: a link row may never exist
// behind a NONE ruleRef, and no action may hold a duplicate member.
static void assertNoOrphanedLinks(void) {
    for (int i = 0; i < MAX_ACTION_LINKS; i++) {
        const ActionLink& l = actionLinks[i];
        if (l.actionId == RULES_ID_NONE) continue;
        const int slot = rulesActionSlot(l.actionId);
        TEST_ASSERT_TRUE_MESSAGE(slot >= 0, "link row for an action that does not exist");
        TEST_ASSERT_TRUE_MESSAGE(actions[slot].ruleRef != RULES_ID_NONE,
                                 "link row stranded behind ruleRef == NONE (#294 route)");
        TEST_ASSERT_TRUE_MESSAGE(actions[slot].ruleRef != l.ruleId,
                                 "link row duplicates the inline member");
        for (int j = 0; j < i; j++)
            TEST_ASSERT_FALSE_MESSAGE(actionLinks[j].actionId == l.actionId &&
                                      actionLinks[j].ruleId == l.ruleId,
                                      "duplicate link rows for one action");
    }
}

void setUp(void) {
    rulesResetTables();     // releases any pin a previous test's action held
    initPinRegistry();
    stubPinReset();
    rulesActionsInit();     // zeroes the manual latch + override for every slot
    memset(g_inputs, 0, sizeof(g_inputs));
    g_inputCount = 0;
    g_test_now = 0;
}
void tearDown(void) {}

// --- membership accessors ---------------------------------------------------

static void test_add_first_member_goes_inline(void) {
    addInput(10, 50.0f);
    addRule(0, 10, false);
    addAction(0, 23);
    TEST_ASSERT_TRUE(rulesActionAddRule(0, 0));
    TEST_ASSERT_EQUAL_UINT8(1, rulesActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(0, actions[0].ruleRef);       // inline, no link row spent
    TEST_ASSERT_EQUAL_UINT8(0, rulesActionLinkCount());
    assertNoOrphanedLinks();
}

static void test_add_is_idempotent(void) {
    addInput(10, 50.0f);
    addRule(0, 10, false);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    TEST_ASSERT_TRUE(rulesActionAddRule(0, 0));         // same rule again
    TEST_ASSERT_EQUAL_UINT8(1, rulesActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(0, rulesActionLinkCount());
    assertNoOrphanedLinks();
}

static void test_members_are_enumerable_in_order(void) {
    addInput(10, 50.0f); addInput(11, 50.0f); addInput(12, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false); addRule(2, 12, false);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 1);
    rulesActionAddRule(0, 2);
    TEST_ASSERT_EQUAL_UINT8(3, rulesActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(0, rulesActionRuleAt(0, 0));
    TEST_ASSERT_EQUAL_UINT8(1, rulesActionRuleAt(0, 1));
    TEST_ASSERT_EQUAL_UINT8(2, rulesActionRuleAt(0, 2));
    TEST_ASSERT_EQUAL_UINT8(RULES_ID_NONE, rulesActionRuleAt(0, 3));
    TEST_ASSERT_TRUE(rulesActionHasRule(0, 2));
    TEST_ASSERT_FALSE(rulesActionHasRule(0, 9));
    assertNoOrphanedLinks();
}

// THE invariant: removing the INLINE member promotes a link row instead of
// blanking ruleRef. Blanking would make rulesActionRuleCount() report 0 --
// "manual-only" -- stranding the survivors and re-arming the #294 route.
static void test_removing_inline_member_promotes_a_link(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 1);

    TEST_ASSERT_TRUE(rulesActionRemoveRule(0, 0));      // drop the INLINE one
    TEST_ASSERT_EQUAL_UINT8(1, rulesActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(1, actions[0].ruleRef);       // promoted, not blanked
    TEST_ASSERT_EQUAL_UINT8(0, rulesActionLinkCount()); // row consumed
    assertNoOrphanedLinks();
}

static void test_removing_last_member_reverts_to_manual(void) {
    addInput(10, 50.0f);
    addRule(0, 10, false);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    TEST_ASSERT_TRUE(rulesActionRemoveRule(0, 0));
    TEST_ASSERT_EQUAL_UINT8(0, rulesActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(RULES_ID_NONE, actions[0].ruleRef);
    assertNoOrphanedLinks();
}

static void test_set_rule_replaces_whole_membership(void) {
    addInput(10, 50.0f); addInput(11, 50.0f); addInput(12, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false); addRule(2, 12, false);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 1);
    TEST_ASSERT_TRUE(rulesActionSetRule(0, 2));
    TEST_ASSERT_EQUAL_UINT8(1, rulesActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(2, actions[0].ruleRef);
    TEST_ASSERT_EQUAL_UINT8(0, rulesActionLinkCount());
    assertNoOrphanedLinks();
}

static void test_clear_rules_frees_every_row(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 1);
    rulesActionClearRules(0);
    TEST_ASSERT_EQUAL_UINT8(0, rulesActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(RULES_ID_NONE, actions[0].ruleRef);
    TEST_ASSERT_EQUAL_UINT8(0, rulesActionLinkCount());
    assertNoOrphanedLinks();
}

// --- #294 / #329: every membership change releases the drive -----------------

static void test_every_mutator_releases_override_and_latch(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false);
    addAction(0, 23);

    // ADD releases
    rulesActionSetManual(0, true);
    rulesActionSetOverride(0, ACT_OVR_FORCE_ON);
    bool released = false;
    rulesActionAddRule(0, 0, &released);
    TEST_ASSERT_TRUE(released);
    TEST_ASSERT_EQUAL_INT(ACT_OVR_AUTO, rulesActionOverride(0));

    // ADD of a second member releases again
    rulesActionSetOverride(0, ACT_OVR_FORCE_ON);
    released = false;
    rulesActionAddRule(0, 1, &released);
    TEST_ASSERT_TRUE(released);
    TEST_ASSERT_EQUAL_INT(ACT_OVR_AUTO, rulesActionOverride(0));

    // REMOVE releases
    rulesActionSetOverride(0, ACT_OVR_FORCE_OFF);
    released = false;
    rulesActionRemoveRule(0, 1, &released);
    TEST_ASSERT_TRUE(released);
    TEST_ASSERT_EQUAL_INT(ACT_OVR_AUTO, rulesActionOverride(0));

    // SET releases -- and reports it even when only the MANUAL LATCH was armed,
    // which is the case cmd_action previously reported silently.
    rulesActionSetManual(0, true);
    released = false;
    rulesActionSetRule(0, 1, &released);
    TEST_ASSERT_TRUE_MESSAGE(released, "latch-only release must still be reported");
    TEST_ASSERT_EQUAL_INT(ACT_OVR_AUTO, rulesActionOverride(0));
}

// A rejected ADD (link table full) must not touch membership OR drive state.
static void test_rejected_add_leaves_state_untouched(void) {
    addInput(10, 50.0f);
    for (uint8_t i = 0; i < MAX_ACTION_LINKS + 2 && i < MAX_RULES; i++)
        addRule(i, 10, false);
    addAction(0, 23);

    // Fill: one inline + MAX_ACTION_LINKS rows.
    uint8_t placed = 0;
    for (uint8_t r = 0; r < MAX_RULES && placed <= MAX_ACTION_LINKS; r++)
        if (rulesActionAddRule(0, r)) placed++;

    const uint8_t before = rulesActionRuleCount(0);
    rulesActionSetOverride(0, ACT_OVR_FORCE_ON);

    // A rule that is not yet a member, with no room left for it.
    bool added = false, released = true;
    for (uint8_t r = 0; r < MAX_RULES; r++) {
        if (rulesRuleSlot(r) >= 0 && !rulesActionHasRule(0, r)) {
            added = rulesActionAddRule(0, r, &released);
            break;
        }
    }
    if (!added) {
        // Table geometry left no spare rule to attempt with -- nothing to assert.
        TEST_ASSERT_EQUAL_UINT8(before, rulesActionRuleCount(0));
        return;
    }
    TEST_ASSERT_EQUAL_UINT8(before, rulesActionRuleCount(0));
    TEST_ASSERT_FALSE(released);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ACT_OVR_FORCE_ON, rulesActionOverride(0),
                                  "a rejected ADD must not release a live force");
    assertNoOrphanedLinks();
}

// --- rule deletion across a multi-member action -----------------------------

static void test_clearing_one_rule_leaves_other_members(void) {
    addInput(10, 50.0f); addInput(11, 50.0f); addInput(12, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false); addRule(2, 12, false);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 1);
    rulesActionAddRule(0, 2);

    rulesClearActionsForRule(1);                       // RULE DEL 1
    TEST_ASSERT_EQUAL_UINT8(2, rulesActionRuleCount(0));
    TEST_ASSERT_TRUE(rulesActionHasRule(0, 0));
    TEST_ASSERT_FALSE(rulesActionHasRule(0, 1));
    TEST_ASSERT_TRUE(rulesActionHasRule(0, 2));
    assertNoOrphanedLinks();
}

// Deleting the rule that happens to be the INLINE member still promotes.
static void test_clearing_the_inline_rule_promotes(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 1);
    rulesClearActionsForRule(0);
    TEST_ASSERT_EQUAL_UINT8(1, rulesActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(1, actions[0].ruleRef);
    assertNoOrphanedLinks();
}

// --- sweep / prune (foreign or truncated stores) -----------------------------

static void test_prune_drops_dangling_duplicate_and_inline_dup_rows(void) {
    addInput(10, 50.0f);
    addRule(0, 10, false);
    addAction(0, 23);
    actions[0].ruleRef = 0;
    // Hand-built rows a foreign/hand-edited store could contain:
    actionLinks[0] = {0, 0};                 // duplicates the inline member
    actionLinks[1] = {0, 99};                // rule does not exist
    actionLinks[2] = {77, 0};                // action does not exist
    TEST_ASSERT_EQUAL_UINT8(3, rulesPruneActionLinks());
    TEST_ASSERT_EQUAL_UINT8(1, rulesActionRuleCount(0));
    assertNoOrphanedLinks();
}

static void test_prune_collapses_repeated_rows(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false);
    addAction(0, 23);
    actions[0].ruleRef = 0;
    actionLinks[0] = {0, 1};
    actionLinks[1] = {0, 1};                 // repeat of an earlier surviving row
    TEST_ASSERT_EQUAL_UINT8(1, rulesPruneActionLinks());
    TEST_ASSERT_EQUAL_UINT8(2, rulesActionRuleCount(0));
    // ...and REMOVE now actually removes the membership, rather than leaving a
    // second copy that keeps the action rule-driven.
    rulesActionRemoveRule(0, 1);
    TEST_ASSERT_FALSE(rulesActionHasRule(0, 1));
    assertNoOrphanedLinks();
}

// A store whose INLINE member did not load: promote a surviving row rather than
// blanking ruleRef (which would strand the survivor and revert to manual).
static void test_sweep_promotes_when_inline_member_is_missing(void) {
    addInput(11, 50.0f);
    addRule(1, 11, false);                   // rule 0 deliberately absent
    addAction(0, 23);
    actions[0].ruleRef = 0;                  // dangles
    actionLinks[0] = {0, 1};                 // valid survivor

    TEST_ASSERT_TRUE(rulesSweepActionRules() > 0);
    TEST_ASSERT_EQUAL_UINT8(1, rulesActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(1, actions[0].ruleRef);
    assertNoOrphanedLinks();
}

static void test_sweep_reverts_to_manual_when_nothing_survives(void) {
    addAction(0, 23);
    actions[0].ruleRef = 5;                  // no rules exist at all
    actionLinks[0] = {0, 6};
    rulesSweepActionRules();
    TEST_ASSERT_EQUAL_UINT8(0, rulesActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(RULES_ID_NONE, actions[0].ruleRef);
    assertNoOrphanedLinks();
}

// --- id reuse ---------------------------------------------------------------

static void test_new_action_does_not_inherit_a_dead_actions_links(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 1);

    // ACTION DEL 0 (cmd_action frees the rows before releasing the slot)...
    rulesActionForgetLinks(0);
    actions[0].id = RULES_ID_NONE;
    // ...then ACTION ADD reuses id 0.
    addAction(0, 24);
    TEST_ASSERT_EQUAL_UINT8(0, rulesActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(0, rulesActionLinkCount());
    assertNoOrphanedLinks();
}

// --- resolveActionDrive: live tables -> the aggregate the resolver consumes ---
// Everything above this line in the suite is membership bookkeeping; these drive
// the real relay decision end-to-end (tables -> drive -> rulesActionDesired).

static void test_drive_or_combines_live_members(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, /*active*/false);
    addRule(1, 11, /*active*/true);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 1);

    rulesSyncActionPins();   // the actions hold their pins (§12.5)
    rulesActionsUpdate(/*configMode*/false);
    TEST_ASSERT_TRUE_MESSAGE(rulesActionState(0), "one active member must assert");

    g_ruleContexts[rulesRuleSlot(1)].state = RULE_READY;   // both quiet now
    rulesActionsUpdate(false);
    TEST_ASSERT_FALSE(rulesActionState(0));
}

// The #343 safety call, through the real tables: a healthy member calling for the
// output beats a sibling whose sensor died, in BOTH failsafe polarities.
static void test_drive_live_member_beats_faulted_sibling(void) {
    addInput(10, NAN);                       // dead sensor
    addInput(11, 50.0f);                     // healthy, and demanding
    addRule(0, 10, false);
    addRule(1, 11, true);
    addAction(0, 23, ACT_FAILSAFE_OFF);      // the polarity that would cut it
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 1);

    rulesSyncActionPins();   // the actions hold their pins (§12.5)
    rulesActionsUpdate(false);
    TEST_ASSERT_TRUE_MESSAGE(rulesActionState(0),
                             "a dead sibling sensor must not cut a demanded output");
    TEST_ASSERT_TRUE(rulesActionSourceFaulted(0));   // ...but it is reported
}

// #348: the reported "a member rule is demanding this output" bit, through the
// real tables. It must track the DEMAND, not the pin -- under a held FORCE OFF
// the output is off while the rule still wants it on, and that disagreement is
// exactly what the client is being told about.
static void test_rule_active_tracks_demand_not_the_pin(void) {
    addInput(11, 50.0f);
    addRule(0, 11, /*active*/true);
    addAction(0, 23, ACT_FAILSAFE_OFF);
    rulesActionAddRule(0, 0);

    rulesSyncActionPins();   // the actions hold their pins (§12.5)
    rulesActionsUpdate(false);
    TEST_ASSERT_TRUE(rulesActionState(0));
    TEST_ASSERT_TRUE(rulesActionRuleActive(0));

    rulesActionSetOverride(actions[0].id, ACT_OVR_FORCE_OFF);
    rulesActionsUpdate(false);
    TEST_ASSERT_FALSE_MESSAGE(rulesActionState(0), "the force wins the pin");
    TEST_ASSERT_TRUE_MESSAGE(rulesActionRuleActive(0),
                             "...but the rule is still demanding it");

    g_ruleContexts[rulesRuleSlot(0)].state = RULE_READY;   // demand releases
    rulesActionsUpdate(false);
    TEST_ASSERT_FALSE(rulesActionRuleActive(0));
}

// The exclusions mirror rulesActionSourceFaulted(): a member that cannot assert
// must not be reported as demanding. A faulted (NaN) source is the sharp case --
// it drives the FAILSAFE, so the pin can be ON with nothing actually demanding it.
static void test_rule_active_false_for_members_that_cannot_assert(void) {
    addInput(10, NAN);                       // dead sensor -> faulted, not active
    addRule(0, 10, /*active*/true);          // ...even though the ctx says active
    addAction(0, 23, ACT_FAILSAFE_ON);
    rulesActionAddRule(0, 0);

    rulesSyncActionPins();   // the actions hold their pins (§12.5)
    rulesActionsUpdate(false);
    TEST_ASSERT_TRUE_MESSAGE(rulesActionState(0), "failsafe drives the pin on");
    TEST_ASSERT_TRUE(rulesActionSourceFaulted(0));
    TEST_ASSERT_FALSE_MESSAGE(rulesActionRuleActive(0),
                              "a faulted source never counts as a demand");
}

// The other two documented exclusions, which hold via the update loop rather than
// by an explicit check inside the resolver -- so they are worth pinning. A
// DISABLED member is forced to RULE_DISABLED, and an ORPHAN member (a rule whose
// source pin has no input, #345) is fed NaN; neither can assert, so neither may
// be reported as demanding the output.
static void test_rule_active_false_for_disabled_member(void) {
    addInput(11, 50.0f);
    addRule(0, 11, /*active*/true, /*enabled*/false);   // would be demanding, parked
    addAction(0, 23, ACT_FAILSAFE_OFF);
    rulesActionAddRule(0, 0);

    rulesActionsUpdate(false);
    TEST_ASSERT_FALSE(rulesActionState(0));
    TEST_ASSERT_FALSE(rulesActionRuleActive(0));
}

// An ORPHAN member (a rule whose source pin has no input, #345) is inert because
// the engine feeds it NaN and its context settles to RULE_READY -- NOT because
// the resolver special-cases it. So the invariant to pin is the one the design
// actually promises: the reported demand is whatever drove the pin, never a
// second opinion about it. Were a stale ACTIVE context to survive, the orphan
// would be driving the output too, and the frame must say so rather than lie.
static void test_rule_active_matches_the_drive_for_an_orphan_member(void) {
    addRule(0, 10, /*active*/false);         // pin 10: NO input configured
    addAction(0, 23, ACT_FAILSAFE_OFF);
    rulesActionAddRule(0, 0);

    rulesSyncActionPins();   // the actions hold their pins (§12.5)
    rulesActionsUpdate(false);
    TEST_ASSERT_FALSE(rulesActionState(0));
    TEST_ASSERT_FALSE(rulesActionRuleActive(0));

    g_ruleContexts[rulesRuleSlot(0)].state = RULE_ACTIVE;   // stale, pre-demotion
    rulesActionsUpdate(false);
    TEST_ASSERT_TRUE_MESSAGE(rulesActionState(0), "a stale ACTIVE ctx drives the pin");
    TEST_ASSERT_TRUE_MESSAGE(rulesActionRuleActive(0),
                             "...so the frame must report it, not disagree");
}

// A manual-only action has no membership, so it can never report a demand -- even
// while its latch is holding the pin on.
static void test_rule_active_false_for_manual_only_action(void) {
    addAction(0, 23);
    rulesSyncActionPins();   // claim first: a claim starts the slot de-asserted
    rulesActionSetManual(actions[0].id, true);
    rulesActionsUpdate(false);
    TEST_ASSERT_TRUE(rulesActionState(0));
    TEST_ASSERT_FALSE(rulesActionRuleActive(0));
}

static void test_drive_failsafe_applies_when_no_member_demands(void) {
    addInput(10, NAN);
    addInput(11, 50.0f);
    addRule(0, 10, false);
    addRule(1, 11, false);                   // healthy but not asserting
    addAction(0, 23, ACT_FAILSAFE_ON);
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 1);

    rulesSyncActionPins();   // the actions hold their pins (§12.5)
    rulesActionsUpdate(false);
    TEST_ASSERT_TRUE(rulesActionState(0));
}

// A DISABLED member can never assert, so it must not be able to force a failsafe
// either -- otherwise parking a rule (the natural way to shelve a member) would
// pin a fail-ON relay ON forever.
static void test_disabled_member_cannot_force_the_failsafe(void) {
    addInput(10, NAN);                       // disabled rule's source is dead
    addInput(11, 50.0f);
    addRule(0, 10, /*active*/false, /*enabled*/false);
    addRule(1, 11, /*active*/false);
    addAction(0, 23, ACT_FAILSAFE_ON);
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 1);

    rulesActionsUpdate(false);
    TEST_ASSERT_FALSE_MESSAGE(rulesActionState(0),
                              "a disabled member must not latch the failsafe on");
    TEST_ASSERT_FALSE(rulesActionSourceFaulted(0));
}

// A rule-driven action ignores the manual latch -- including when every member is
// dangling. This is the #294 route: memberCount counts dangling members precisely
// so the action can never fall back to the latch.
static void test_dangling_members_do_not_fall_back_to_the_latch(void) {
    addAction(0, 23);
    actions[0].ruleRef = 9;                  // rule 9 does not exist
    rulesActionSetManual(0, true);         // stale latch, armed
    rulesActionsUpdate(false);
    TEST_ASSERT_FALSE(rulesActionState(0));
}

// --- orphan rules: a source pin with no input (#345) ------------------------

// The #345 case: CLEAR <pin> used to leave the pin's rules behind. An orphan can
// never assert (the manager feeds it NaN), so it must not be able to force a
// failsafe either -- same argument as the disabled member above. Before the fix
// this single orphan pinned a fail-ON relay ON forever.
static void test_orphan_member_cannot_force_the_failsafe(void) {
    addInput(11, 50.0f);
    addRule(0, 10, /*active*/false);         // pin 10: NO input configured -> orphan
    addRule(1, 11, /*active*/false);
    addAction(0, 23, ACT_FAILSAFE_ON);
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 1);

    rulesActionsUpdate(false);
    TEST_ASSERT_FALSE_MESSAGE(rulesActionState(0),
                              "an orphan member must not latch the failsafe on");
    TEST_ASSERT_FALSE_MESSAGE(rulesActionSourceFaulted(0),
                              "a missing input is inert, not faulted");
}

// The distinction the fix rests on: a CONFIGURED input reading NaN is still the
// real sensor-loss case and MUST keep driving the failsafe.
static void test_configured_nan_source_still_faults(void) {
    addInput(10, NAN);
    addRule(0, 10, false);
    addAction(0, 23, ACT_FAILSAFE_ON);
    rulesActionAddRule(0, 0);

    rulesSyncActionPins();   // the actions hold their pins (§12.5)
    rulesActionsUpdate(false);
    TEST_ASSERT_TRUE(rulesActionState(0));
    TEST_ASSERT_TRUE(rulesActionSourceFaulted(0));
}

// CLEAR <pin> takes the pin's rules with it, through the RULE DEL path: every
// referring action loses that membership (and an action left with none reverts
// to manual-only), so no orphan is created in the first place.
static void test_remove_rules_for_input_drops_memberships(void) {
    addInput(10, 50.0f);
    addInput(11, 50.0f);
    addRule(0, 10, /*active*/true);           // two rules on the doomed pin
    addRule(1, 10, false);
    addRule(2, 11, false);                    // a bystander on another pin
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    rulesActionAddRule(0, 2);
    addAction(1, 24);
    rulesActionAddRule(1, 1);               // only member: reverts to manual-only

    uint8_t released = 0;
    TEST_ASSERT_EQUAL_UINT8(2, rulesRemoveRulesForInput(10, &released));
    TEST_ASSERT_EQUAL_UINT8(1, rulesRuleCount());          // only rule 2 survives
    TEST_ASSERT_TRUE(rulesRuleSlot(2) >= 0);
    TEST_ASSERT_EQUAL_UINT8(1, rulesActionRuleCount(0));   // rule 0 dropped
    TEST_ASSERT_TRUE(rulesActionHasRule(0, 2));
    TEST_ASSERT_EQUAL_UINT8(0, rulesActionRuleCount(1));   // manual-only now
    assertNoOrphanedLinks();

    rulesActionsUpdate(false);
    TEST_ASSERT_FALSE_MESSAGE(rulesActionState(0),
                              "the deleted rule must stop driving the action");
}

static void test_remove_rules_for_input_reports_released_drive(void) {
    addInput(10, 50.0f);
    addRule(0, 10, false);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    rulesSyncActionPins();                  // the action holds its pin (§12.5)
    TEST_ASSERT_TRUE(rulesActionCommand(0, true));   // operator override held

    uint8_t released = 0;
    TEST_ASSERT_EQUAL_UINT8(1, rulesRemoveRulesForInput(10, &released));
    TEST_ASSERT_EQUAL_UINT8(1, released);
    TEST_ASSERT_EQUAL_UINT8(0, rulesRemoveRulesForInput(10, nullptr));  // idempotent
}

// --- RULE <id> ENABLE|DISABLE (#346) ----------------------------------------

static void test_rule_disable_parks_it_and_enable_rearms(void) {
    addInput(10, 50.0f);
    addRule(0, 10, /*active*/true);

    TEST_ASSERT_TRUE(rulesRuleSetEnabled(0, false, 1000));
    TEST_ASSERT_EQUAL_UINT8(0, rules[0].flags & RULE_FLAG_ENABLED);
    TEST_ASSERT_EQUAL(RULE_DISABLED, g_ruleContexts[0].state);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1000, g_ruleContexts[0].stateEntryTime,
                                     "parking is a state entry: INFO <pin> ALARM "
                                     "prints time-in-state from this stamp");

    TEST_ASSERT_TRUE(rulesRuleSetEnabled(0, true, 2000));
    TEST_ASSERT_TRUE(rules[0].flags & RULE_FLAG_ENABLED);
    TEST_ASSERT_EQUAL(RULE_INIT, g_ruleContexts[0].state);   // re-armed, not ACTIVE
    TEST_ASSERT_EQUAL_UINT32(2000, g_ruleContexts[0].stateEntryTime);
}

// #254: enabling an already-armed rule must NOT re-init it, or a live ACTIVE
// alarm is silenced for warmupMs by a no-op command.
static void test_rule_enable_is_idempotent_and_never_reinits(void) {
    addInput(10, 50.0f);
    addRule(0, 10, /*active*/true);
    g_ruleContexts[0].stateEntryTime = 500;

    TEST_ASSERT_FALSE(rulesRuleSetEnabled(0, true, 9000));
    TEST_ASSERT_EQUAL(RULE_ACTIVE, g_ruleContexts[0].state);
    TEST_ASSERT_EQUAL_UINT32(500, g_ruleContexts[0].stateEntryTime);

    TEST_ASSERT_TRUE(rulesRuleSetEnabled(0, false, 9000));
    TEST_ASSERT_FALSE(rulesRuleSetEnabled(0, false, 9500));  // already parked
    TEST_ASSERT_FALSE(rulesRuleSetEnabled(7, true, 9500));   // unknown id
}

// A parked rule stops driving its action -- the CLI path #346 adds is what makes
// the #343 disabled-member behaviour reachable from hardware at all.
static void test_parked_rule_stops_driving_its_action(void) {
    addInput(10, 50.0f);
    addRule(0, 10, /*active*/true);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    rulesSyncActionPins();   // the actions hold their pins (§12.5)
    rulesActionsUpdate(false);
    TEST_ASSERT_TRUE(rulesActionState(0));

    rulesRuleSetEnabled(0, false, 1000);
    rulesActionsUpdate(false);
    TEST_ASSERT_FALSE(rulesActionState(0));
}


// --- action pin lifecycle (§12.5, #646, #648, #649) -------------------------

// Index of the first event for `pin` of `kind` at or after `from`, or -1.
static int pinEvent(uint8_t pin, char kind, uint8_t value, int from = 0) {
    for (int i = from; i < stubPinLogN; i++)
        if (stubPinLog[i].pin == pin && stubPinLog[i].kind == kind && stubPinLog[i].value == value) return i;
    return -1;
}

static uint8_t addInvertedAction(uint8_t id, uint8_t target) {
    addAction(id, target);
    actions[rulesActionSlot(id)].behavior |= ACT_BEHAVIOR_INVERT;
    return id;
}

static void test_init_touches_no_pin(void) {
    addAction(0, 23);
    rulesActionsInit();
    TEST_ASSERT_FALSE(stubPinTouched(23));
    TEST_ASSERT_FALSE(rulesActionDriven(0));
}

static void test_sync_claims_then_latches_off_before_output(void) {
    addInvertedAction(0, 23);                 // active-low: OFF is HIGH
    TEST_ASSERT_EQUAL_UINT8(0, rulesSyncActionPins());
    TEST_ASSERT_TRUE(rulesActionDriven(0));
    const int input  = pinEvent(23, 'M', INPUT);
    const int latch  = pinEvent(23, 'W', HIGH);
    const int output = pinEvent(23, 'M', OUTPUT);
    TEST_ASSERT_TRUE_MESSAGE(input >= 0 && latch > input, "INPUT first (STM32 port clock)");
    TEST_ASSERT_TRUE_MESSAGE(latch >= 0 && output > latch, "OFF must be latched before OUTPUT");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, pinEvent(23, 'W', LOW), "never the energized level");
    TEST_ASSERT_EQUAL_UINT8(OUTPUT, stubPinMode[23]);
    TEST_ASSERT_EQUAL_UINT8(HIGH, stubPinLatch[23]);
}

static void test_refused_action_never_touches_its_pin(void) {
    const uint8_t p = 23;
    TEST_ASSERT_TRUE(claimPins(&p, 1, PIN_OUTPUT, PIN_RANK_BUILTIN, TAG_SYSTEM, "TEST RGB"));
    addAction(0, 23);
    TEST_ASSERT_EQUAL_UINT8(1, rulesSyncActionPins());
    TEST_ASSERT_FALSE(rulesActionDriven(0));
    TEST_ASSERT_FALSE(rulesActionCommand(0, true));     // refused, nothing latched
    rulesActionsUpdate(false);
    rulesActionsQuiesce();
    TEST_ASSERT_FALSE_MESSAGE(stubPinTouched(23), "a refused action never configures, drives or releases its pin");
    TEST_ASSERT_EQUAL_STRING("TEST RGB", getPinDescription(23));
}

static void test_two_actions_on_one_pin_only_the_winner_drives(void) {
    addAction(0, 23);
    addAction(1, 23);
    TEST_ASSERT_EQUAL_UINT8(1, rulesSyncActionPins());
    TEST_ASSERT_TRUE(rulesActionDriven(0));
    TEST_ASSERT_FALSE(rulesActionDriven(1));
}

static void test_refused_action_claims_once_its_pin_frees(void) {
    const uint8_t p = 23;
    claimPins(&p, 1, PIN_OUTPUT, PIN_RANK_BUILTIN, TAG_SYSTEM, "TEST RGB");
    addAction(0, 23);
    rulesSyncActionPins();
    TEST_ASSERT_FALSE(rulesActionDriven(0));
    unregisterPin(23);                         // the holder lets go
    TEST_ASSERT_EQUAL_UINT8(0, rulesSyncActionPins());
    TEST_ASSERT_TRUE(rulesActionDriven(0));
    TEST_ASSERT_FALSE_MESSAGE(rulesActionState(0), "starts de-asserted");
}

static void test_resync_leaves_a_running_relay_alone(void) {
    addAction(0, 23);
    rulesSyncActionPins();
    TEST_ASSERT_TRUE(rulesActionCommand(0, true));    // manual ON
    rulesActionsUpdate(true);
    TEST_ASSERT_EQUAL_UINT8(HIGH, stubPinLatch[23]);
    stubPinLogN = 0;
    addAction(1, 24);                          // ACTION ADD of another
    rulesSyncActionPins();
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, pinEvent(23, 'W', LOW), "the running relay must not glitch");
    TEST_ASSERT_TRUE(rulesActionState(0));
    TEST_ASSERT_TRUE(rulesActionDriven(1));
}

static void test_quiesce_switches_off_then_releases(void) {
    addAction(0, 23);
    addInvertedAction(1, 24);
    rulesSyncActionPins();
    rulesActionCommand(0, true);
    rulesActionCommand(1, true);
    rulesActionsUpdate(true);                  // manual relays actuate in CONFIG
    TEST_ASSERT_EQUAL_UINT8(HIGH, stubPinLatch[23]);
    TEST_ASSERT_EQUAL_UINT8(LOW, stubPinLatch[24]);   // inverted ON
    rulesActionsQuiesce();
    TEST_ASSERT_EQUAL_UINT8(LOW, stubPinLatch[23]);
    TEST_ASSERT_EQUAL_UINT8(HIGH, stubPinLatch[24]);  // inverted OFF
    TEST_ASSERT_EQUAL_UINT8(INPUT, stubPinMode[23]);
    TEST_ASSERT_EQUAL_UINT8(INPUT, stubPinMode[24]);
    TEST_ASSERT_NULL(getPinDescription(23));
    TEST_ASSERT_NULL(getPinDescription(24));
    TEST_ASSERT_FALSE(rulesActionDriven(0));
}

static void test_reset_tables_releases_an_energized_relay(void) {
    addAction(0, 23);
    rulesSyncActionPins();
    rulesActionCommand(0, true);
    rulesActionsUpdate(true);
    rulesResetTables();                        // LOAD / SYSTEM RESET path
    TEST_ASSERT_EQUAL_UINT8(LOW, stubPinLatch[23]);
    TEST_ASSERT_EQUAL_UINT8(INPUT, stubPinMode[23]);
    TEST_ASSERT_NULL(getPinDescription(23));
}

static void test_reset_tables_with_nothing_held_touches_nothing(void) {
    // BSS-style rows: id 0 on pin 0, never claimed
    memset(actions, 0, sizeof(actions));
    rulesResetTables();
    TEST_ASSERT_FALSE(stubPinTouched(0));
}

static void test_action_delete_releases_its_pin(void) {
    addInvertedAction(0, 23);
    rulesSyncActionPins();
    bool held = false;
    TEST_ASSERT_TRUE(rulesActionDelete(0, &held));
    TEST_ASSERT_TRUE(held);
    TEST_ASSERT_EQUAL_UINT8(HIGH, stubPinLatch[23]);
    TEST_ASSERT_EQUAL_UINT8(INPUT, stubPinMode[23]);
    TEST_ASSERT_NULL(getPinDescription(23));
    TEST_ASSERT_TRUE(rulesActionSlot(0) < 0);
}

static void test_loop_never_drives_an_unclaimed_slot(void) {
    addAction(0, 23);                          // never synced
    rulesActionsUpdate(false);
    TEST_ASSERT_FALSE(stubPinTouched(23));
}

static void test_test_pulse_inverts_and_restores(void) {
    addInvertedAction(0, 23);
    rulesSyncActionPins();
    TEST_ASSERT_EQUAL(ACT_TEST_OK, rulesActionTestPulse(0, true, true));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(LOW, stubPinLatch[23], "logical ON energizes an active-low relay");
    TEST_ASSERT_EQUAL(ACT_TEST_OK, rulesActionTestPulse(0, false, true));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(HIGH, stubPinLatch[23], "and it ends OFF");
}

static void test_test_pulse_restores_rule_driven_relay_off_in_config(void) {
    addInput(10, 999.0f);
    addRule(0, 10, /*active*/true);
    addAction(0, 23);
    rulesActionAddRule(0, 0);
    rulesSyncActionPins();
    rulesActionsUpdate(false);                 // RUN: the rule drives it ON
    TEST_ASSERT_TRUE(rulesActionState(0));
    rulesActionTestPulse(0, true, true);
    rulesActionTestPulse(0, false, true);      // CONFIG holds rule-driven relays off
    TEST_ASSERT_EQUAL_UINT8(LOW, stubPinLatch[23]);
}

static void test_test_pulse_skips_forced_and_unheld(void) {
    addAction(0, 23);
    addAction(1, 24);
    const uint8_t p = 25;
    claimPins(&p, 1, PIN_OUTPUT, PIN_RANK_BUILTIN, TAG_SYSTEM, "TEST RGB");
    addAction(2, 25);
    rulesSyncActionPins();
    addRule(0, 10, false);
    rulesActionAddRule(0, 0);
    rulesActionCommand(0, false);              // FORCE_OFF on a rule-driven relay
    rulesActionCommand(1, true);               // manual ON
    stubPinLogN = 0;
    TEST_ASSERT_EQUAL(ACT_TEST_FORCED, rulesActionTestPulse(rulesActionSlot(0), true, true));
    TEST_ASSERT_EQUAL(ACT_TEST_ALREADY_ON, rulesActionTestPulse(rulesActionSlot(1), true, true));
    TEST_ASSERT_EQUAL(ACT_TEST_NOT_DRIVEN, rulesActionTestPulse(rulesActionSlot(2), true, true));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, stubPinLogN, "a skipped test touches no pin");
}


// #648 review B1: something else unregistered the relay's pin by number
// (a serial port's disable did this). The relay must still be switchable off.
static void test_entry_removed_by_another_still_switches_off(void) {
    addAction(0, 23);
    rulesSyncActionPins();
    rulesActionCommand(0, true);
    rulesActionsUpdate(true);
    TEST_ASSERT_EQUAL_UINT8(HIGH, stubPinLatch[23]);
    unregisterPin(23);                          // not the action's doing
    TEST_ASSERT_FALSE(rulesActionDriven(0));
    rulesActionsUpdate(true);
    TEST_ASSERT_FALSE_MESSAGE(rulesActionState(0), "a relay not driven is reported OFF");
    TEST_ASSERT_TRUE(rulesActionDelete(0));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(LOW, stubPinLatch[23], "nobody holds it: switched off anyway");
    TEST_ASSERT_EQUAL_UINT8(INPUT, stubPinMode[23]);
}

// ...but a pin another function took in the meantime is left alone
static void test_entry_taken_by_another_is_left_alone(void) {
    addAction(0, 23);
    rulesSyncActionPins();
    unregisterPin(23);
    const uint8_t p = 23;
    TEST_ASSERT_TRUE(claimPins(&p, 1, PIN_OUTPUT, PIN_RANK_BUILTIN, TAG_SYSTEM, "TEST RGB"));
    stubPinLogN = 0;
    rulesActionsQuiesce();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, stubPinLogN, "never touch a pin someone else holds");
    TEST_ASSERT_EQUAL_STRING("TEST RGB", getPinDescription(23));
}

// #648 review S3: after an outside unregister, a second action claims the
// same pin. One sync later exactly one slot drives it.
static void test_stale_hold_and_new_claim_settle_to_one_owner(void) {
    addAction(9, 24);                           // slot 0
    addAction(1, 23);                           // slot 1 holds 23
    rulesSyncActionPins();
    TEST_ASSERT_TRUE(rulesActionDriven(1));
    unregisterPin(23);                          // outside removal: slot 1 still records 23
    TEST_ASSERT_TRUE(rulesActionDelete(9));     // frees slot 0 properly
    addAction(2, 23);                           // lands in slot 0, BELOW the stale holder
    TEST_ASSERT_EQUAL_INT(0, rulesActionSlot(2));
    stubPinLogN = 0;
    rulesSyncActionPins();
    TEST_ASSERT_TRUE_MESSAGE(rulesActionDriven(0), "the new claimant drives");
    TEST_ASSERT_FALSE_MESSAGE(rulesActionDriven(1), "the stale holder doesn't");
    // slot 1's release must not have written 23 behind slot 0's back: every
    // write to 23 in this sync is slot 0's configure (INPUT, latch, OUTPUT, OFF)
    int writes = 0;
    for (int k = 0; k < stubPinLogN; k++) if (stubPinLog[k].pin == 23 && stubPinLog[k].kind == 'W') writes++;
    TEST_ASSERT_EQUAL_INT(2, writes);
    stubPinLogN = 0;
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, rulesSyncActionPins(), "slot 1 stays refused");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, stubPinLogN, "and is re-checked without touching the pin");
    int owners = 0;
    for (int k = 0; k < MAX_ACTIONS; k++) if (rulesActionDriven(k) && actions[k].target == 23) owners++;
    TEST_ASSERT_EQUAL_INT(1, owners);
}

static void test_sync_refuses_a_virtual_pin(void) {
    addAction(0, 0xF0);                         // I2C virtual pin from parsePin()
    TEST_ASSERT_EQUAL_UINT8(1, rulesSyncActionPins());
    TEST_ASSERT_FALSE(rulesActionDriven(0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_add_first_member_goes_inline);
    RUN_TEST(test_add_is_idempotent);
    RUN_TEST(test_members_are_enumerable_in_order);
    RUN_TEST(test_removing_inline_member_promotes_a_link);
    RUN_TEST(test_removing_last_member_reverts_to_manual);
    RUN_TEST(test_set_rule_replaces_whole_membership);
    RUN_TEST(test_clear_rules_frees_every_row);
    RUN_TEST(test_every_mutator_releases_override_and_latch);
    RUN_TEST(test_rejected_add_leaves_state_untouched);
    RUN_TEST(test_clearing_one_rule_leaves_other_members);
    RUN_TEST(test_clearing_the_inline_rule_promotes);
    RUN_TEST(test_prune_drops_dangling_duplicate_and_inline_dup_rows);
    RUN_TEST(test_prune_collapses_repeated_rows);
    RUN_TEST(test_sweep_promotes_when_inline_member_is_missing);
    RUN_TEST(test_sweep_reverts_to_manual_when_nothing_survives);
    RUN_TEST(test_new_action_does_not_inherit_a_dead_actions_links);
    RUN_TEST(test_drive_or_combines_live_members);
    RUN_TEST(test_drive_live_member_beats_faulted_sibling);
    RUN_TEST(test_rule_active_tracks_demand_not_the_pin);
    RUN_TEST(test_rule_active_false_for_members_that_cannot_assert);
    RUN_TEST(test_rule_active_false_for_disabled_member);
    RUN_TEST(test_rule_active_matches_the_drive_for_an_orphan_member);
    RUN_TEST(test_rule_active_false_for_manual_only_action);
    RUN_TEST(test_drive_failsafe_applies_when_no_member_demands);
    RUN_TEST(test_disabled_member_cannot_force_the_failsafe);
    RUN_TEST(test_dangling_members_do_not_fall_back_to_the_latch);
    RUN_TEST(test_orphan_member_cannot_force_the_failsafe);
    RUN_TEST(test_configured_nan_source_still_faults);
    RUN_TEST(test_remove_rules_for_input_drops_memberships);
    RUN_TEST(test_remove_rules_for_input_reports_released_drive);
    RUN_TEST(test_rule_disable_parks_it_and_enable_rearms);
    RUN_TEST(test_rule_enable_is_idempotent_and_never_reinits);
    RUN_TEST(test_parked_rule_stops_driving_its_action);
    RUN_TEST(test_init_touches_no_pin);
    RUN_TEST(test_sync_claims_then_latches_off_before_output);
    RUN_TEST(test_refused_action_never_touches_its_pin);
    RUN_TEST(test_two_actions_on_one_pin_only_the_winner_drives);
    RUN_TEST(test_refused_action_claims_once_its_pin_frees);
    RUN_TEST(test_resync_leaves_a_running_relay_alone);
    RUN_TEST(test_quiesce_switches_off_then_releases);
    RUN_TEST(test_reset_tables_releases_an_energized_relay);
    RUN_TEST(test_reset_tables_with_nothing_held_touches_nothing);
    RUN_TEST(test_action_delete_releases_its_pin);
    RUN_TEST(test_loop_never_drives_an_unclaimed_slot);
    RUN_TEST(test_test_pulse_inverts_and_restores);
    RUN_TEST(test_test_pulse_restores_rule_driven_relay_off_in_config);
    RUN_TEST(test_test_pulse_skips_forced_and_unheld);
    RUN_TEST(test_entry_removed_by_another_still_switches_off);
    RUN_TEST(test_entry_taken_by_another_is_left_alone);
    RUN_TEST(test_stale_hold_and_new_claim_settle_to_one_owner);
    RUN_TEST(test_sync_refuses_a_virtual_pin);
    return UNITY_END();
}
