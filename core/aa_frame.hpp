// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Android Auto frames: the header both ways, and the reassembly of fragmented messages. No I/O, no logging.
#ifndef ENTRY_AAD_CORE_AA_FRAME_HPP
#define ENTRY_AAD_CORE_AA_FRAME_HPP

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <vector>

#include "aa_protocol.hpp"

namespace aa {

struct FrameHeader {
    uint8_t channel;
    uint8_t flags;
    uint16_t length;     // payload bytes on the wire (TLS records when encrypted)
    uint32_t totalSize;  // a FIRST fragment announces its message's size; 0 otherwise

    uint8_t position() const { return flags & frame::kPositionMask; }
    bool encrypted() const { return (flags & frame::kEncrypted) != 0; }
    // A FIRST fragment carries 4 more header bytes, the total size.
    bool announcesSize() const { return position() == frame::kFirst; }
};

// The 4 fixed header bytes; totalSize stays 0.
FrameHeader decodeFrameHeader(const uint8_t* bytes);
// The 4 bytes that follow the header of a FIRST fragment.
uint32_t decodeTotalSize(const uint8_t* bytes);
// A whole frame as the head unit sends it (never fragmented). False when the payload exceeds 64 KiB - 1.
bool encodeFrame(uint8_t channel, uint8_t flags, const uint8_t* payload, size_t length, std::vector<uint8_t>& out);

// Joins the fragments of each channel's messages. A whole frame passes straight through and leaves a message being
// assembled on its channel as it is.
class MessageAssembler {
public:
    enum Outcome {
        kPending,      // a FIRST or MIDDLE fragment, kept
        kComplete,     // `message` holds a whole message
        kOverflow,     // the channel's message would pass frame::kMaxMessage, or all channels' frame::kMaxPending:
                       // dropped, and the rest of its fragments with it (kSkipped)
        kNoFirst,      // a MIDDLE or LAST fragment without a FIRST on its channel: dropped
        kShortOfSize,  // a LAST fragment closed a message smaller than announced: dropped
        kSkipped,      // a MIDDLE or LAST fragment of a message dropped at kOverflow: dropped quietly
    };

    MessageAssembler() : received_(0), announced_(0) {}

    // `payload` is the frame's decrypted payload; `header.length` counts what came on the wire.
    Outcome add(const FrameHeader& header, const std::vector<uint8_t>& payload, std::vector<uint8_t>& message);
    // Forgets the channel's message in progress.
    void drop(uint8_t channel) { pending_.erase(channel); }
    // A FIRST or MIDDLE fragment that could not be decrypted: the channel's message is dropped, and the rest of its
    // fragments up to its LAST with it (kSkipped), as after kOverflow.
    void skip(uint8_t channel);
    // The channel's current message was dropped and its fragments are being skipped.
    bool skipping(uint8_t channel) const {
        const std::map<uint8_t, Pending>::const_iterator i = pending_.find(channel);
        return i != pending_.end() && i->second.skipping;
    }
    void clear() { pending_.clear(); }
    // After kShortOfSize: the wire bytes received and the size the phone announced.
    uint32_t receivedBytes() const { return received_; }
    uint32_t announcedBytes() const { return announced_; }

private:
    struct Pending {
        std::vector<uint8_t> data;
        bool sized;       // a FIRST fragment announced a size
        uint32_t size;    // that size
        uint32_t onWire;  // the length fields received so far
        bool skipping;    // the message overflowed: its fragments up to its LAST are dropped
        Pending() : sized(false), size(0), onWire(0), skipping(false) {}
    };
    size_t pendingBytes() const;  // held for all channels together
    std::map<uint8_t, Pending> pending_;
    uint32_t received_;
    uint32_t announced_;
};

}  // namespace aa

#endif  // ENTRY_AAD_CORE_AA_FRAME_HPP
