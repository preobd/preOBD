/*
 * slotted_broadcaster.h - Per-slot transition-cache broadcast machinery.
 *
 * The control-engine broadcasters (Live Values, Alarm, Action) all follow the
 * same shape: walk a fixed table of slots, decide per slot whether anything
 * changed since the last frame we sent, and emit a stamped frame on change —
 * plus an optional periodic re-send (heartbeat) so a late subscriber catches
 * up, with backpressure (a failed send keeps the cache so the next tick
 * retries). This template single-sources that bookkeeping: the monotonic
 * sequence counter, the per-slot "last sent" cache, the heartbeat timer, the
 * resume cursor that lets a heartbeat span several ticks, AND the occupancy /
 * post-vacate grace window described below. Each subsystem supplies its own
 * change-key, equality, frame-send and occupancy predicate via callbacks.
 *
 * `Key` is the per-slot value compared to decide "changed since last sent" — the
 * full payload for Actions, or a compact (severity,kind) tuple for Alarms (so
 * alarm frames fire on transitions, not on every value wiggle). Keep it small;
 * it is stored N times.
 *
 * ---------------------------------------------------------------------------
 * OCCUPANCY AND THE POST-VACATE GRACE WINDOW
 * ---------------------------------------------------------------------------
 * A subsystem's table is sparse: a slot either holds something (an Input, an
 * Action) or it does not. `occupied(i)` is the ONLY thing a subsystem has to
 * supply for all of the following, which used to be copy-pasted per subsystem —
 * four bugs (#337, #349, #350, #352) landed in that duplicated shape and each
 * had to be fixed once per copy:
 *
 *   - Heartbeat membership. The periodic re-send covers the occupied slots (plus
 *     the grace window below), so a mostly-empty table stops paying for its
 *     empty slots every heartbeat while a slot going empty still pushes exactly
 *     one frame for the client to clear its tile on (#337, #365). Change
 *     detection is deliberately untouched: excluding a slot from the heartbeat
 *     must never make a transition invisible.
 *
 *   - The grace window. The clearing frame is edge-triggered and this transport
 *     has no end-to-end ACK: send() reports only LOCAL flow refusal, so a frame
 *     dropped further downstream (the nRF's per-destination stash, a notify a
 *     central does not accept) is invisible here. Before #337 the heartbeat
 *     re-sent the empty frame forever and a lost clear healed on its own — this
 *     is the self-healing-heartbeat tier, not the retry-until-accepted tier.
 *     Dropping a vacated slot from the heartbeat the instant it frees would have
 *     made that one frame the only chance, and a client that missed it would
 *     show a stale tile permanently, across reconnects. So a vacated slot keeps
 *     taking part in the heartbeat for a bounded window: that restores the
 *     healing without restoring the per-N sweep.
 *
 *   - Spend-on-accept. The window is spent per clearing frame the transport
 *     ACCEPTED, never per heartbeat edge — the template is the only thing that
 *     sees send()'s return value, and a subsystem inferring it got it wrong
 *     (#349). Counting edges burned the window on refused sends: on a link busy
 *     enough to starve the heartbeat the window expired having delivered
 *     nothing, which is exactly the self-healing it exists to provide.
 *     Bench-confirmed on teensy40: the vacated slot's clear went out once as a
 *     change push and the three repeats never arrived, so a client that missed
 *     it (or was disconnected across the CLEAR) kept a stale tile permanently.
 *
 *   - Seed-empty. reset()/prime() start from "nothing was occupied" rather than
 *     snapshotting the table, and the first tick() records the truth. This is
 *     correct by construction and it is what #352 was: the broadcasters are
 *     brought up next to the transports (main.cpp) BEFORE the config load
 *     (initInputManager()), so at init() the table is still BSS-zeroed and every
 *     slot reads id/pin == 0 — which an occupancy predicate cannot tell from a
 *     real record on id/pin 0. Snapshotting that made the first tick see N - n
 *     occupied->vacated edges and arm the grace window for every slot that had
 *     never held anything, putting them all back in the heartbeat; each then
 *     emitted the "tile is gone" frame, dripping out a few slots at a time as
 *     the sweep drained. Seeding empty is safe in both orders: the first tick
 *     records real occupancy and can only arm on a LATER vacate, and a slot that
 *     is occupied at init() has nothing to clear anyway — no client can hold a
 *     tile for it before the first loop() pass.
 *
 * A subsystem that passes no occupancy predicate (Alarms) is treated as "every
 * slot always occupied": membership is unconditional and the window never arms,
 * which is exactly the old behaviour. It inherits the machinery for free the day
 * it gains a heartbeat and an occupancy predicate.
 */

