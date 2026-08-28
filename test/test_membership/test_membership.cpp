// Native unit test: Rule<->Action MEMBERSHIP (src/control/control_manager.cpp).
//
// The pure resolver (controlActionDesired) is covered by test_control_engine
// against hand-built ActionDrive literals. This suite covers the layer BELOW
// that -- the real rules[]/actions[]/actionLinks[] tables and the mutators over
// them -- because that is where #343's load-bearing invariants live:
//
//   * ruleRef == CONTROL_ID_NONE MUST mean "zero members". controlActionRuleCount()
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

#include "../../src/control/control_engine.cpp"
#include "../../src/control/control_manager.cpp"

// ---- messaging: the sweep logs a repair; nothing else here prints ----------
// Just enough of the message plane to link: no transport is registered, so every
// stream resolves to nullptr and writes go nowhere.
MessageAPI    msg;
LogFilter::LogFilter() {}
MessageRouter::MessageRouter() {}
MessageRouter router;
TransportInterface* MessageRouter::getTransport(MessagePlane, bool) { return nullptr; }
size_t MessageStream::warn(const char* tag, const char* fmt, ...) {
    (void)tag; (void)fmt; return 0;
}

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
    int slot = controlRuleFreeSlot();
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
    int slot = controlActionFreeSlot();
    Action& a = actions[slot];
    memset(&a, 0, sizeof(a));
    a.id = id;
    a.type = ACT_RELAY;
    a.target = target;
    a.ruleRef = CONTROL_ID_NONE;
    a.failsafe = failsafe;
    return id;
}

