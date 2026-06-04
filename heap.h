#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>


// Один элемент-таймер. `backIndex` указывает на хранимую владельцем позицию,
// поэтому владельца всегда можно найти (и обновить) при перестройках кучи.
struct TimerSlot {
    uint64_t deadline = 0;
    size_t *backIndex = nullptr;
};

// Двоичная min-куча по времени срабатывания; обеспечивает истечение по TTL.
class TimerHeap {
public:
    bool   isEmpty() const { return slots_.empty(); }
    size_t size() const { return slots_.size(); }

    TimerSlot &operator[](size_t i) { return slots_[i]; }
    const TimerSlot &operator[](size_t i) const { return slots_[i]; }

    // вставить новый элемент либо перезаписать уже стоящий на позиции `pos`
    void put(size_t pos, TimerSlot slot);
    // удалить элемент на позиции `pos`
    void remove(size_t pos);

private:
    std::vector<TimerSlot> slots_;

    static size_t parentOf(size_t i) { return (i + 1) / 2 - 1; }
    static size_t leftOf(size_t i)   { return i * 2 + 1; }
    static size_t rightOf(size_t i)  { return i * 2 + 2; }

    void siftUp(size_t pos);
    void siftDown(size_t pos);
    void restore(size_t pos);
};
