/*
 * slotted_broadcaster.h - Per-slot transition-cache broadcast machinery.
 *
 * The control-engine broadcasters (Alarm, Action) all follow the same shape:
 * walk a fixed table of slots, decide per slot whether anything changed since
 * the last frame we sent, and emit a stamped frame on change — plus an optional
 * periodic re-send (heartbeat) so a late subscriber catches up, with
 * backpressure (a failed send keeps the cache so the next tick retries). This
 * template single-sources that bookkeeping: the monotonic sequence counter, the
 * per-slot "last sent" cache, the heartbeat timer, and the resume cursor that
 * lets a heartbeat span several ticks. Each subsystem supplies its own
 * change-key, equality, and frame-send via callbacks.
 *
 * `Key` is the per-slot value compared to decide "changed since last sent" — the
 * full payload for Actions, or a compact (severity,kind) tuple for Alarms (so
 * alarm frames fire on transitions, not on every value wiggle). Keep it small;
 * it is stored N times.
 */

#ifndef SLOTTED_BROADCASTER_H
#define SLOTTED_BROADCASTER_H

#include <Arduino.h>

template <typename Key, int N>
class SlottedBroadcaster {
public:
    // Cold start: nothing cached, so the first tick emits every slot.
    void reset() {
        for (int i = 0; i < N; ++i) have_[i] = false;
        sequence_ = 0;
        lastSendMs_ = 0;
        endSweep();
    }

    // Seed every slot's cached key (have = true) so the first tick emits only on
    // a real transition away from `k` (e.g. Alarms prime to NORMAL/CLEAR and
    // stay quiet until something actually trips).
    void prime(const Key& k) {
        for (int i = 0; i < N; ++i) { last_[i] = k; have_[i] = true; }
        sequence_ = 0;
        lastSendMs_ = 0;
        endSweep();
    }

    // Force slot(s) to re-emit on the next tick (e.g. a manual command's
    // confirmation, or a fresh subscriber resync).
    void invalidate(int slot) { if (slot >= 0 && slot < N) have_[slot] = false; }
    void invalidateAll()      { for (int i = 0; i < N; ++i) have_[i] = false; }

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
    template <typename PackFn, typename EqualFn, typename SendFn>
    void tick(uint32_t now, uint32_t heartbeatMs,
              PackFn pack, EqualFn equal, SendFn send) {
        tick(now, heartbeatMs, pack, equal, send, [](int) { return true; });
    }

    // As above, plus `heartbeatFor(i)` -> whether slot i takes part in the
    // PERIODIC re-send. A slot it excludes still emits on a real change, so a
    // table that is mostly empty stops paying for its empty slots every
    // heartbeat while a slot going empty still pushes exactly one frame for the
    // client to clear its tile on (#337). Change detection is deliberately
    // untouched: excluding a slot from the heartbeat must never make a
    // transition invisible.
    template <typename PackFn, typename EqualFn, typename SendFn, typename HeartbeatFn>
    void tick(uint32_t now, uint32_t heartbeatMs,
              PackFn pack, EqualFn equal, SendFn send, HeartbeatFn heartbeatFor) {
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
                const bool changed = !have_[i] || !equal(k, last_[i]);
                if (!changed && !heartbeatFor(i)) continue;
                if (!send(i, sequence_)) {
                    // Park the sweep here; the next tick resumes at this slot
                    // rather than re-sending what it already served. The cursor
                    // only ever moves forward — phase A starts at it, so this
                    // assignment cannot rewind it.
                    cursor_ = i;
                    blocked = true;
                    break;
                }
                last_[i] = k;
                have_[i] = true;
                ++sequence_;
            }
            if (!blocked) completed = true;
        }

        // --- Phase B: change-driven traffic ------------------------------
        // Slots below the sweep (or the whole table when no sweep is running).
        // Change detection is deliberately untouched by any of the sweep
        // bookkeeping: a slot the heartbeat predicate excludes, or one the sweep
        // has already passed, still emits the moment its key changes (#337).
        if (!blocked) {
            for (int i = 0; i < sweepStart && i < N; ++i) {
                Key k = pack(i);
                if (have_[i] && equal(k, last_[i])) continue;
                if (!send(i, sequence_)) break;   // never touches the cursor
                last_[i] = k;
                have_[i] = true;
                ++sequence_;
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

    Key      last_[N];
    bool     have_[N];
    uint16_t sequence_ = 0;
    uint32_t lastSendMs_ = 0;
    int      cursor_ = 0;        // next slot the parked heartbeat sweep owes
    bool     sweeping_ = false;  // a heartbeat sweep is in flight
    bool     advanced_ = false;  // last pass moved the cursor (sweep is draining)
};

#endif // SLOTTED_BROADCASTER_H