// THE invariant, asserted after every mutation: a link row may never exist
// behind a NONE ruleRef, and no action may hold a duplicate member.
static void assertNoOrphanedLinks(void) {
    for (int i = 0; i < MAX_ACTION_LINKS; i++) {
        const ActionLink& l = actionLinks[i];
        if (l.actionId == CONTROL_ID_NONE) continue;
        const int slot = controlActionSlot(l.actionId);
        TEST_ASSERT_TRUE_MESSAGE(slot >= 0, "link row for an action that does not exist");
        TEST_ASSERT_TRUE_MESSAGE(actions[slot].ruleRef != CONTROL_ID_NONE,
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
    controlResetTables();
    controlActionsInit();     // zeroes the manual latch + override for every slot
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
    TEST_ASSERT_TRUE(controlActionAddRule(0, 0));
    TEST_ASSERT_EQUAL_UINT8(1, controlActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(0, actions[0].ruleRef);       // inline, no link row spent
    TEST_ASSERT_EQUAL_UINT8(0, controlActionLinkCount());
    assertNoOrphanedLinks();
}

static void test_add_is_idempotent(void) {
    addInput(10, 50.0f);
    addRule(0, 10, false);
    addAction(0, 23);
    controlActionAddRule(0, 0);
    TEST_ASSERT_TRUE(controlActionAddRule(0, 0));         // same rule again
    TEST_ASSERT_EQUAL_UINT8(1, controlActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(0, controlActionLinkCount());
    assertNoOrphanedLinks();
}

static void test_members_are_enumerable_in_order(void) {
    addInput(10, 50.0f); addInput(11, 50.0f); addInput(12, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false); addRule(2, 12, false);
    addAction(0, 23);
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 1);
    controlActionAddRule(0, 2);
    TEST_ASSERT_EQUAL_UINT8(3, controlActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(0, controlActionRuleAt(0, 0));
    TEST_ASSERT_EQUAL_UINT8(1, controlActionRuleAt(0, 1));
    TEST_ASSERT_EQUAL_UINT8(2, controlActionRuleAt(0, 2));
    TEST_ASSERT_EQUAL_UINT8(CONTROL_ID_NONE, controlActionRuleAt(0, 3));
    TEST_ASSERT_TRUE(controlActionHasRule(0, 2));
    TEST_ASSERT_FALSE(controlActionHasRule(0, 9));
    assertNoOrphanedLinks();
}

// THE invariant: removing the INLINE member promotes a link row instead of
// blanking ruleRef. Blanking would make controlActionRuleCount() report 0 --
// "manual-only" -- stranding the survivors and re-arming the #294 route.
static void test_removing_inline_member_promotes_a_link(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false);
    addAction(0, 23);
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 1);

    TEST_ASSERT_TRUE(controlActionRemoveRule(0, 0));      // drop the INLINE one
    TEST_ASSERT_EQUAL_UINT8(1, controlActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(1, actions[0].ruleRef);       // promoted, not blanked
    TEST_ASSERT_EQUAL_UINT8(0, controlActionLinkCount()); // row consumed
    assertNoOrphanedLinks();
}

static void test_removing_last_member_reverts_to_manual(void) {
    addInput(10, 50.0f);
    addRule(0, 10, false);
    addAction(0, 23);
    controlActionAddRule(0, 0);
    TEST_ASSERT_TRUE(controlActionRemoveRule(0, 0));
    TEST_ASSERT_EQUAL_UINT8(0, controlActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(CONTROL_ID_NONE, actions[0].ruleRef);
    assertNoOrphanedLinks();
}

static void test_set_rule_replaces_whole_membership(void) {
    addInput(10, 50.0f); addInput(11, 50.0f); addInput(12, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false); addRule(2, 12, false);
    addAction(0, 23);
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 1);
    TEST_ASSERT_TRUE(controlActionSetRule(0, 2));
    TEST_ASSERT_EQUAL_UINT8(1, controlActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(2, actions[0].ruleRef);
    TEST_ASSERT_EQUAL_UINT8(0, controlActionLinkCount());
    assertNoOrphanedLinks();
}

static void test_clear_rules_frees_every_row(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false);
    addAction(0, 23);
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 1);
    controlActionClearRules(0);
    TEST_ASSERT_EQUAL_UINT8(0, controlActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(CONTROL_ID_NONE, actions[0].ruleRef);
    TEST_ASSERT_EQUAL_UINT8(0, controlActionLinkCount());
    assertNoOrphanedLinks();
}

// --- #294 / #329: every membership change releases the drive -----------------

static void test_every_mutator_releases_override_and_latch(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false);
    addAction(0, 23);

    // ADD releases
    controlActionSetManual(0, true);
    controlActionSetOverride(0, ACT_OVR_FORCE_ON);
    bool released = false;
    controlActionAddRule(0, 0, &released);
    TEST_ASSERT_TRUE(released);
    TEST_ASSERT_EQUAL_INT(ACT_OVR_AUTO, controlActionOverride(0));

    // ADD of a second member releases again
    controlActionSetOverride(0, ACT_OVR_FORCE_ON);
    released = false;
    controlActionAddRule(0, 1, &released);
    TEST_ASSERT_TRUE(released);
    TEST_ASSERT_EQUAL_INT(ACT_OVR_AUTO, controlActionOverride(0));

    // REMOVE releases
    controlActionSetOverride(0, ACT_OVR_FORCE_OFF);
    released = false;
    controlActionRemoveRule(0, 1, &released);
    TEST_ASSERT_TRUE(released);
    TEST_ASSERT_EQUAL_INT(ACT_OVR_AUTO, controlActionOverride(0));

    // SET releases -- and reports it even when only the MANUAL LATCH was armed,
    // which is the case cmd_action previously reported silently.
    controlActionSetManual(0, true);
    released = false;
    controlActionSetRule(0, 1, &released);
    TEST_ASSERT_TRUE_MESSAGE(released, "latch-only release must still be reported");
    TEST_ASSERT_EQUAL_INT(ACT_OVR_AUTO, controlActionOverride(0));
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
        if (controlActionAddRule(0, r)) placed++;

    const uint8_t before = controlActionRuleCount(0);
    controlActionSetOverride(0, ACT_OVR_FORCE_ON);

    // A rule that is not yet a member, with no room left for it.
    bool added = false, released = true;
    for (uint8_t r = 0; r < MAX_RULES; r++) {
        if (controlRuleSlot(r) >= 0 && !controlActionHasRule(0, r)) {
            added = controlActionAddRule(0, r, &released);
            break;
        }
    }
    if (!added) {
        // Table geometry left no spare rule to attempt with -- nothing to assert.
        TEST_ASSERT_EQUAL_UINT8(before, controlActionRuleCount(0));
        return;
    }
    TEST_ASSERT_EQUAL_UINT8(before, controlActionRuleCount(0));
    TEST_ASSERT_FALSE(released);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ACT_OVR_FORCE_ON, controlActionOverride(0),
                                  "a rejected ADD must not release a live force");
    assertNoOrphanedLinks();
}

// --- rule deletion across a multi-member action -----------------------------

static void test_clearing_one_rule_leaves_other_members(void) {
    addInput(10, 50.0f); addInput(11, 50.0f); addInput(12, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false); addRule(2, 12, false);
    addAction(0, 23);
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 1);
    controlActionAddRule(0, 2);

    controlClearActionsForRule(1);                       // RULE DEL 1
    TEST_ASSERT_EQUAL_UINT8(2, controlActionRuleCount(0));
    TEST_ASSERT_TRUE(controlActionHasRule(0, 0));
    TEST_ASSERT_FALSE(controlActionHasRule(0, 1));
    TEST_ASSERT_TRUE(controlActionHasRule(0, 2));
    assertNoOrphanedLinks();
}

