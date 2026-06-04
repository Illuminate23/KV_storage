#include "protocol.h"

#include <cassert>
#include <cstring>


bool parseRequest(std::span<const uint8_t> frame, std::vector<std::string> &out) {
    size_t pos = 0;

    auto takeU32 = [&](uint32_t &dst) -> bool {
        if (pos + 4 > frame.size()) {
            return false;
        }
        std::memcpy(&dst, frame.data() + pos, 4);
        pos += 4;
        return true;
    };
    auto takeBytes = [&](size_t n, std::string &dst) -> bool {
        if (pos + n > frame.size()) {
            return false;
        }
        dst.assign(reinterpret_cast<const char *>(frame.data() + pos), n);
        pos += n;
        return true;
    };

    uint32_t tokens = 0;
    if (!takeU32(tokens)) {
        return false;
    }
    if (tokens > kMaxArgs) {
        return false;
    }
    while (out.size() < tokens) {
        uint32_t len = 0;
        if (!takeU32(len)) {
            return false;
        }
        if (!takeBytes(len, out.emplace_back())) {
            return false;
        }
    }
    return pos == frame.size();     // лишних байт в хвосте быть не должно
}

void Response::writeNil() {
    out_.appendU8(static_cast<uint8_t>(Tag::Nil));
}

void Response::writeString(std::string_view s) {
    out_.appendU8(static_cast<uint8_t>(Tag::Str));
    out_.appendU32(static_cast<uint32_t>(s.size()));
    out_.append({reinterpret_cast<const uint8_t *>(s.data()), s.size()});
}

void Response::writeInt(int64_t value) {
    out_.appendU8(static_cast<uint8_t>(Tag::Int));
    out_.appendI64(value);
}

void Response::writeDouble(double value) {
    out_.appendU8(static_cast<uint8_t>(Tag::Dbl));
    out_.appendF64(value);
}

void Response::writeError(ErrCode code, std::string_view text) {
    out_.appendU8(static_cast<uint8_t>(Tag::Err));
    out_.appendU32(static_cast<uint32_t>(code));
    out_.appendU32(static_cast<uint32_t>(text.size()));
    out_.append({reinterpret_cast<const uint8_t *>(text.data()), text.size()});
}

void Response::writeArray(uint32_t count) {
    out_.appendU8(static_cast<uint8_t>(Tag::Arr));
    out_.appendU32(count);
}

size_t Response::beginArray() {
    out_.appendU8(static_cast<uint8_t>(Tag::Arr));
    out_.appendU32(0);              // заглушка, перезаписывается в endArray()
    return out_.size() - 4;
}

void Response::endArray(size_t mark, uint32_t count) {
    assert(out_[mark - 1] == static_cast<uint8_t>(Tag::Arr));
    std::memcpy(out_.data() + mark, &count, 4);
}
