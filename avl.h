#pragma once

#include <cstddef>
#include <cstdint>


// Узел встроенного AVL-дерева. Хранит высоту поддерева (для балансировки) и
// размер поддерева (для запросов по рангу/смещению). Это низкоуровневый
// примитив, поверх которого построен SortedSet.
struct AvlNode {
    AvlNode *parent = nullptr;
    AvlNode *left = nullptr;
    AvlNode *right = nullptr;
    uint32_t height = 0;
    uint32_t subtreeSize = 0;

    void reset() {
        left = right = parent = nullptr;
        height = 1;
        subtreeSize = 1;
    }
};

inline uint32_t avlHeight(AvlNode *node) { return node ? node->height : 0; }
inline uint32_t avlSize(AvlNode *node) { return node ? node->subtreeSize : 0; }

// Восстановить баланс вверх по дереву после изменения; возвращает новый корень поддерева.
AvlNode *avlRebalance(AvlNode *node);
// Удалить узел `node`; возвращает новый корень дерева.
AvlNode *avlErase(AvlNode *node);
// Сместиться на `delta` позиций в порядке сортировки; в худшем случае O(log N).
AvlNode *avlStep(AvlNode *node, int64_t delta);
