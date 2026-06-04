#include "avl.h"

#include <cassert>


static uint32_t maxU32(uint32_t a, uint32_t b) {
    return a < b ? b : a;
}

// пересчитать кэшированные высоту и размер поддерева узла
static void refresh(AvlNode *node) {
    node->height = 1 + maxU32(avlHeight(node->left), avlHeight(node->right));
    node->subtreeSize = 1 + avlSize(node->left) + avlSize(node->right);
}

static AvlNode *rotateLeft(AvlNode *node) {
    AvlNode *parent = node->parent;
    AvlNode *pivot = node->right;
    AvlNode *middle = pivot->left;
    node->right = middle;
    if (middle) {
        middle->parent = node;
    }
    pivot->parent = parent;
    pivot->left = node;
    node->parent = pivot;
    refresh(node);
    refresh(pivot);
    return pivot;
}

static AvlNode *rotateRight(AvlNode *node) {
    AvlNode *parent = node->parent;
    AvlNode *pivot = node->left;
    AvlNode *middle = pivot->right;
    node->left = middle;
    if (middle) {
        middle->parent = node;
    }
    pivot->parent = parent;
    pivot->right = node;
    node->parent = pivot;
    refresh(node);
    refresh(pivot);
    return pivot;
}

// левая сторона тяжелее на два
static AvlNode *fixLeftHeavy(AvlNode *node) {
    if (avlHeight(node->left->left) < avlHeight(node->left->right)) {
        node->left = rotateLeft(node->left);    // случай «лево-право»
    }
    return rotateRight(node);
}

// правая сторона тяжелее на два
static AvlNode *fixRightHeavy(AvlNode *node) {
    if (avlHeight(node->right->right) < avlHeight(node->right->left)) {
        node->right = rotateRight(node->right); // случай «право-лево»
    }
    return rotateLeft(node);
}

AvlNode *avlRebalance(AvlNode *node) {
    while (true) {
        AvlNode **anchor = &node;       // куда записать исправленное поддерево
        AvlNode *parent = node->parent;
        if (parent) {
            anchor = parent->left == node ? &parent->left : &parent->right;
        }
        refresh(node);
        uint32_t lh = avlHeight(node->left);
        uint32_t rh = avlHeight(node->right);
        if (lh == rh + 2) {
            *anchor = fixLeftHeavy(node);
        } else if (lh + 2 == rh) {
            *anchor = fixRightHeavy(node);
        }
        if (!parent) {
            return *anchor;             // дошли до корня
        }
        node = parent;                  // высота родителя могла измениться
    }
}

// удалить узел, у которого не более одного потомка
static AvlNode *eraseSimple(AvlNode *node) {
    assert(!node->left || !node->right);
    AvlNode *child = node->left ? node->left : node->right;  // возможно, null
    AvlNode *parent = node->parent;
    if (child) {
        child->parent = parent;
    }
    if (!parent) {
        return child;                   // удалён корень
    }
    AvlNode **anchor = parent->left == node ? &parent->left : &parent->right;
    *anchor = child;
    return avlRebalance(parent);
}

AvlNode *avlErase(AvlNode *node) {
    if (!node->left || !node->right) {
        return eraseSimple(node);
    }
    // два потомка: подставляем на место узла его «преемника» по порядку
    AvlNode *succ = node->right;
    while (succ->left) {
        succ = succ->left;
    }
    AvlNode *root = eraseSimple(succ);
    *succ = *node;                      // копируем ссылки из удаляемого узла
    if (succ->left) {
        succ->left->parent = succ;
    }
    if (succ->right) {
        succ->right->parent = succ;
    }
    AvlNode **anchor = &root;
    AvlNode *parent = node->parent;
    if (parent) {
        anchor = parent->left == node ? &parent->left : &parent->right;
    }
    *anchor = succ;
    return root;
}

AvlNode *avlStep(AvlNode *node, int64_t delta) {
    int64_t pos = 0;                    // смещение по рангу от стартового узла
    while (delta != pos) {
        if (pos < delta && pos + avlSize(node->right) >= delta) {
            node = node->right;
            pos += avlSize(node->left) + 1;
        } else if (pos > delta && pos - avlSize(node->left) <= delta) {
            node = node->left;
            pos -= avlSize(node->right) + 1;
        } else {
            AvlNode *parent = node->parent;
            if (!parent) {
                return nullptr;
            }
            if (parent->right == node) {
                pos -= avlSize(node->left) + 1;
            } else {
                pos += avlSize(node->right) + 1;
            }
            node = parent;
        }
    }
    return node;
}