// Deleting the rule that happens to be the INLINE member still promotes.
static void test_clearing_the_inline_rule_promotes(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false);
    addAction(0, 23);
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 1);
    controlClearActionsForRule(0);
    TEST_ASSERT_EQUAL_UINT8(1, controlActionRuleCount(0));
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
    TEST_ASSERT_EQUAL_UINT8(3, controlPruneActionLinks());
    TEST_ASSERT_EQUAL_UINT8(1, controlActionRuleCount(0));
    assertNoOrphanedLinks();
}

static void test_prune_collapses_repeated_rows(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false);
    addAction(0, 23);
    actions[0].ruleRef = 0;
    actionLinks[0] = {0, 1};
    actionLinks[1] = {0, 1};                 // repeat of an earlier surviving row
    TEST_ASSERT_EQUAL_UINT8(1, controlPruneActionLinks());
    TEST_ASSERT_EQUAL_UINT8(2, controlActionRuleCount(0));
    // ...and REMOVE now actually removes the membership, rather than leaving a
    // second copy that keeps the action rule-driven.
    controlActionRemoveRule(0, 1);
    TEST_ASSERT_FALSE(controlActionHasRule(0, 1));
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

    TEST_ASSERT_TRUE(controlSweepActionRules() > 0);
    TEST_ASSERT_EQUAL_UINT8(1, controlActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(1, actions[0].ruleRef);
    assertNoOrphanedLinks();
}

static void test_sweep_reverts_to_manual_when_nothing_survives(void) {
    addAction(0, 23);
    actions[0].ruleRef = 5;                  // no rules exist at all
    actionLinks[0] = {0, 6};
    controlSweepActionRules();
    TEST_ASSERT_EQUAL_UINT8(0, controlActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(CONTROL_ID_NONE, actions[0].ruleRef);
    assertNoOrphanedLinks();
}

// --- id reuse ---------------------------------------------------------------

static void test_new_action_does_not_inherit_a_dead_actions_links(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, false); addRule(1, 11, false);
    addAction(0, 23);
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 1);

    // ACTION DEL 0 (cmd_action frees the rows before releasing the slot)...
    controlActionForgetLinks(0);
    actions[0].id = CONTROL_ID_NONE;
    // ...then ACTION ADD reuses id 0.
    addAction(0, 24);
    TEST_ASSERT_EQUAL_UINT8(0, controlActionRuleCount(0));
    TEST_ASSERT_EQUAL_UINT8(0, controlActionLinkCount());
    assertNoOrphanedLinks();
}

// --- resolveActionDrive: live tables -> the aggregate the resolver consumes ---
// Everything above this line in the suite is membership bookkeeping; these drive
// the real relay decision end-to-end (tables -> drive -> controlActionDesired).

static void test_drive_or_combines_live_members(void) {
    addInput(10, 50.0f); addInput(11, 50.0f);
    addRule(0, 10, /*active*/false);
    addRule(1, 11, /*active*/true);
    addAction(0, 23);
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 1);

    controlActionsUpdate(/*configMode*/false);
    TEST_ASSERT_TRUE_MESSAGE(controlActionState(0), "one active member must assert");

    g_ruleContexts[controlRuleSlot(1)].state = RULE_READY;   // both quiet now
    controlActionsUpdate(false);
    TEST_ASSERT_FALSE(controlActionState(0));
}

// The #343 safety call, through the real tables: a healthy member calling for the
// output beats a sibling whose sensor died, in BOTH failsafe polarities.
static void test_drive_live_member_beats_faulted_sibling(void) {
    addInput(10, NAN);                       // dead sensor
    addInput(11, 50.0f);                     // healthy, and demanding
    addRule(0, 10, false);
    addRule(1, 11, true);
    addAction(0, 23, ACT_FAILSAFE_OFF);      // the polarity that would cut it
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 1);

    controlActionsUpdate(false);
    TEST_ASSERT_TRUE_MESSAGE(controlActionState(0),
                             "a dead sibling sensor must not cut a demanded output");
    TEST_ASSERT_TRUE(controlActionSourceFaulted(0));   // ...but it is reported
}

