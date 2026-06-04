#pragma once

#include <string>

#include "zset.h"


enum class ValueKind {
    String,     // обычная байтовая строка
    SortedSet,  // упорядоченное множество пар (score, name)
};

// Базовый класс для всего, что хранится под ключом. Полиморфная иерархия
// (вместо размеченного объединения) делает каждый тип значения
// самодостаточным и позволяет освобождать память через RAII.
class Value {
public:
    virtual ~Value() = default;
    [[nodiscard]] virtual ValueKind kind() const = 0;

    // Число «тяжёлых» элементов; нужно, чтобы решить, отдавать ли освобождение
    // этого значения в пул потоков. Скаляры возвращают ноль.
    [[nodiscard]] virtual size_t heavyCount() const { return 0; }
};

class StringValue final : public Value {
public:
    std::string bytes;

    [[nodiscard]] ValueKind kind() const override { return ValueKind::String; }
};

class SortedSetValue final : public Value {
public:
    SortedSet set;

    [[nodiscard]] ValueKind kind() const override { return ValueKind::SortedSet; }
    [[nodiscard]] size_t heavyCount() const override { return set.size(); }
};
