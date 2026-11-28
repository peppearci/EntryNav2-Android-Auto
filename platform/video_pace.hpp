// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// What reached the decoder over a stretch of time, for the [Video] line logged every 10 s of a session.
//
// Frames and bytes of video, the longest gap between two frames, the longest hand-off of one frame to the decoder (the
// frame's ack waits for it, and the phone stops at its window of unacked frames; over Wi-Fi the wait of FrameGap is
// part of it), the frames lost, and how late they arrived: arrival against the phone's timestamp, over the smallest
// such offset seen in the session (so 0 is the session's quickest frame, not the true delay). Written by the session
// thread, taken by the main loop. tests/test_video_pace.cpp.
#ifndef ENTRY_AAD_PLATFORM_VIDEO_PACE_HPP
#define ENTRY_AAD_PLATFORM_VIDEO_PACE_HPP

#include <stdint.h>

#include <atomic>

namespace evo {

struct VideoPaceTotals {
    unsigned frames, kbytes, gapMaxMs, feedMaxMs, lost, lateMinMs, lateMaxMs;
};

class VideoPace {
public:
    VideoPace()
        : frames_(0),
          bytes_(0),
          gapMax_(0),
          feedMax_(0),
          lost_(0),
          lateMin_(kNone),
          lateMax_(0),
          lastMs_(0),
          base_(0),
          based_(false) {}

    // Session thread: one frame received, its hand-off to the decoder done at nowMs after feedMs.
    // ptsUs: the phone's timestamp of the frame, in microseconds of its own clock.
    void frame(uint32_t nowMs, unsigned bytes, unsigned feedMs, bool lost, uint64_t ptsUs) {
        // Arrival (before the hand-off) minus capture; 32 bits, wrapping like both clocks.
        const uint32_t offset = nowMs - feedMs - static_cast<uint32_t>(ptsUs / 1000);
        if (!based_.exchange(true) || static_cast<int32_t>(offset - base_.load()) < 0) {
            base_.store(offset);
        }
        const unsigned late = offset - base_.load();
        raise(lateMax_, late);
        if (late < lateMin_.load()) {
            lateMin_.store(late);
        }
        ++frames_;
        bytes_ += bytes;
        if (lost) {
            ++lost_;
        }
        const uint32_t last = lastMs_.exchange(nowMs);
        if (last) {
            raise(gapMax_, nowMs - last);
        }
        raise(feedMax_, feedMs);
    }

    // Main loop, when a session starts: nothing counted yet, and its first frame is not a gap.
    void restart() {
        take();
        lastMs_.store(0);
        based_.store(false);
    }

    // Main loop: the totals since the last take, reset.
    VideoPaceTotals take() {
        VideoPaceTotals t;
        t.frames = frames_.exchange(0);
        t.kbytes = static_cast<unsigned>((bytes_.exchange(0) + 512u) / 1024u);
        t.gapMaxMs = gapMax_.exchange(0);
        t.feedMaxMs = feedMax_.exchange(0);
        t.lost = lost_.exchange(0);
        const unsigned least = lateMin_.exchange(kNone);
        t.lateMinMs = least == kNone ? 0 : least;
        t.lateMaxMs = lateMax_.exchange(0);
        return t;
    }

private:
    static const unsigned kNone = 0xffffffffu;  // no frame since the last take
    // shortcut: a take() between the load and the store can keep the previous stretch's maximum, harmless for a log.
    static void raise(std::atomic<unsigned>& max, unsigned v) {
        if (v > max.load()) {
            max.store(v);
        }
    }
    std::atomic<unsigned> frames_;
    std::atomic<unsigned> bytes_;  // 32 bits: no libatomic on the car, and 10 s of video stay far below 4 GB
    std::atomic<unsigned> gapMax_, feedMax_, lost_, lateMin_, lateMax_;
    std::atomic<uint32_t> lastMs_, base_;
    std::atomic<bool> based_;
};

}  // namespace evo

#endif  // ENTRY_AAD_PLATFORM_VIDEO_PACE_HPP
