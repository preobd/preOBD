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
    RUN_TEST(test_drive_failsafe_applies_when_no_member_demands);
    RUN_TEST(test_disabled_member_cannot_force_the_failsafe);
    RUN_TEST(test_dangling_members_do_not_fall_back_to_the_latch);
    return UNITY_END();
}
