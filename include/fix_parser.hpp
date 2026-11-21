#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
// HW11 — a high-performance FIX parser (SOH-delimited tag=value; tags 11/55/54/38/44).
struct NewOrder {
    const char* clordid; int clordid_len;   // tag 11
    char symbol[16]; char side;             // tags 55, 54
    uint32_t qty; double price;             // tags 38, 44
};
namespace fix_detail {
constexpr char soh = '\x01';

inline bool digit(char c) { return c >= '0' && c <= '9'; }

// Bounded decimal conversion: [+-]?digits[.digits]. No temporary string,
// locale dependency, exponent, or read past the end of the field.
inline bool decimal_price(const char* p, const char* end, double& price) {
    if (p == end) return false;
    bool negative = false;
    if (*p == '-' || *p == '+') {
        negative = *p == '-';
        ++p;
    }
    if (p == end || !digit(*p)) return false;
    double whole = 0.0;
    while (p < end && digit(*p)) {
        whole = whole * 10.0 + (*p - '0');
        ++p;
    }
    double fraction = 0.0;
    double place = 0.1;
    if (p < end && *p == '.') {
        ++p;
        if (p == end || !digit(*p)) return false;
        while (p < end && digit(*p)) {
            fraction += (*p - '0') * place;
            place *= 0.1;
            ++p;
        }
    }
    const double value = whole + fraction;
    if (p != end || !std::isfinite(value)) return false;
    price = negative ? -value : value;
    return true;
}

// The price reader is a template so the benchmark can change ONLY the decimal
// conversion (strtod -> arithmetic) while keeping the same scan and validation.
template <class PriceParser>
inline bool parse_order(const char* buf, int len, NewOrder& out,
                        PriceParser read_price) {
    if (buf == nullptr || len <= 0) return false;
    const char* p = buf;
    const char* end = buf + len;
    NewOrder order{};                  // publish only after a successful parse
    unsigned seen = 0;
    constexpr unsigned all_fields = 31;

    while (p < end) {
        if (!digit(*p) || *p == '0') return false;
        unsigned tag = 0;
        do {
            const unsigned d = static_cast<unsigned>(*p - '0');
            if (tag > (std::numeric_limits<unsigned>::max() - d) / 10)
                return false;
            tag = tag * 10 + d;
            ++p;
        } while (p < end && digit(*p));
        if (p == end || *p != '=') return false;
        const char* value = ++p;
        while (p < end && *p != soh) {
            if (*p == '\0') return false;
            ++p;
        }
        if (p == end || p == value) return false; // SOH and nonempty value required
        const char* value_end = p++;
        const int size = static_cast<int>(value_end - value);

        unsigned field = 0;
        switch (tag) {
            case 11:
                field = 1;
                order.clordid = value; // view into buf, no allocation or copy
                order.clordid_len = size;
                break;
            case 55:
                field = 2;
                if (size > 15) return false; // never silently truncate a symbol
                std::memcpy(order.symbol, value, static_cast<unsigned>(size));
                order.symbol[size] = '\0';
                break;
            case 54:
                field = 4;
                if (size != 1 || (*value != '1' && *value != '2')) return false;
                order.side = *value;
                break;
            case 38: {
                field = 8;
                std::uint32_t quantity = 0;
                for (const char* q = value; q < value_end; ++q) {
                    if (!digit(*q)) return false;
                    const unsigned d = static_cast<unsigned>(*q - '0');
                    if (quantity > (std::numeric_limits<std::uint32_t>::max() - d) / 10)
                        return false;
                    quantity = quantity * 10 + d;
                }
                order.qty = quantity;
                break;
            }
            case 44:
                field = 16;
                if (!read_price(value, value_end, order.price)) return false;
                break;
            case 35:
                if (size != 1 || *value != 'D') return false;
                break;
            default:
                break; // skip fields not needed by the assignment
        }
        if ((seen & field) != 0) return false; // duplicate required field
        seen |= field;
    }
    if (seen != all_fields) return false;
    out = order;
    return true;
}
} // namespace fix_detail

// Extract one message's fields in a forward scan, without heap allocation.
// This is a field parser, not a session validator: 8/9/10 are ignored here.
// On success, out.clordid borrows buf: keep that buffer alive and unchanged.
// On failure, out is unchanged. Use fix_batch.hpp for TCP/message framing.
inline bool parse_new_order(const char* buf, int len, NewOrder& out) {
    return fix_detail::parse_order(buf, len, out, fix_detail::decimal_price);
}
