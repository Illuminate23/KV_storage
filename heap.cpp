#include "heap.h"


void TimerHeap::siftUp(size_t pos) {
    TimerSlot moving = slots_[pos];
    while (pos > 0 && slots_[parentOf(pos)].deadline > moving.deadline) {
        slots_[pos] = slots_[parentOf(pos)];    // меняемся местами с родителем
        *slots_[pos].backIndex = pos;
        pos = parentOf(pos);
    }
    slots_[pos] = moving;
    *slots_[pos].backIndex = pos;
}

void TimerHeap::siftDown(size_t pos) {
    size_t n = slots_.size();
    TimerSlot moving = slots_[pos];
    while (true) {
        // выбираем наименьший среди узла и его потомков
        size_t l = leftOf(pos);
        size_t r = rightOf(pos);
        size_t best = pos;
        uint64_t bestVal = moving.deadline;
        if (l < n && slots_[l].deadline < bestVal) {
            best = l;
            bestVal = slots_[l].deadline;
        }
        if (r < n && slots_[r].deadline < bestVal) {
            best = r;
        }
        if (best == pos) {
            break;
        }
        slots_[pos] = slots_[best];             // меняемся местами с потомком
        *slots_[pos].backIndex = pos;
        pos = best;
    }
    slots_[pos] = moving;
    *slots_[pos].backIndex = pos;
}

void TimerHeap::restore(size_t pos) {
    if (pos > 0 && slots_[parentOf(pos)].deadline > slots_[pos].deadline) {
        siftUp(pos);
    } else {
        siftDown(pos);
    }
}

void TimerHeap::put(size_t pos, TimerSlot slot) {
    if (pos < slots_.size()) {
        slots_[pos] = slot;             // обновляем существующий элемент
    } else {
        pos = slots_.size();
        slots_.push_back(slot);         // либо добавляем новый
    }
    restore(pos);
}

void TimerHeap::remove(size_t pos) {
    slots_[pos] = slots_.back();        // ставим на место удаляемого последний
    slots_.pop_back();
    if (pos < slots_.size()) {
        restore(pos);
    }
}
