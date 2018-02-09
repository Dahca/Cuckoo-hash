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
#if defined(MAP) && defined(UNORDERED_MAP)
#error Either define exactly one of MAP or UNORDERED_MAP, or none
#endif

#include "../src/cuckoo_hash.h"
#ifdef UNORDERED_MAP
#include <unordered_map>
#endif
#include <map>
#include <cassert>
#include <string>
#include <iostream>
#include <memory>
#include <sstream>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#ifdef HAVE_CONFIG_H
#include "autoconfig.h"
#endif
#ifdef HAVE_MALLINFO
#include <malloc.h>
#endif
#include "test.h"


struct Data {
  uint64_t key;
  uint64_t data;
};


template<class Cont>
static inline
Cont *
create()
{
  return new Cont;
}


template<>
inline
cuckoo_hash *
create<cuckoo_hash>()
{
  cuckoo_hash *hash = new cuckoo_hash;
  if (! cuckoo_hash_init(hash, 1))
    throw std::bad_alloc();

  return hash;
}


template<class Cont>
static inline
double
load_factor(Cont *cont)
{
  return cont->load_factor();
}


template<>
inline
double
load_factor<std::map<uint64_t, uint64_t> >(std::map<uint64_t, uint64_t> *)
{
  return 1.0;
}


template<>
inline
double
load_factor<cuckoo_hash>(cuckoo_hash *cont)
{
  /* Peek hash size.  */
  return (static_cast<double>(cuckoo_hash_count(cont))
          / (static_cast<size_t>(2 << cont->power)));
}


template<class Cont>
static inline
void
insert(Cont *cont, Data *d)
{
  cont->insert(std::make_pair(d->key, d->data));
}


template<>
inline
void
insert<cuckoo_hash>(cuckoo_hash *cont, Data *d)
{
  if (cuckoo_hash_insert(cont, d->key, d->data) == CUCKOO_HASH_FAILED)
    throw std::bad_alloc();
}


template<class Cont>
static inline
int
lookup(Cont *cont, const Data *d)
{
  typename Cont::const_iterator it = cont->find(d->key);
  if (it != cont->end()) {
    ok(it->second == d->data);
    return 1;
  }

  return 0;
}


template<>
inline
int
lookup<cuckoo_hash>(cuckoo_hash *cont, const Data *d)
{
  const cuckoo_hash_item *it =
    cuckoo_hash_lookup(cont, d->key);
  if (it != NULL) {
    ok(it->value == d->data);
    return 1;
  }

  return 0;
}


template<class Cont>
static inline
void
remove(Cont *cont, const Data *d) {
  cont->erase(d->key);
}


template<>
inline
void
remove<cuckoo_hash>(cuckoo_hash *cont, const Data *d)
{
  cuckoo_hash_remove(cont, cuckoo_hash_lookup(cont, d->key));
}


template<class Cont>
static inline
size_t
traverse(Cont *cont)
{
  size_t sum = 0;
  for (typename Cont::const_iterator it = cont->begin();
       it != cont->end();
       ++it) {
    sum += it->second;
  }

  return sum;
}


template<>
inline
size_t
traverse<cuckoo_hash>(cuckoo_hash *cont)
{
  uint64_t sum = 0;
  for (const cuckoo_hash_item *cuckoo_hash_each(it, cont)) {
    sum += it->value;
  }

  return sum;
}


#if defined(MAP)

typedef std::map<uint64_t, uint64_t> cont_type;

#elif defined(UNORDERED_MAP)

#ifndef USE_CACHE

typedef std::unordered_map<uint64_t, uint64_t> cont_type;

#else  // USE_CACHE

typedef
  std::__unordered_map<uint64_t, uint64_t,
                       std::hash<uint64_t>,
                       std::equal_to<uint64_t>,
                       std::allocator<std::pair<uint64_t, uint64_t> >,
                       true> cont_type;

#endif  // USE_CACHE

#else

typedef cuckoo_hash cont_type;

#endif


int main(int argc, char *argv[]) {
  if (argc < 3 || argc > 4) {
    std::cerr << "Usage: " << argv[0] << " SEED COUNT [REPEAT]" << std::endl;
    exit(2);
  }

  unsigned int seed;
  {
    std::istringstream arg(argv[1]);
    arg >> seed;
  }

  int count;
  {
    std::istringstream arg(argv[2]);
    arg >> count;
  }

  int repeat = 1;
  if (argc == 4) {
    std::istringstream arg(argv[3]);
    arg >> repeat;
  }

  srand(seed);

  // Allocate a bit more keys to do non-existent queries.
  int total = count * 1.1;

  Data *data = new Data[total];
  for (int i = 0; i < total; ++i) {
    data[i].key = i;
    data[i].data = i;
  }

  // Shuffle.
  for (int n = total - 1; n > 0; --n) {
    int i = rand() / (static_cast<double>(RAND_MAX) + 1) * n;
    std::swap(data[i], data[n]);
  }

  clock_t start, stop;

  cont_type *cont = create<cont_type>();

#ifdef HAVE_MALLINFO
  struct mallinfo before = mallinfo();
#endif

  size_t sum = 0;
  start = clock();
  for (int i = 0; i < count; ++i) {
    insert(cont, &data[i]);
    sum += data[i].data;
  }
  stop = clock();

#ifdef HAVE_MALLINFO
  struct mallinfo after = mallinfo();
  std::cout << "used memory: "
            << (after.hblkhd + after.arena) - (before.hblkhd + before.arena)
            << std::endl;
#endif

  std::cout << "load factor: " << load_factor(cont) << std::endl;
  std::cout << "insert: "
            << static_cast<double>(stop - start) / CLOCKS_PER_SEC << " sec"
            << std::endl;

  start = clock();
  for (int j = 0; j < repeat; ++j) {
    int found = 0;
    for (int i = 0; i < total; ++i) {
      found += lookup(cont, &data[i]);
    }
    ok(found == count);
  }
  stop = clock();
  std::cout << "lookup (x " << repeat << "): "
            << static_cast<double>(stop - start) / CLOCKS_PER_SEC << " sec"
            << std::endl;

  start = clock();
  for (int j = 0; j < repeat; ++j) {
    size_t s = traverse(cont);
    ok(s == sum, " found: %lu, expected: %d", s, sum);
  }
  stop = clock();
  std::cout << "traverse (x " << repeat << "): "
            << static_cast<double>(stop - start) / CLOCKS_PER_SEC << " sec"
            << std::endl;

  start = clock();
  for (int i = 0; i < count; ++i)
    remove(cont, &data[i]);
  stop = clock();
  std::cout << "remove: "
            << static_cast<double>(stop - start) / CLOCKS_PER_SEC << " sec"
            << std::endl;

  return 0;
}
