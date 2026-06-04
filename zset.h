#pragma once

#include "avl.h"
#include "hashtable.h"


// Элемент сортированного множества. Включён сразу в два индекса:
//   - AVL-дерево, упорядоченное по (score, name) — для запросов по диапазону/рангу,
//   - хеш-таблица по имени (name) — для проверки принадлежности за O(1).
// `name` — завершающий гибкий массив, поэтому узел выделяется через malloc().
struct SortedNode {
    AvlNode  tree;
    HashNode link;
    double   score = 0;
    size_t   nameLen = 0;
    char     name[0];
};

// Упорядоченная коллекция пар (score, name) с уникальными именами.
class SortedSet {
public:
    SortedSet() = default;
    ~SortedSet() { clear(); }

    SortedSet(const SortedSet &) = delete;
    SortedSet &operator=(const SortedSet &) = delete;

    // добавить новую пару либо изменить score существующей; true — если пара новая
    bool                     add(const char *name, size_t len, double score);
    [[nodiscard]] SortedNode *find(const char *name, size_t len);
    void                     erase(SortedNode *node);
    // наименьшая пара, не меньшая, чем (score, name)
    [[nodiscard]] SortedNode *lowerBound(double score, const char *name, size_t len);
    void                     clear();

    [[nodiscard]] size_t size() const { return index_.count(); }

    // сместиться на `delta` позиций по порядку сортировки от `node`
    [[nodiscard]] static SortedNode *step(SortedNode *node, int64_t delta);
    // общий, всегда пустой набор, возвращаемый для отсутствующих ключей
    static SortedSet *blank();

private:
    AvlNode  *root_ = nullptr;  // упорядочено по (score, name)
    HashTable index_;           // по имени (name)

    void attach(SortedNode *node);
    void rescore(SortedNode *node, double score);
};