#ifndef SLOTTED_BROADCASTER_H
#define SLOTTED_BROADCASTER_H

#include <Arduino.h>

template <typename Key, int N>
class SlottedBroadcaster {
public:
    // How many heartbeats a slot keeps taking part in AFTER it is vacated, and
    // the total frames that budget covers: the edge-triggered change push costs
    // one, leaving kGraceHeartbeats heartbeat repeats.
    static constexpr uint8_t kGraceHeartbeats = 3;
    static constexpr uint8_t kGraceFrames     = kGraceHeartbeats + 1;

    // Cold start: nothing cached, so the first tick emits every slot. Occupancy
    // seeds EMPTY and the grace window closed — see the header comment.
    void reset() {
        for (int i = 0; i < N; ++i) state_[i] = 0;
        sequence_ = 0;
        lastSendMs_ = 0;
        endSweep();
    }

    // Seed every slot's cached key (have = true) so the first tick emits only on
    // a real transition away from `k` (e.g. Alarms prime to NORMAL/CLEAR and
    // stay quiet until something actually trips; Live Values and Actions prime
    // to their EMPTY frame, so the cold-start burst scales with the configured
    // record count instead of N). Occupancy seeds EMPTY here too.
    void prime(const Key& k) {
        for (int i = 0; i < N; ++i) { last_[i] = k; state_[i] = kHaveBit; }
        sequence_ = 0;
        lastSendMs_ = 0;
        endSweep();
    }

    // Force slot(s) to re-emit on the next tick (e.g. a manual command's
    // confirmation, or a fresh subscriber resync).
    void invalidate(int slot) { if (slot >= 0 && slot < N) state_[slot] &= ~kHaveBit; }
    void invalidateAll()      { for (int i = 0; i < N; ++i) state_[i] &= ~kHaveBit; }

    // Resync: force a re-emit of the slots a client actually has a tile for —
    // the occupied ones, plus any still inside its grace window so a pending
    // clear survives the reconnect. A slot with nothing in it has no tile to
    // rebuild (#337); it keeps its cached empty key, so a later
    // occupied->vacated transition still emits. `occupied` is evaluated live
    // rather than off the last tick's snapshot: a resync can arrive between a
    // config edit and the next tick.
    template <typename OccupiedFn>
    void invalidateActive(OccupiedFn occupied) {
        for (int i = 0; i < N; ++i) {
            if (occupied(i) || grace(i)) state_[i] &= ~kHaveBit;
        }
    }

    uint16_t sequence() const { return sequence_; }

    // True while a heartbeat sweep has been parked mid-table by backpressure and
    // still owes frames.
    bool sweepPending() const { return sweeping_; }

    // True while a parked sweep is actually DRAINING — it owes frames AND the
    // last pass moved the cursor forward. This, not sweepPending(), is what a
    // caller that rate-limits its own tick() (Live Values scans at 2 Hz) should
    // bypass its gate on: the sweep must advance faster than the caller's own
    // cadence or a full table takes longer to drain than the heartbeat interval
    // itself, but a sweep that is making NO progress must not buy a loop-rate
    // bypass forever. It stalls at zero progress in two real cases: framed mode
    // off (sendFrame() refuses unconditionally, and it is off by default) or a
    // peer holding the flow-control gate — where sweeping_ would otherwise latch
    // true for the rest of the boot and pin the caller at loop rate. It also
    // stalls when change traffic keeps eating the whole TX budget below the
    // cursor, which is the positive-feedback path that would otherwise let the
    // bypass amplify the very congestion starving the sweep.
    bool sweepDraining() const { return sweeping_ && advanced_; }

