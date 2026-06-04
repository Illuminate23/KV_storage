#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "buffer.h"


// ограничения протокола
constexpr size_t kMaxMessage = 32 << 20;    // больше, чем буфер сокета в ядре
constexpr size_t kMaxArgs    = 200 * 1000;

// Тег типа, предваряющий каждое сериализованное значение (один байт в потоке).
enum class Tag : uint8_t {
    Nil = 0,
    Err = 1,
    Str = 2,
    Int = 3,
    Dbl = 4,
    Arr = 5,
};

// Коды ошибок, передаваемые в ответе Tag::Err.
enum class ErrCode : uint32_t {
    Unknown = 1,    // неизвестная команда
    TooBig  = 2,    // ответ превышает ограничение по размеру
    BadType = 3,    // операция над значением неподходящего типа
    BadArg  = 4,    // некорректный аргумент
};

// Разобрать сырой кадр запроса на токены-аргументы.
// Формат кадра: количество, затем для каждого токена его длина и сами байты.
[[nodiscard]] bool parseRequest(std::span<const uint8_t> frame,
                                std::vector<std::string> &out);

// Записывает типизированные значения с тегами в выходной Buffer.
class Response {
public:
    explicit Response(Buffer &out) : out_(out) {}

    void writeNil();
    void writeString(std::string_view s);
    // удобная перегрузка для пар «указатель + длина» (в т.ч. бинарных имён)
    void writeString(const char *data, size_t size) {
        writeString(std::string_view{data, size});
    }
    void writeInt(int64_t value);
    void writeDouble(double value);
    void writeError(ErrCode code, std::string_view text);
    void writeArray(uint32_t count);

    // Для массивов неизвестной заранее длины: резервируем счётчик, заполняем
    // элементы, затем проставляем итог.
    size_t beginArray();
    void   endArray(size_t mark, uint32_t count);

private:
    Buffer &out_;
};
