#include <stdint.h>
#include <stdlib.h>

#include "hash.h"

#define HASH_SIZE 4096

struct _hash_entry_t {
    uint32_t key;
    void *value;
    hash_entry_t *prev, *next;
};

struct _hash_t {
    unsigned size;
    hash_entry_t entry[];
};

hash_t *hash_new(unsigned size) {
    hash_t *h;
    unsigned i;

    if (size == 0)
        size = HASH_SIZE;

    h = malloc(sizeof(*h) + sizeof(hash_entry_t) * size);
    h->size = size;
    for (i = 0; i < size; ++i)
        h->entry[i].next = h->entry[i].prev = &h->entry[i];
    return h;
}

void hash_destroy(hash_t *h) {
    // TODO
}

// FIXME handle double-insertion
void hash_insert(hash_t *h, uint32_t key, void *value) {
    unsigned b = key % h->size;
    hash_entry_t *e = malloc(sizeof(*e));
    e->key = key;
    e->value = value;
    e->prev = &h->entry[b];
    e->next = h->entry[b].next;
    h->entry[b].next->prev = e;
    h->entry[b].next = e;
}

void *hash_find(hash_t *h, uint32_t key) {
    hash_entry_t *e;
    unsigned b = key % h->size;
    for (e = h->entry[b].next; e != &h->entry[b]; e = e->next)
        if (e->key == key)
            return e->value;
    return NULL;
}

void hash_delete(hash_t *h, uint32_t key) {
    hash_entry_t *e;
    unsigned b = key % h->size;
    for (e = h->entry[b].next; e != &h->entry[b]; e = e->next) {
        if (e->key == key) {
            e->next->prev = e->prev;
            e->prev->next = e->next;
            free(e);
            return;
        }
    }
}

void hash_iterator_init(hash_iterator_t *i, hash_t *h) {
    i->h = h;
    i->b = 0;
    i->e = h->entry[0].next;
}

void *hash_iterator_next(hash_iterator_t *i, uint32_t *key_out) {
    void *ret;
    while (i->e == &i->h->entry[i->b]) {
        if (++i->b == i->h->size)
            return NULL;
        i->e = i->h->entry[i->b].next;
    }
    *key_out = i->e->key;
    ret = i->e->value;
    i->e = i->e->next;
    return ret;
}
