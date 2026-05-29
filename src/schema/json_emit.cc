#include "schema/json_emit.h"

#include <charconv>
#include <cmath>
#include <cstring>

namespace b70::json_emit {

LineBuilder::LineBuilder() {
    buf_[0] = '\0';
}

void LineBuilder::put(char c) {
    if (len_ + 1 >= sizeof(buf_)) { overflowed_ = true; return; }
    buf_[len_++] = c;
}

void LineBuilder::put(std::string_view s) {
    if (len_ + s.size() >= sizeof(buf_)) { overflowed_ = true; return; }
    std::memcpy(buf_ + len_, s.data(), s.size());
    len_ += s.size();
}

void LineBuilder::comma_if_needed() {
    if (need_comma_) { put(','); need_comma_ = false; }
}

void LineBuilder::write_escaped(std::string_view s) {
    put('"');
    for (char c : s) {
        switch (c) {
            case '"':  put("\\\""); break;
            case '\\': put("\\\\"); break;
            case '\n': put("\\n");  break;
            case '\r': put("\\r");  break;
            case '\t': put("\\t");  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char hex[7];
                    std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned>(c));
                    put(hex);
                } else {
                    put(c);
                }
        }
    }
    put('"');
}

void LineBuilder::begin_object() { comma_if_needed(); put('{'); need_comma_ = false; }
void LineBuilder::end_object()   { put('}'); need_comma_ = true;  }
void LineBuilder::begin_array()  { comma_if_needed(); put('['); need_comma_ = false; }
void LineBuilder::end_array()    { put(']'); need_comma_ = true;  }

void LineBuilder::key(std::string_view k) {
    comma_if_needed();
    write_escaped(k);
    put(':');
    need_comma_ = false;
}

void LineBuilder::value_string(std::string_view s) {
    comma_if_needed();
    write_escaped(s);
    need_comma_ = true;
}

void LineBuilder::value_u64(std::uint64_t v) {
    comma_if_needed();
    char tmp[32];
    auto r = std::to_chars(tmp, tmp + sizeof(tmp), v);
    put(std::string_view(tmp, r.ptr - tmp));
    need_comma_ = true;
}

void LineBuilder::value_i64(std::int64_t v) {
    comma_if_needed();
    char tmp[32];
    auto r = std::to_chars(tmp, tmp + sizeof(tmp), v);
    put(std::string_view(tmp, r.ptr - tmp));
    need_comma_ = true;
}

void LineBuilder::value_f64_or_missing(double v, bool& out_emitted_nan, bool& out_emitted_inf) {
    comma_if_needed();
    if (std::isnan(v)) {
        out_emitted_nan = true;
        put("null");
    } else if (std::isinf(v)) {
        out_emitted_inf = true;
        put("null");
    } else {
        char tmp[48];
        auto r = std::to_chars(tmp, tmp + sizeof(tmp), v);
        put(std::string_view(tmp, r.ptr - tmp));
    }
    need_comma_ = true;
}

void LineBuilder::value_bool(bool v) {
    comma_if_needed();
    put(v ? std::string_view("true") : std::string_view("false"));
    need_comma_ = true;
}

void LineBuilder::value_null() {
    comma_if_needed();
    put("null");
    need_comma_ = true;
}

void LineBuilder::value_raw(std::string_view raw) {
    comma_if_needed();
    put(raw);
    need_comma_ = true;
}

void LineBuilder::newline() { put('\n'); }

std::string_view LineBuilder::view() const noexcept {
    return std::string_view(buf_, len_);
}

void LineBuilder::reset() noexcept {
    len_ = 0;
    need_comma_ = false;
    overflowed_ = false;
    buf_[0] = '\0';
}

}
