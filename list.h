#pragma once

#include <cstddef>


// Узел встроенного (intrusive) кольцевого двусвязного списка. Свежий узел
// ссылается сам на себя — это одновременно и пустой список, и отцепленный элемент.
struct LinkedNode {
    LinkedNode *prev = this;
    LinkedNode *next = this;

    void reset() { prev = next = this; }

    bool isEmpty() const { return next == this; }

    // отцепить этот узел от списка, в котором он сейчас состоит
    void unlink() {
        prev->next = next;
        next->prev = prev;
    }

    // вставить узел `node` непосредственно перед текущим (т.е. в хвост,
    // когда `this` используется как голова-страж списка)
    void insertBefore(LinkedNode *node) {
        LinkedNode *tail = this->prev;
        tail->next = node;
        node->prev = tail;
        node->next = this;
        this->prev = node;
    }
};