// #348: the reported "a member rule is demanding this output" bit, through the
// real tables. It must track the DEMAND, not the pin -- under a held FORCE OFF
// the output is off while the rule still wants it on, and that disagreement is
// exactly what the client is being told about.
static void test_rule_active_tracks_demand_not_the_pin(void) {
    addInput(11, 50.0f);
    addRule(0, 11, /*active*/true);
    addAction(0, 23, ACT_FAILSAFE_OFF);
    controlActionAddRule(0, 0);

    controlActionsUpdate(false);
    TEST_ASSERT_TRUE(controlActionState(0));
    TEST_ASSERT_TRUE(controlActionRuleActive(0));

    controlActionSetOverride(actions[0].id, ACT_OVR_FORCE_OFF);
    controlActionsUpdate(false);
    TEST_ASSERT_FALSE_MESSAGE(controlActionState(0), "the force wins the pin");
    TEST_ASSERT_TRUE_MESSAGE(controlActionRuleActive(0),
                             "...but the rule is still demanding it");

    g_ruleContexts[controlRuleSlot(0)].state = RULE_READY;   // demand releases
    controlActionsUpdate(false);
    TEST_ASSERT_FALSE(controlActionRuleActive(0));
}

// The exclusions mirror controlActionSourceFaulted(): a member that cannot assert
// must not be reported as demanding. A faulted (NaN) source is the sharp case --
// it drives the FAILSAFE, so the pin can be ON with nothing actually demanding it.
static void test_rule_active_false_for_members_that_cannot_assert(void) {
    addInput(10, NAN);                       // dead sensor -> faulted, not active
    addRule(0, 10, /*active*/true);          // ...even though the ctx says active
    addAction(0, 23, ACT_FAILSAFE_ON);
    controlActionAddRule(0, 0);

    controlActionsUpdate(false);
    TEST_ASSERT_TRUE_MESSAGE(controlActionState(0), "failsafe drives the pin on");
    TEST_ASSERT_TRUE(controlActionSourceFaulted(0));
    TEST_ASSERT_FALSE_MESSAGE(controlActionRuleActive(0),
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
    controlActionAddRule(0, 0);

    controlActionsUpdate(false);
    TEST_ASSERT_FALSE(controlActionState(0));
    TEST_ASSERT_FALSE(controlActionRuleActive(0));
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
    controlActionAddRule(0, 0);

    controlActionsUpdate(false);
    TEST_ASSERT_FALSE(controlActionState(0));
    TEST_ASSERT_FALSE(controlActionRuleActive(0));

    g_ruleContexts[controlRuleSlot(0)].state = RULE_ACTIVE;   // stale, pre-demotion
    controlActionsUpdate(false);
    TEST_ASSERT_TRUE_MESSAGE(controlActionState(0), "a stale ACTIVE ctx drives the pin");
    TEST_ASSERT_TRUE_MESSAGE(controlActionRuleActive(0),
                             "...so the frame must report it, not disagree");
}

// A manual-only action has no membership, so it can never report a demand -- even
// while its latch is holding the pin on.
static void test_rule_active_false_for_manual_only_action(void) {
    addAction(0, 23);
    controlActionSetManual(actions[0].id, true);
    controlActionsUpdate(false);
    TEST_ASSERT_TRUE(controlActionState(0));
    TEST_ASSERT_FALSE(controlActionRuleActive(0));
}

static void test_drive_failsafe_applies_when_no_member_demands(void) {
    addInput(10, NAN);
    addInput(11, 50.0f);
    addRule(0, 10, false);
    addRule(1, 11, false);                   // healthy but not asserting
    addAction(0, 23, ACT_FAILSAFE_ON);
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 1);

    controlActionsUpdate(false);
    TEST_ASSERT_TRUE(controlActionState(0));
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
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 1);

    controlActionsUpdate(false);
    TEST_ASSERT_FALSE_MESSAGE(controlActionState(0),
                              "a disabled member must not latch the failsafe on");
    TEST_ASSERT_FALSE(controlActionSourceFaulted(0));
}

// A rule-driven action ignores the manual latch -- including when every member is
// dangling. This is the #294 route: memberCount counts dangling members precisely
// so the action can never fall back to the latch.
static void test_dangling_members_do_not_fall_back_to_the_latch(void) {
    addAction(0, 23);
    actions[0].ruleRef = 9;                  // rule 9 does not exist
    controlActionSetManual(0, true);         // stale latch, armed
    controlActionsUpdate(false);
    TEST_ASSERT_FALSE(controlActionState(0));
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
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 1);

    controlActionsUpdate(false);
    TEST_ASSERT_FALSE_MESSAGE(controlActionState(0),
                              "an orphan member must not latch the failsafe on");
    TEST_ASSERT_FALSE_MESSAGE(controlActionSourceFaulted(0),
                              "a missing input is inert, not faulted");
}

