#include "hashtable.h"

#include <bit>
#include <cassert>
#include <cstdlib>      // std::calloc(), std::free()


// --- Table: одна физическая хеш-таблица ------------------------------------
// (шаблонные find/forEach определены в заголовке)

void HashTable::Table::init(size_t capacity) {
    assert(capacity > 0 && std::has_single_bit(capacity));   // степень двойки
    slots = static_cast<HashNode **>(std::calloc(capacity, sizeof(HashNode *)));
    mask = capacity - 1;
    count = 0;
}

void HashTable::Table::add(HashNode *node) {
    size_t idx = node->hash & mask;
    node->next = slots[idx];
    slots[idx] = node;
    count++;
}

HashNode *HashTable::Table::detach(Table *table, HashNode **link) {
    HashNode *node = *link;
    *link = node->next;
    table->count--;
    return node;
}

// --- HashTable: прогрессивный рехеш поверх двух таблиц Table ---------------

static constexpr size_t kMigrateBudget = 128;    // ключей переносится за операцию
static constexpr size_t kMaxLoadFactor = 8;      // длина цепочки, запускающая рост

void HashTable::migrateSome() {
    size_t moved = 0;
    while (moved < kMigrateBudget && shadow_.count > 0) {
        HashNode **slot = &shadow_.slots[migratePos_];
        if (!*slot) {
            migratePos_++;
            continue;               // в этом слоте переносить нечего
        }
        primary_.add(Table::detach(&shadow_, slot));
        moved++;
    }
    if (shadow_.count == 0 && shadow_.slots) {
        std::free(shadow_.slots);
        shadow_ = Table{};
    }
}

void HashTable::startResize() {
    assert(shadow_.slots == nullptr);
    shadow_ = primary_;                         // понижаем текущую таблицу
    primary_.init((primary_.mask + 1) * 2);     // новая таблица вдвое большего размера
    migratePos_ = 0;
}

void HashTable::add(HashNode *node) {
    if (!primary_.slots) {
        primary_.init(4);
    }
    primary_.add(node);

    if (!shadow_.slots) {                        // рехеш ещё не идёт?
        size_t limit = (primary_.mask + 1) * kMaxLoadFactor;
        if (primary_.count >= limit) {
            startResize();
        }
    }
    migrateSome();
}

void HashTable::clear() {
    std::free(primary_.slots);
    std::free(shadow_.slots);
    primary_ = Table{};
    shadow_ = Table{};
    migratePos_ = 0;
}

size_t HashTable::count() const {
    return primary_.count + shadow_.count;
}
