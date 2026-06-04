#include "common.h"

#include <time.h>    // POSIX clock_gettime()
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <charconv>
#include <cmath>
#include <format>
#include <string>


uint64_t nowMillis() {
    struct timespec ts = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000 + ts.tv_nsec / 1000 / 1000;
}

bool parseInt(std::string_view text, int64_t &out) {
    const char *begin = text.data();
    const char *end = begin + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

bool parseDouble(std::string_view text, double &out) {
    // std::from_chars для double поддерживается, но строку всё же передаём
    // через временный буфер, чтобы корректно работать со string_view.
    std::string buf(text);
    char *tail = nullptr;
    out = std::strtod(buf.c_str(), &tail);
    return tail == buf.c_str() + buf.size() && !std::isnan(out);
}

void logLine(std::string_view text) {
    std::fputs(std::format("{}\n", text).c_str(), stderr);
}

void logErrno(std::string_view text) {
    std::fputs(std::format("[errno:{}] {}\n", errno, text).c_str(), stderr);
}

void fatal(std::string_view text) {
    std::fputs(std::format("[{}] {}\n", errno, text).c_str(), stderr);
    std::abort();
}
