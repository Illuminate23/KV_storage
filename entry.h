#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "hashtable.h"
#include "value.h"


// Значение-маркер для Record::timerIndex, когда у ключа нет TTL-таймера.
inline constexpr size_t kNoTimer = static_cast<size_t>(-1);

// Запись «ключ/значение», хранимая в хеш-таблице базы. HashNode встроен,
// поэтому запись лежит прямо внутри таблицы; значение хранится полиморфно и
// принадлежит записи через unique_ptr.
struct Record {
    HashNode node;                  // встроенная ссылка хеш-таблицы
    std::string key;
    size_t timerIndex = kNoTimer;   // позиция в TTL-куче либо kNoTimer
    std::unique_ptr<Value> value;   // StringValue или SortedSetValue

    [[nodiscard]] static Record *makeString() {
        Record *r = new Record();
        r->value = std::make_unique<StringValue>();
        return r;
    }
    [[nodiscard]] static Record *makeSortedSet() {
        Record *r = new Record();
        r->value = std::make_unique<SortedSetValue>();
        return r;
    }

    [[nodiscard]] StringValue    *asString()    { return static_cast<StringValue *>(value.get()); }
    [[nodiscard]] SortedSetValue *asSortedSet() { return static_cast<SortedSetValue *>(value.get()); }
};
