#pragma once

#include <functional>
#include <string_view>

#include "hashtable.h"
#include "heap.h"
#include "entry.h"

class ThreadPool;


// Хранилище «ключ/значение»: основная хеш-таблица, куча TTL-таймеров и
// жизненный цикл записей (создание, отцепление, удаление, истечение).
// Команды работают через этот движок.
class Database {
public:
    explicit Database(ThreadPool &pool) : pool_(pool) {}

    // доступ к хеш-таблице по строковому ключу (без копирования — string_view)
    [[nodiscard]] Record *find(std::string_view key);
    void                  insert(Record *record);          // вычисляет хеш и сохраняет
    [[nodiscard]] Record *detach(std::string_view key);    // удаляет и возвращает либо null

    // полностью уничтожить уже отцепленную запись (снимает её TTL; большие
    // контейнеры освобождаются в пуле потоков, чтобы не блокировать главный цикл)
    void dispose(Record *record);

    // управление TTL; отрицательное значение снимает уже заданный таймер
    void setExpiry(Record *record, int64_t ttlMs);
    const TimerHeap &timers() const { return timers_; }

    size_t size() const { return table_.count(); }
    void   forEach(const std::function<bool(Record *)> &visit) const;

    // удалить ключи, у которых истёк TTL (ограниченный объём работы за вызов)
    void expireDue();

private:
    HashTable table_;
    TimerHeap timers_;
    ThreadPool &pool_;
};