    // Whether the NEXT tick(now, heartbeatMs) will fire a periodic re-send.
    // tick() consumes the edge (it advances lastSendMs_), so a caller that needs
    // to count heartbeats rather than scans must read this first. Same predicate
    // tick() uses — deliberately not a second copy of the condition.
    bool heartbeatDue(uint32_t now, uint32_t heartbeatMs) const {
        return heartbeatMs && (now - lastSendMs_) >= heartbeatMs;
    }

    // Drive one broadcast pass.
    //   pack(i)        -> Key for slot i (the change-detection value)
    //   equal(a, b)    -> true when slot i has NOT changed (suppress send)
    //   send(i, seq)   -> build + emit slot i's frame stamped with `seq`;
    //                     return true on success, false on backpressure
    // heartbeatMs == 0 disables the periodic full re-send.
    //
    // This overload treats every slot as permanently occupied: the heartbeat
    // covers the whole table and the grace window never arms.
    template <typename PackFn, typename EqualFn, typename SendFn>
    void tick(uint32_t now, uint32_t heartbeatMs,
              PackFn pack, EqualFn equal, SendFn send) {
        tick(now, heartbeatMs, pack, equal, send, [](int) { return true; });
    }

    // As above, plus `occupied(i)` -> whether slot i currently holds a record.
    // The template derives heartbeat membership and the post-vacate grace window
    // from it; see the header comment for why all of that lives here rather than
    // in each subsystem.
    template <typename PackFn, typename EqualFn, typename SendFn, typename OccupiedFn>
    void tick(uint32_t now, uint32_t heartbeatMs,
              PackFn pack, EqualFn equal, SendFn send, OccupiedFn occupied) {
        // --- Phase 0: occupancy edges -------------------------------------
        // Arm the grace window on the occupied->vacated edge, BEFORE the pass
        // that emits the clearing frame, and snapshot occupancy for the rest of
        // this tick (heartbeat membership below, and the spend in markSent()).
        for (int i = 0; i < N; ++i) {
            const bool occ = occupied(i);
            if (!occ && (state_[i] & kOccBit)) setGrace(i, kGraceFrames);
            if (occ) state_[i] |=  kOccBit;
            else     state_[i] &= static_cast<uint8_t>(~kOccBit);
        }

        // A due heartbeat opens a SWEEP rather than a single burst. The old code
        // emitted the whole table inside one call, which the UART could not
        // absorb: a Teensy 4 Serial1 TX buffer is 64 B and a framed 16-byte
        // payload is 21 B on the wire, so exactly three frames fit and every
        // slot past the third was refused. An *unchanged* slot refused during a
        // heartbeat is not retried by the change check (it has not changed), and
        // the next heartbeat started over from slot 0 — so slots 3..N-1 were
        // starved permanently and late-subscriber catch-up, the only reason the
        // heartbeat exists, was broken past slot 2 (#350).
        const bool hbDue = heartbeatDue(now, heartbeatMs);
        if (hbDue) {
            // Restart from the top even if the previous sweep never finished:
            // the fresh pass owes the whole table anyway, and holding the old
            // cursor would let a chronically busy link pin the sweep in the
            // middle of the table forever.
            sweeping_ = true;
            cursor_   = 0;
        }

        // Phase B's range is fixed BEFORE the sweep runs: phase A covers
        // [sweepStart, N), so change traffic only needs [0, sweepStart).
        const int sweepStart = sweeping_ ? cursor_ : N;
        bool completed = !sweeping_;   // no sweep in flight => nothing owed
        bool blocked   = false;

        // --- Phase A: the sweep's remaining range, FIRST -------------------
        // The heartbeat gets first call on the TX budget while a sweep is in
        // flight. Serving the table strictly in index order instead lets change
        // traffic at low indices consume the whole budget every pass and starve
        // the sweep forever: three churning low slots against the three frames a
        // 64-byte buffer accepts is enough, and analog inputs churn constantly.
        // That is #350's starvation rebuilt out of change traffic, so a cursor
        // that merely refuses to rewind does not fix it — the sweep has to be
        // able to advance. The cost is bounded and small: only changed slots
        // BELOW the cursor wait, and only for the few passes a sweep lasts.
        if (sweeping_) {
            for (int i = sweepStart; i < N; ++i) {
                Key k = pack(i);
                const bool changed = !have(i) || !equal(k, last_[i]);
                if (!changed && !inHeartbeat(i)) continue;
                if (!send(i, sequence_)) {
                    // Park the sweep here; the next tick resumes at this slot
                    // rather than re-sending what it already served. The cursor
                    // only ever moves forward — phase A starts at it, so this
                    // assignment cannot rewind it.
                    cursor_ = i;
                    blocked = true;
                    break;
                }
                markSent(i, k);
            }
            if (!blocked) completed = true;
        }

        // --- Phase B: change-driven traffic ------------------------------
        // Slots below the sweep (or the whole table when no sweep is running).
        // Change detection is deliberately untouched by any of the sweep or
        // occupancy bookkeeping: a slot the heartbeat excludes, or one the sweep
        // has already passed, still emits the moment its key changes (#337).
        if (!blocked) {
            for (int i = 0; i < sweepStart && i < N; ++i) {
                Key k = pack(i);
                if (have(i) && equal(k, last_[i])) continue;
                if (!send(i, sequence_)) break;   // never touches the cursor
                markSent(i, k);
            }
        }

        // A sweep counts as draining only if it moved — see sweepDraining().
        advanced_ = completed || (cursor_ > sweepStart);
        if (completed) endSweep();

        // Advance the heartbeat timer whenever a heartbeat fired, regardless of
        // whether every send succeeded. Gating this on "all sends succeeded" was
        // a flood bug: one transient TX-buffer-full during the heartbeat burst
        // left lastSendMs_ frozen, so heartbeatDue stayed true forever and every
        // slot re-emitted on *every* tick (empty Action slots saturated the BLE
        // link and starved the text channel). Backpressured slots now recover by
        // resuming the sweep, and a changed slot still recovers via the change
        // check above.
        if (hbDue) lastSendMs_ = now;
    }

private:
    void endSweep() { sweeping_ = false; cursor_ = 0; advanced_ = false; }

