/*
  Copyright (C) 2010 Tomash Brechko.  All rights reserved.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "cuckoo_hash.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


static inline
void
compute_hash(uint64_t key, uint32_t *h1, uint32_t *h2)
{
  extern void hashlittle2(const void *key, size_t length,
                          uint32_t *pc, uint32_t *pb);

  /* Initial values are arbitrary.  */
  *h1 = 0x3ac5d673;
  *h2 = 0x6d7839d0;
  hashlittle2(&key, sizeof(key), h1, h2);

  if (*h1 == *h2) {
    *h2 = ~*h2;
  }
}


bool cuckoo_hash_init(struct cuckoo_hash *hash, size_t power) {

  hash->count = 0;
  hash->power = power;
  hash->recursion_limit = 16;
  hash->table = calloc(2 << hash->power, sizeof(*hash->table));
  if (!hash->table)
    return false;

  return true;
}


void cuckoo_hash_destroy(const struct cuckoo_hash *hash) {
  free(hash->table);
}


static inline
struct _cuckoo_hash_elem *
bin_at(const struct cuckoo_hash *hash, uint32_t index)
{
  return (hash->table + index);
}


static inline
struct cuckoo_hash_item *
lookup(const struct cuckoo_hash *hash, uint64_t key,
       uint32_t h1, uint32_t h2)
{
  struct _cuckoo_hash_elem *elem;

  uint32_t mask = (2 << hash->power) - 1;

  elem = bin_at(hash, (h1 & mask));

  if (elem->hash2 == h2 && elem->hash1 == h1
    && elem->hash_item.key == key) {
    return &elem->hash_item;
  }

  elem = bin_at(hash, (h2 & mask));
  if (elem->hash2 == h1 && elem->hash1 == h2
      && elem->hash_item.key == key) {
    return &elem->hash_item;
  }

  return NULL;
}


struct cuckoo_hash_item *
cuckoo_hash_lookup(const struct cuckoo_hash *hash, uint64_t key) {
  uint32_t h1, h2;
  compute_hash(key, &h1, &h2);

  return lookup(hash, key, h1, h2);
}


void cuckoo_hash_remove(struct cuckoo_hash *hash,
                   const struct cuckoo_hash_item *hash_item) {
  assert(hash);
  assert(hash_item);

  struct _cuckoo_hash_elem *elem =
    ((struct _cuckoo_hash_elem *)
     ((char *) hash_item - offsetof(struct _cuckoo_hash_elem, hash_item)));
  elem->hash1 = elem->hash2 = 0;
  --hash->count;
}


static bool grow_table(struct cuckoo_hash *hash) {
  size_t old_size = 2 << hash->power;
  size_t new_size = old_size << 1;

  struct _cuckoo_hash_elem *table = realloc(hash->table, new_size);
  if (!table) {
    return false;
  }

  hash->table = table;
  memset(hash->table + old_size, 0, old_size);

  ++hash->power;

  return true;
}

#if 0
static bool grow_bin_size(struct cuckoo_hash *hash) {
  size_t size =
    ((size_t) hash->bin_size << hash->power) * sizeof(*hash->table);
  uint32_t bin_count = 1U << hash->power;
  size_t add = bin_count * sizeof(*hash->table);
  struct _cuckoo_hash_elem *table = realloc(hash->table, size + add);
  if (! table)
    return false;

  hash->table = table;
  for (uint32_t bin = bin_count - 1; bin > 0; --bin) {
    struct _cuckoo_hash_elem *old = bin_at(hash, bin);
    struct _cuckoo_hash_elem *new = old + bin;
    memmove(new, old, hash->bin_size * sizeof(*hash->table));
    memset(new + hash->bin_size, 0, sizeof(*hash->table));
  }
  memset(hash->table + hash->bin_size, 0, sizeof(*hash->table));

  ++hash->bin_size;

  return true;
}
#endif


static bool undo_insert(struct cuckoo_hash *hash,
    struct _cuckoo_hash_elem *item) {

  uint32_t mask = (2 << hash->power) - 1;

  for (size_t depth = 0; depth < hash->recursion_limit; ++depth) {

    uint32_t h2m = item->hash2 & mask;

    struct _cuckoo_hash_elem *victim = bin_at(hash, h2m);
    struct _cuckoo_hash_elem cpy = *victim;

    uint32_t h1m = victim->hash1 & mask;

    victim->hash_item = item->hash_item;
    victim->hash1 = item->hash2;
    victim->hash2 = item->hash1;

    if (h1m != h2m) {
      assert(depth >= hash->recursion_limit);
      return true;
    }

    *item = cpy;
  }

  return false;
}


static inline bool insert(struct cuckoo_hash *hash,
     struct _cuckoo_hash_elem *item) {
  size_t max_depth = (size_t) 16;

  uint32_t mask = (2 << hash->power) - 1;

  for (size_t depth = 0; depth < hash->recursion_limit; ++depth) {
    uint32_t h1m = item->hash1 & mask;
    struct _cuckoo_hash_elem *elem = bin_at(hash, h1m);

    if (elem->hash1 == elem->hash2 || (elem->hash1 & mask) != h1m) {
      *elem = *item;
      return true;
    }

    struct _cuckoo_hash_elem victim = *elem;

    *elem = *item;

    item->hash_item = victim.hash_item;
    item->hash1 = victim.hash2;
    item->hash2 = victim.hash1;
  }

  // resize, try again.
  if (grow_table(hash)) return insert(hash, item);
  // If we can undo, all is forgiven.
  if (undo_insert(hash, item)) return false;

  // We messed up the table, so die.
  assert(false);
}


struct cuckoo_hash_item *
cuckoo_hash_insert(struct cuckoo_hash *hash, uint64_t key, uint64_t value) {
  uint32_t h1, h2;
  compute_hash(key, &h1, &h2);

  struct cuckoo_hash_item *item = lookup(hash, key, h1, h2);
  if (item) {
    return item;
  }

  struct _cuckoo_hash_elem elem = {
    .hash_item = { .key = key, .value = value },
    .hash1 = h1,
    .hash2 = h2
  };

  if (insert(hash, &elem)) {
    ++hash->count;

    return NULL;
  } else {
    assert(elem.hash_item.key == key);
    assert(elem.hash_item.value == value);
    assert(elem.hash1 == h1);
    assert(elem.hash2 == h2);

    return CUCKOO_HASH_FAILED;
  }
}

struct cuckoo_hash_item *
cuckoo_hash_next(const struct cuckoo_hash *hash,
                 const struct cuckoo_hash_item *hash_item)
{
  struct _cuckoo_hash_elem *elem =
    (hash_item != NULL
     ? ((struct _cuckoo_hash_elem *)
        ((char *) hash_item - offsetof(struct _cuckoo_hash_elem, hash_item))
        + 1)
     : hash->table);

  uint32_t bin_count = 2 << hash->power;
  struct _cuckoo_hash_elem *end = bin_at(hash, bin_count);
  uint32_t mask = bin_count - 1;
  while (elem != end) {
    if (elem->hash1 != elem->hash2) {
      /*
        Test that the element is valid, i.e., its hash1 matches
        the bin index it resides in.
      */
      struct _cuckoo_hash_elem *bin = bin_at(hash, (elem->hash1 & mask));
      if (bin <= elem && elem < bin)
        return &elem->hash_item;
    }

    ++elem;
  }

  return NULL;
}
