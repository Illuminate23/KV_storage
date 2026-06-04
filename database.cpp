#include "database.h"

#include <cassert>
#include <string_view>

#include "common.h"
#include "thread_pool.h"


Record *Database::find(std::string_view key) {
    HashNode probe{.hash = hashKey(key)};   // designated initializer (C++20)
    HashNode *node = table_.find(&probe, [&](HashNode *candidate) {
        return std::string_view{ownerOf(candidate, Record, node)->key} == key;
    });
    return node ? ownerOf(node, Record, node) : nullptr;
}

void Database::insert(Record *record) {
    record->node.hash = hashKey(record->key);
    table_.add(&record->node);
}

Record *Database::detach(std::string_view key) {
    HashNode probe{.hash = hashKey(key)};
    HashNode *node = table_.take(&probe, [&](HashNode *candidate) {
        return std::string_view{ownerOf(candidate, Record, node)->key} == key;
    });
    return node ? ownerOf(node, Record, node) : nullptr;
}

void Database::setExpiry(Record *record, int64_t ttlMs) {
    if (ttlMs < 0 && record->timerIndex != kNoTimer) {
        timers_.remove(record->timerIndex);
        record->timerIndex = kNoTimer;
    } else if (ttlMs >= 0) {
        uint64_t deadline = nowMillis() + static_cast<uint64_t>(ttlMs);
        timers_.put(record->timerIndex,
                    TimerSlot{.deadline = deadline, .backIndex = &record->timerIndex});
    }
}

void Database::dispose(Record *record) {
    setExpiry(record, -1);          // отцепляем от кучи таймеров

    constexpr size_t kHeavyThreshold = 1000;
    size_t heavy = record->value ? record->value->heavyCount() : 0;
    if (heavy > kHeavyThreshold) {
        pool_.queue([record] { delete record; });   // освобождаем вне главного цикла
    } else {
        delete record;              // ~Record -> ~Value -> ~SortedSet
    }
}

void Database::forEach(const std::function<bool(Record *)> &visit) const {
    table_.forEach([&](HashNode *node) {
        return visit(ownerOf(node, Record, node));
    });
}

void Database::expireDue() {
    uint64_t now = nowMillis();
    constexpr size_t kBudget = 2000;
    size_t done = 0;

    while (!timers_.isEmpty() && timers_[0].deadline < now) {
        Record *record = ownerOf(timers_[0].backIndex, Record, timerIndex);
        HashNode *node = table_.take(&record->node, [&](HashNode *candidate) {
            return candidate == &record->node;
        });
        assert(node == &record->node);
        (void)node;
        dispose(record);
        if (done++ >= kBudget) {
            break;                  // не зависаем при массовом истечении
        }
    }
}