    // One state byte per slot: the cache-valid bit, this tick's occupancy
    // snapshot, and the grace counter. Packed rather than kept as three arrays
    // so that folding the occupancy machinery in from the subsystems COSTS
    // nothing — Live Values and Actions each drop two N-byte arrays and gain
    // none, and a subsystem with no occupancy predicate (Alarms) pays exactly
    // what its old `have` array cost.
    static constexpr uint8_t kHaveBit   = 0x80;  // last_[i] is valid
    static constexpr uint8_t kOccBit    = 0x40;  // occupied at this tick's phase 0
    static constexpr uint8_t kGraceMask = 0x3F;  // frames of post-vacate grace left
    static_assert(kGraceFrames <= kGraceMask, "grace window does not fit its field");

    bool    have(int i)  const { return (state_[i] & kHaveBit) != 0; }
    uint8_t grace(int i) const { return static_cast<uint8_t>(state_[i] & kGraceMask); }
    void    setGrace(int i, uint8_t g) {
        state_[i] = static_cast<uint8_t>((state_[i] & ~kGraceMask) | (g & kGraceMask));
    }

    // Heartbeat membership: occupied, plus any slot still inside its post-vacate
    // grace window so the clearing frame gets more than one chance (#337).
    bool inHeartbeat(int i) const { return (state_[i] & kOccBit) || grace(i); }

    // An accepted frame: cache the key, stamp the sequence, and spend one frame
    // of a vacated slot's grace window (#349 — accepted frames only, never
    // heartbeat edges).
    void markSent(int i, const Key& k) {
        last_[i]   = k;
        state_[i] |= kHaveBit;
        if (!(state_[i] & kOccBit)) {
            const uint8_t g = grace(i);
            if (g) setGrace(i, static_cast<uint8_t>(g - 1));
        }
        ++sequence_;
    }

    Key      last_[N];
    uint8_t  state_[N];          // kHaveBit | kOccBit | grace counter
    uint16_t sequence_ = 0;
    uint32_t lastSendMs_ = 0;
    int      cursor_ = 0;        // next slot the parked heartbeat sweep owes
    bool     sweeping_ = false;  // a heartbeat sweep is in flight
    bool     advanced_ = false;  // last pass moved the cursor (sweep is draining)
};

#endif // SLOTTED_BROADCASTER_H
