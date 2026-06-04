#include "zset.h"
#include "common.h"

#include <cassert>
#include <cstring>
#include <cstdlib>


static SortedNode *createNode(const char *name, size_t len, double score) {
    SortedNode *node = (SortedNode *)std::malloc(sizeof(SortedNode) + len);
    assert(node);
    node->tree.reset();
    node->link.next = nullptr;
    node->link.hash = hashKey((uint8_t *)name, len);
    node->score = score;
    node->nameLen = len;
    std::memcpy(&node->name[0], name, len);
    return node;
}

static void freeNode(SortedNode *node) {
    std::free(node);
}

static size_t minSize(size_t a, size_t b) {
    return a < b ? a : b;
}

// упорядочить узел дерева относительно явного ключа (score, name)
static bool precedes(AvlNode *lhs, double score, const char *name, size_t len) {
    SortedNode *a = ownerOf(lhs, SortedNode, tree);
    if (a->score != score) {
        return a->score < score;
    }
    int cmp = std::memcmp(a->name, name, minSize(a->nameLen, len));
    if (cmp != 0) {
        return cmp < 0;
    }
    return a->nameLen < len;
}

static bool precedes(AvlNode *lhs, AvlNode *rhs) {
    SortedNode *b = ownerOf(rhs, SortedNode, tree);
    return precedes(lhs, b->score, b->name, b->nameLen);
}

void SortedSet::attach(SortedNode *node) {
    AvlNode *parent = nullptr;
    AvlNode **anchor = &root_;
    while (*anchor) {
        parent = *anchor;
        anchor = precedes(&node->tree, parent) ? &parent->left : &parent->right;
    }
    *anchor = &node->tree;
    node->tree.parent = parent;
    root_ = avlRebalance(&node->tree);
}

void SortedSet::rescore(SortedNode *node, double score) {
    if (node->score == score) {
        return;
    }
    root_ = avlErase(&node->tree);  // вынимаем, меняем ключ, вставляем обратно
    node->tree.reset();
    node->score = score;
    attach(node);
}

bool SortedSet::add(const char *name, size_t len, double score) {
    SortedNode *node = find(name, len);
    if (node) {
        rescore(node, score);
        return false;
    }
    node = createNode(name, len, score);
    index_.add(&node->link);
    attach(node);
    return true;
}

SortedNode *SortedSet::find(const char *name, size_t len) {
    if (!root_) {
        return nullptr;
    }
    HashNode probe;
    probe.hash = hashKey((uint8_t *)name, len);
    HashNode *hit = index_.find(&probe, [&](HashNode *candidate) {
        SortedNode *n = ownerOf(candidate, SortedNode, link);
        return n->nameLen == len && 0 == std::memcmp(n->name, name, len);
    });
    return hit ? ownerOf(hit, SortedNode, link) : nullptr;
}

void SortedSet::erase(SortedNode *node) {
    HashNode probe;
    probe.hash = node->link.hash;
    HashNode *gone = index_.take(&probe, [&](HashNode *candidate) {
        SortedNode *n = ownerOf(candidate, SortedNode, link);
        return n->nameLen == node->nameLen &&
               0 == std::memcmp(n->name, node->name, node->nameLen);
    });
    assert(gone);
    (void)gone;
    root_ = avlErase(&node->tree);
    freeNode(node);
}

SortedNode *SortedSet::lowerBound(double score, const char *name, size_t len) {
    AvlNode *best = nullptr;
    for (AvlNode *node = root_; node; ) {
        if (precedes(node, score, name, len)) {
            node = node->right;     // ещё ниже ключа
        } else {
            best = node;            // кандидат; ищем меньший
            node = node->left;
        }
    }
    return best ? ownerOf(best, SortedNode, tree) : nullptr;
}

SortedNode *SortedSet::step(SortedNode *node, int64_t delta) {
    AvlNode *t = node ? avlStep(&node->tree, delta) : nullptr;
    return t ? ownerOf(t, SortedNode, tree) : nullptr;
}

static void disposeTree(AvlNode *node) {
    if (!node) {
        return;
    }
    disposeTree(node->left);
    disposeTree(node->right);
    freeNode(ownerOf(node, SortedNode, tree));
}

void SortedSet::clear() {
    index_.clear();
    disposeTree(root_);
    root_ = nullptr;
}

SortedSet *SortedSet::blank() {
    static SortedSet empty;
    return &empty;
}
