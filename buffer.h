#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>


// Растущий байтовый буфер для бинарного протокола.
//
// В отличие от наивного варианта, вычитывание из головы не сдвигает массив на
// каждой операции: хранится курсор чтения `head_`, а реальное освобождение
// (компактизация) выполняется лениво, когда накопится достаточно прочитанного.
// Это снимает квадратичные затраты при конвейерной обработке многих запросов.
class Buffer {
public:
    void append(const uint8_t *data, size_t length) {
        bytes_.insert(bytes_.end(), data, data + length);
    }
    void append(std::span<const uint8_t> data) {
        bytes_.insert(bytes_.end(), data.begin(), data.end());
    }
    void appendU8(uint8_t value)   { bytes_.push_back(value); }
    void appendU32(uint32_t value) { append({reinterpret_cast<const uint8_t *>(&value), 4}); }
    void appendI64(int64_t value)  { append({reinterpret_cast<const uint8_t *>(&value), 8}); }
    void appendF64(double value)   { append({reinterpret_cast<const uint8_t *>(&value), 8}); }

    // продвинуть курсор чтения на `count` байт
    void consume(size_t count) {
        head_ += count;
        if (head_ == bytes_.size()) {
            bytes_.clear();
            head_ = 0;
        } else if (head_ >= 4096 && head_ * 2 >= bytes_.size()) {
            bytes_.erase(bytes_.begin(), bytes_.begin() + head_);
            head_ = 0;
        }
    }

    void resize(size_t logicalSize) { bytes_.resize(head_ + logicalSize); }

    uint8_t       *data()       { return bytes_.data() + head_; }
    const uint8_t *data() const { return bytes_.data() + head_; }
    size_t size() const { return bytes_.size() - head_; }
    bool isEmpty() const { return size() == 0; }

    uint8_t &operator[](size_t i) { return bytes_[head_ + i]; }

    [[nodiscard]] std::span<const uint8_t> view() const { return {data(), size()}; }

private:
    std::vector<uint8_t> bytes_;
    size_t head_ = 0;       // количество уже вычитанных байт в голове
};
