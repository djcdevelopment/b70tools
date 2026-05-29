#pragma once

#include <cstdint>
#include <string_view>

namespace b70::json_emit {

constexpr std::size_t kLineBufferBytes = 4096;

class LineBuilder {
public:
    LineBuilder();

    void begin_object();
    void end_object();
    void begin_array();
    void end_array();

    void key(std::string_view k);
    void value_string(std::string_view s);
    void value_u64(std::uint64_t v);
    void value_i64(std::int64_t v);
    void value_f64_or_missing(double v, bool& out_emitted_nan, bool& out_emitted_inf);
    void value_bool(bool v);
    void value_null();
    void value_raw(std::string_view raw);

    void newline();

    std::string_view view() const noexcept;
    void reset() noexcept;
    bool overflowed() const noexcept { return overflowed_; }

private:
    char buf_[kLineBufferBytes];
    std::size_t len_ = 0;
    bool need_comma_ = false;
    bool overflowed_ = false;

    void put(char c);
    void put(std::string_view s);
    void comma_if_needed();
    void write_escaped(std::string_view s);
};

}
