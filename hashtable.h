#pragma once

#include <cstddef>
#include <cstdint>
#include <concepts>
#include <functional>


// Узел индекса, встраиваемый внутрь объекта-нагрузки (intrusive-приём).
struct HashNode {
    HashNode *next = nullptr;
    uint64_t hash = 0;
};

// Предикат, отличающий настоящий искомый ключ от кандидата с тем же хеш-кодом.
template <class F>
concept NodePredicate = std::predicate<F, HashNode *>;

// Хеш-таблица с цепочками и прогрессивным рехешем: во время расширения
// сосуществуют две таблицы, и на каждой операции переносится небольшая
// ограниченная порция ключей. Поиск параметризован концептом-предикатом —
// благодаря этому в горячем пути нет накладных расходов std::function.
class HashTable {
public:
    HashTable() = default;
    ~HashTable() { clear(); }

    HashTable(const HashTable &) = delete;
    HashTable &operator=(const HashTable &) = delete;

    template <NodePredicate Eq>
    [[nodiscard]] HashNode *find(HashNode *key, Eq eq) {
        migrateSome();
        if (HashNode **link = primary_.find(key, eq)) return *link;
        if (HashNode **link = shadow_.find(key, eq)) return *link;
        return nullptr;
    }

    void add(HashNode *node);

    template <NodePredicate Eq>
    [[nodiscard]] HashNode *take(HashNode *key, Eq eq) {
        migrateSome();
        if (HashNode **link = primary_.find(key, eq)) return Table::detach(&primary_, link);
        if (HashNode **link = shadow_.find(key, eq)) return Table::detach(&shadow_, link);
        return nullptr;
    }

    void clear();
    [[nodiscard]] size_t count() const;

    // обойти все узлы, пока посетитель не вернёт false
    template <NodePredicate F>
    void forEach(F visit) const {
        primary_.forEach(visit) && shadow_.forEach(visit);
    }

private:
    // одна физическая таблица: массив односвязных цепочек-слотов
    struct Table {
        HashNode **slots = nullptr;
        size_t mask = 0;        // ёмкость - 1 (ёмкость — степень двойки)
        size_t count = 0;

        void init(size_t capacity);
        void add(HashNode *node);
        static HashNode *detach(Table *table, HashNode **link);

        template <NodePredicate Eq>
        HashNode **find(HashNode *key, Eq eq) {
            if (!slots) {
                return nullptr;
            }
            size_t idx = key->hash & mask;
            for (HashNode **link = &slots[idx]; *link; link = &(*link)->next) {
                if ((*link)->hash == key->hash && eq(*link)) {
                    return link;
                }
            }
            return nullptr;
        }

        template <NodePredicate F>
        bool forEach(F visit) const {
            for (size_t i = 0; mask != 0 && i <= mask; i++) {
                for (HashNode *node = slots[i]; node; node = node->next) {
                    if (!visit(node)) {
                        return false;
                    }
                }
            }
            return true;
        }
    };

    Table primary_;             // принимает все новые ключи
    Table shadow_;              // старая таблица, опустошаемая во время рехеша
    size_t migratePos_ = 0;

    void migrateSome();
    void startResize();
};
