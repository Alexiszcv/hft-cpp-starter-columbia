#pragma once
#include "fix_parser.hpp"
#include <cstddef>
#include <climits>

enum class FixBatchStatus { complete, incomplete, malformed, output_full };

struct FixBatchResult {
    FixBatchStatus status;
    std::size_t consumed; // bytes belonging to successfully emitted orders
    std::size_t count;
};

namespace fix_detail {
// Recognize an incomplete prefix without reading outside the supplied buffer.
inline FixBatchStatus prefix(const char* buf, std::size_t len,
                             const char* expected, std::size_t size) {
    const std::size_t available = len < size ? len : size;
    if (available != 0 && std::memcmp(buf, expected, available) != 0)
        return FixBatchStatus::malformed;
    return len < size ? FixBatchStatus::incomplete : FixBatchStatus::complete;
}

// Find one frame at buf[0]. BodyLength counts bytes AFTER tag 9's SOH and
// BEFORE tag 10. The seven-byte trailer is 10=ddd<SOH>. No checksum sum is
// computed: this helper validates framing and the checksum field's syntax.
inline FixBatchStatus frame_length(const char* buf, std::size_t len,
                                   std::size_t& frame_len) {
    if (buf == nullptr)
        return len == 0 ? FixBatchStatus::incomplete : FixBatchStatus::malformed;
    auto status = prefix(buf, len, "8=FIX", 5);
    if (status != FixBatchStatus::complete) return status;
    if (len == 5) return FixBatchStatus::incomplete;
    std::size_t pos;
    if (buf[5] == '.') {
        status = prefix(buf, len, "8=FIX.4.", 8);
        if (status != FixBatchStatus::complete) return status;
        if (len == 8) return FixBatchStatus::incomplete;
        if (buf[8] < '0' || buf[8] > '4') return FixBatchStatus::malformed;
        if (len == 9) return FixBatchStatus::incomplete;
        if (buf[9] != soh) return FixBatchStatus::malformed;
        pos = 10;
    } else {
        status = prefix(buf, len, "8=FIXT.1.1\x01", 11);
        if (status != FixBatchStatus::complete) return status;
        pos = 11;
    }
    status = prefix(buf + pos, len - pos, "9=", 2);
    if (status != FixBatchStatus::complete) return status;
    pos += 2;
    if (pos == len) return FixBatchStatus::incomplete;
    if (!digit(buf[pos])) return FixBatchStatus::malformed;
    std::size_t body_size = 0;
    while (pos < len && digit(buf[pos])) {
        const unsigned d = static_cast<unsigned>(buf[pos] - '0');
        if (body_size > (static_cast<std::size_t>(INT_MAX) - d) / 10)
            return FixBatchStatus::malformed;
        body_size = body_size * 10 + d;
        ++pos;
    }
    if (pos == len) return FixBatchStatus::incomplete;
    if (buf[pos] != soh || body_size < 5) return FixBatchStatus::malformed;
    ++pos;
    // Keep the resulting length representable by parse_new_order's int len.
    if (pos > static_cast<std::size_t>(INT_MAX) - 7 ||
        body_size > static_cast<std::size_t>(INT_MAX) - pos - 7)
        return FixBatchStatus::malformed;

    const std::size_t available = len - pos;
    status = prefix(buf + pos, available, "35=D\x01", 5);
    if (status != FixBatchStatus::complete) return status;
    if (body_size > available) return FixBatchStatus::incomplete;
    const std::size_t trailer = pos + body_size;
    if (buf[trailer - 1] != soh) return FixBatchStatus::malformed;
    status = prefix(buf + trailer, len - trailer, "10=", 3);
    if (status != FixBatchStatus::complete) return status;
    unsigned checksum = 0;
    for (std::size_t i = 3; i < 6; ++i) {
        if (len - trailer <= i) return FixBatchStatus::incomplete;
        if (!digit(buf[trailer + i])) return FixBatchStatus::malformed;
        checksum = checksum * 10 + static_cast<unsigned>(buf[trailer + i] - '0');
    }
    if (checksum > 255) return FixBatchStatus::malformed;
    if (len - trailer < 7) return FixBatchStatus::incomplete;
    if (buf[trailer + 6] != soh) return FixBatchStatus::malformed;
    frame_len = trailer + 7;
    return FixBatchStatus::complete;
}
} // namespace fix_detail

// Parse concatenated messages into storage owned by the caller. On incomplete,
// retain buf[consumed..len) for the next receive; on output_full, drain orders
// and resume at consumed. Previously emitted clordid views still borrow buf.
// No byte of an incomplete/malformed/unemitted frame is marked consumed.
inline FixBatchResult parse_new_order_batch(const char* buf, std::size_t len,
                                            NewOrder* orders, std::size_t capacity) {
    FixBatchResult result{FixBatchStatus::complete, 0, 0};
    if ((buf == nullptr && len != 0) || (orders == nullptr && capacity != 0)) {
        result.status = FixBatchStatus::malformed;
        return result;
    }
    while (result.consumed < len) {
        if (result.count == capacity) {
            result.status = FixBatchStatus::output_full;
            return result;
        }
        const char* message = buf + result.consumed;
        std::size_t length = 0;
        result.status = fix_detail::frame_length(message, len - result.consumed, length);
        if (result.status != FixBatchStatus::complete) return result;
        if (!parse_new_order(message, static_cast<int>(length), orders[result.count])) {
            result.status = FixBatchStatus::malformed;
            return result;
        }
        result.consumed += length;
        ++result.count;
    }
    return result;
}
