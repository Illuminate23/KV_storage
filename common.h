#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>


// По указателю на встроенное поле восстанавливает указатель на объект-владелец.
// Это позволяет одному объекту одновременно лежать в нескольких индексах,
// храня узлы этих индексов прямо в себе.
#define ownerOf(memberPtr, OwnerType, field) \
    ((OwnerType *)( (char *)(memberPtr) - offsetof(OwnerType, field) ))

// 64-битный хеш FNV-1a по диапазону байт.
[[nodiscard]] constexpr uint64_t hashKey(const uint8_t *bytes, size_t length) {
    uint32_t acc = 0x811C9DC5;
    for (size_t i = 0; i < length; i++) {
        acc = (acc + bytes[i]) * 0x01000193;
    }
    return acc;
}

// Удобная перегрузка для строкового представления ключа.
[[nodiscard]] inline uint64_t hashKey(std::string_view text) {
    return hashKey(reinterpret_cast<const uint8_t *>(text.data()), text.size());
}

// --- общие вспомогательные функции ----------------------------------------

// текущее монотонное время в миллисекундах
[[nodiscard]] uint64_t nowMillis();

// строгий разбор числа: корректной должна быть вся строка целиком
[[nodiscard]] bool parseInt(std::string_view text, int64_t &out);
[[nodiscard]] bool parseDouble(std::string_view text, double &out);

// диагностика (форматирование через std::format)
void logLine(std::string_view text);
void logErrno(std::string_view text);
[[noreturn]] void fatal(std::string_view text);
