/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#ifndef __HASH_H__
#define __HASH_H__

typedef struct _hash_t hash_t;
typedef struct _hash_entry_t hash_entry_t;

typedef struct _hash_iterator_t {
    hash_t *h;
    unsigned b;
    hash_entry_t *e;
} hash_iterator_t;

hash_t *hash_new(unsigned size);
void hash_destroy(hash_t *h);
void hash_insert(hash_t *h, uint32_t key, void *value);
void *hash_find(hash_t *h, uint32_t key);
void hash_delete(hash_t *h, uint32_t key);

void hash_iterator_init(hash_iterator_t *i, hash_t *h);
void *hash_iterator_next(hash_iterator_t *i, uint32_t *key_out);

#endif
