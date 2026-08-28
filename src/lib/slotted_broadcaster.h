/*
 * slotted_broadcaster.h - Per-slot transition-cache broadcast machinery.
 *
 * The control-engine broadcasters (Alarm, Action) all follow the same shape:
 * walk a fixed table of slots, decide per slot whether anything changed since
 * the last frame we sent, and emit a stamped frame on change — plus an optional
 * periodic re-send (heartbeat) so a late subscriber catches up, with
 * backpressure (a failed send keeps the cache so the next tick retries). This
 * template single-sources that bookkeeping: the monotonic sequence counter, the
 * per-slot "last sent" cache, and the heartbeat timer. Each subsystem supplies
 * its own change-key, equality, and frame-send via callbacks.
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
    }

    // Seed every slot's cached key (have = true) so the first tick emits only on
    // a real transition away from `k` (e.g. Alarms prime to NORMAL/CLEAR and
    // stay quiet until something actually trips).
    void prime(const Key& k) {
        for (int i = 0; i < N; ++i) { last_[i] = k; have_[i] = true; }
        sequence_ = 0;
        lastSendMs_ = 0;
    }

    // Force slot(s) to re-emit on the next tick (e.g. a manual command's
    // confirmation, or a fresh subscriber resync).
    void invalidate(int slot) { if (slot >= 0 && slot < N) have_[slot] = false; }
    void invalidateAll()      { for (int i = 0; i < N; ++i) have_[i] = false; }

    uint16_t sequence() const { return sequence_; }

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
        const bool hbDue = heartbeatDue(now, heartbeatMs);
        for (int i = 0; i < N; ++i) {
            Key k = pack(i);
            const bool changed = !have_[i] || !equal(k, last_[i]);
            if (!changed && !(hbDue && heartbeatFor(i))) continue;
            if (send(i, sequence_)) {
                last_[i] = k;
                have_[i] = true;
                ++sequence_;
            }
            // Backpressure (send == false): leave have_/last_ untouched so a
            // *changed* slot retries on the next tick via the change check.
        }
        // Advance the heartbeat timer whenever a heartbeat fired, regardless of
        // whether every send succeeded. Gating this on "all sends succeeded" was
        // a flood bug: one transient TX-buffer-full during the heartbeat burst
        // left lastSendMs_ frozen, so heartbeatDue stayed true forever and every
        // slot re-emitted on *every* tick (empty Action slots saturated the BLE
        // link and starved the text channel). Backpressured slots still recover
        // via the change check above (changed) or the next heartbeat.
        if (hbDue) lastSendMs_ = now;
    }

private:
    Key      last_[N];
    bool     have_[N];
    uint16_t sequence_ = 0;
    uint32_t lastSendMs_ = 0;
};

#endif // SLOTTED_BROADCASTER_H