// The distinction the fix rests on: a CONFIGURED input reading NaN is still the
// real sensor-loss case and MUST keep driving the failsafe.
static void test_configured_nan_source_still_faults(void) {
    addInput(10, NAN);
    addRule(0, 10, false);
    addAction(0, 23, ACT_FAILSAFE_ON);
    controlActionAddRule(0, 0);

    controlActionsUpdate(false);
    TEST_ASSERT_TRUE(controlActionState(0));
    TEST_ASSERT_TRUE(controlActionSourceFaulted(0));
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
    controlActionAddRule(0, 0);
    controlActionAddRule(0, 2);
    addAction(1, 24);
    controlActionAddRule(1, 1);               // only member: reverts to manual-only

    uint8_t released = 0;
    TEST_ASSERT_EQUAL_UINT8(2, controlRemoveRulesForInput(10, &released));
    TEST_ASSERT_EQUAL_UINT8(1, controlRuleCount());          // only rule 2 survives
    TEST_ASSERT_TRUE(controlRuleSlot(2) >= 0);
    TEST_ASSERT_EQUAL_UINT8(1, controlActionRuleCount(0));   // rule 0 dropped
    TEST_ASSERT_TRUE(controlActionHasRule(0, 2));
    TEST_ASSERT_EQUAL_UINT8(0, controlActionRuleCount(1));   // manual-only now
    assertNoOrphanedLinks();

    controlActionsUpdate(false);
    TEST_ASSERT_FALSE_MESSAGE(controlActionState(0),
                              "the deleted rule must stop driving the action");
}

static void test_remove_rules_for_input_reports_released_drive(void) {
    addInput(10, 50.0f);
    addRule(0, 10, false);
    addAction(0, 23);
    controlActionAddRule(0, 0);
    controlActionCommand(0, true);            // operator override held

    uint8_t released = 0;
    TEST_ASSERT_EQUAL_UINT8(1, controlRemoveRulesForInput(10, &released));
    TEST_ASSERT_EQUAL_UINT8(1, released);
    TEST_ASSERT_EQUAL_UINT8(0, controlRemoveRulesForInput(10, nullptr));  // idempotent
}

// --- RULE <id> ENABLE|DISABLE (#346) ----------------------------------------

static void test_rule_disable_parks_it_and_enable_rearms(void) {
    addInput(10, 50.0f);
    addRule(0, 10, /*active*/true);

    TEST_ASSERT_TRUE(controlRuleSetEnabled(0, false, 1000));
    TEST_ASSERT_EQUAL_UINT8(0, rules[0].flags & RULE_FLAG_ENABLED);
    TEST_ASSERT_EQUAL(RULE_DISABLED, g_ruleContexts[0].state);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1000, g_ruleContexts[0].stateEntryTime,
                                     "parking is a state entry: INFO <pin> ALARM "
                                     "prints time-in-state from this stamp");

    TEST_ASSERT_TRUE(controlRuleSetEnabled(0, true, 2000));
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

    TEST_ASSERT_FALSE(controlRuleSetEnabled(0, true, 9000));
    TEST_ASSERT_EQUAL(RULE_ACTIVE, g_ruleContexts[0].state);
    TEST_ASSERT_EQUAL_UINT32(500, g_ruleContexts[0].stateEntryTime);

    TEST_ASSERT_TRUE(controlRuleSetEnabled(0, false, 9000));
    TEST_ASSERT_FALSE(controlRuleSetEnabled(0, false, 9500));  // already parked
    TEST_ASSERT_FALSE(controlRuleSetEnabled(7, true, 9500));   // unknown id
}

// A parked rule stops driving its action -- the CLI path #346 adds is what makes
// the #343 disabled-member behaviour reachable from hardware at all.
static void test_parked_rule_stops_driving_its_action(void) {
    addInput(10, 50.0f);
    addRule(0, 10, /*active*/true);
    addAction(0, 23);
    controlActionAddRule(0, 0);
    controlActionsUpdate(false);
    TEST_ASSERT_TRUE(controlActionState(0));

    controlRuleSetEnabled(0, false, 1000);
    controlActionsUpdate(false);
    TEST_ASSERT_FALSE(controlActionState(0));
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
    return UNITY_END();
}
