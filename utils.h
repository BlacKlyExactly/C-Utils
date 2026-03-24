#pragma once

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ================================================================
// ARENA
// ================================================================

#define ARENA_ALIGN 16
#define ALIGN(s) (((s) + (ARENA_ALIGN - 1)) & ~(ARENA_ALIGN - 1))

typedef struct ArenaChunk {
  struct ArenaChunk *next;
  size_t capacity;
  size_t used;
  char data[];
} ArenaChunk;

typedef struct ArenaSnapshot {
  size_t used;
  ArenaChunk *chunk;
} ArenaSnapshot;

typedef struct Arena {
  size_t chunk_capacity;
  ArenaChunk *head;
} Arena;

static inline Arena arena_init(size_t chunk_capacity) {
  return (Arena){
      .chunk_capacity = chunk_capacity,
      .head = NULL,
  };
}

static inline void *arena_alloc(Arena *arena, size_t size) {
  if (size == 0)
    return NULL;

  size = ALIGN(size);

  if (!arena->head || arena->head->used + size > arena->head->capacity) {
    size_t chunk_size = arena->chunk_capacity;

    if (chunk_size < size)
      chunk_size = size;

    ArenaChunk *chunk = (ArenaChunk *)malloc(sizeof(ArenaChunk) + chunk_size);
    if (!chunk)
      return NULL;

    chunk->used = 0;
    chunk->capacity = chunk_size;
    chunk->next = arena->head;
    arena->head = chunk;
  }

  void *ptr = arena->head->data + arena->head->used;
  arena->head->used += size;
  return ptr;
}

static inline void arena_free(Arena *arena) {
  ArenaChunk *curr = arena->head;

  while (curr) {
    ArenaChunk *next = curr->next;
    free(curr);
    curr = next;
  }

  arena->head = NULL;
}

static inline void arena_reset(Arena *arena) {
  ArenaChunk *curr = arena->head;
  while (curr) {
    curr->used = 0;
    curr = curr->next;
  }
}

static inline ArenaSnapshot arena_snapshot(Arena *arena) {
  return (ArenaSnapshot){
      .used = arena->head ? arena->head->used : 0,
      .chunk = arena->head,
  };
}

static inline void arena_restore(Arena *arena, ArenaSnapshot snapshot) {
  if (!snapshot.chunk) {
    arena_free(arena);
    return;
  }

  while (arena->head && arena->head != snapshot.chunk) {
    ArenaChunk *next = arena->head->next;
    free(arena->head);
    arena->head = next;
  }

  if (arena->head)
    arena->head->used = snapshot.used;
}

// ================================================================
// STRING VIEW
// ================================================================

#define SV_FMT "%.*s"
#define SV_ARG(sv) (int)(sv).len, (sv).data

typedef struct StringView {
  const char *data;
  size_t len;
} StringView;

typedef struct {
  StringView *elements;
  size_t len;
  Arena *arena;
} StringViewArray;

#define SV_LIT(s) ((StringView){(s), sizeof(s) - 1})

static inline StringView sv_from(const char *str) {
  return (StringView){
      .data = str,
      .len = strlen(str),
  };
}

static inline StringView sv_trim_left(StringView sv) {
  while (sv.len && isspace(*sv.data)) {
    sv.len--;
    sv.data++;
  }

  return sv;
}

static inline StringView sv_trim_right(StringView sv) {
  while (sv.len && isspace(sv.data[sv.len - 1])) {
    sv.len--;
  }

  return sv;
}

static inline StringView sv_trim(StringView sv) {
  sv = sv_trim_left(sv);
  sv = sv_trim_right(sv);
  return sv;
}

static inline int sv_eq(StringView sv_a, StringView sv_b) {
  return sv_a.len == sv_b.len && memcmp(sv_a.data, sv_b.data, sv_a.len) == 0;
}

static inline int sv_cmp(StringView a, StringView b) {
  size_t min_len = a.len < b.len ? a.len : b.len;

  int result = memcmp(a.data, b.data, min_len);

  if (result != 0)
    return result;

  return a.len - b.len;
}

static inline StringView sv_slice(StringView sv, size_t start, size_t end) {
  if (start > sv.len)
    start = sv.len;

  if (end > sv.len)
    end = sv.len;

  if (start > end)
    start = end;

  return (StringView){
      .data = sv.data + start,
      .len = end - start,
  };
}

static inline int sv_starts_with(StringView sv, StringView prefix) {
  return sv.len >= prefix.len && memcmp(sv.data, prefix.data, prefix.len) == 0;
}

static inline int sv_ends_with(StringView sv, StringView sufix) {
  return sv.len >= sufix.len &&
         memcmp(sv.data + sv.len - sufix.len, sufix.data, sufix.len) == 0;
}

static inline ptrdiff_t sv_index_of(StringView sv, StringView sub) {
  if (sub.len == 0)
    return 0;

  const char *start = sv.data;

  while (sv.len >= sub.len) {
    if (sv_starts_with(sv, sub))
      return sv.data - start;

    sv.len--;
    sv.data++;
  }

  return -1;
}

static inline int sv_includes(StringView sv, StringView sub) {
  return sv_index_of(sv, sub) != -1;
}

static inline size_t sv_count(StringView sv, StringView sub) {
  if (sub.len == 0)
    return sv.len + 1;

  size_t count = 0;
  StringView tmp = sv;

  while (tmp.len >= sub.len) {
    ptrdiff_t pos = sv_index_of(tmp, sub);

    if (pos == -1)
      break;

    tmp.data += pos + sub.len;
    tmp.len -= pos + sub.len;
    count++;
  }

  return count;
}

static inline StringView sv_to_upper(Arena *arena, StringView sv) {
  char *buffer = (char *)arena_alloc(arena, sv.len);
  if (!buffer)
    return (StringView){0};

  for (size_t i = 0; i < sv.len; i++) {
    buffer[i] = toupper((unsigned char)sv.data[i]);
  }

  return (StringView){
      .data = buffer,
      .len = sv.len,
  };
}

static inline StringView sv_to_lower(Arena *arena, StringView sv) {
  char *buffer = (char *)arena_alloc(arena, sv.len);

  if (!buffer)
    return (StringView){0};

  for (size_t i = 0; i < sv.len; i++) {
    buffer[i] = tolower((unsigned char)sv.data[i]);
  }

  return (StringView){
      .data = buffer,
      .len = sv.len,
  };
}

static inline StringView sv_replace(Arena *arena, StringView sv,
                                    StringView old_sv, StringView new_sv) {
  ptrdiff_t pos = sv_index_of(sv, old_sv);

  if (pos == -1)
    return sv;

  size_t len = sv.len - old_sv.len + new_sv.len;
  char *buffer = (char *)arena_alloc(arena, len);

  if (!buffer)
    return (StringView){0};

  memcpy(buffer, sv.data, pos);
  memcpy(buffer + pos, new_sv.data, new_sv.len);
  memcpy(buffer + pos + new_sv.len, sv.data + pos + old_sv.len,
         sv.len - pos - old_sv.len);

  return (StringView){.data = buffer, .len = len};
}

static inline StringView sv_replace_all(Arena *arena, StringView sv,
                                        StringView old_sv, StringView new_sv) {
  size_t count = sv_count(sv, old_sv);
  if (count == 0)
    return sv;

  size_t len = sv.len - (old_sv.len * count) + (new_sv.len * count);
  char *buffer = (char *)arena_alloc(arena, len);

  if (!buffer)
    return (StringView){0};

  char *out = buffer;

  while (sv.len >= old_sv.len) {
    ptrdiff_t pos = sv_index_of(sv, old_sv);
    if (pos == -1)
      break;

    memcpy(out, sv.data, pos);
    out += pos;

    memcpy(out, new_sv.data, new_sv.len);
    out += new_sv.len;

    sv.data += pos + old_sv.len;
    sv.len -= pos + old_sv.len;
  }

  memcpy(out, sv.data, sv.len);

  return (StringView){.data = buffer, .len = len};
}

static inline StringView sv_reverse(Arena *arena, StringView sv) {
  char *buffer = (char *)arena_alloc(arena, sv.len);

  if (!buffer)
    return (StringView){0};

  for (size_t i = 0; i < sv.len; i++) {
    buffer[i] = sv.data[sv.len - 1 - i];
  }

  return (StringView){
      .data = buffer,
      .len = sv.len,
  };
}

static inline StringView sv_repeat(Arena *arena, StringView sv, size_t n) {
  if (sv.len == 0 || n == 0)
    return (StringView){0};

  if (sv.len > SIZE_MAX / n)
    return (StringView){0};

  size_t len = sv.len * n;
  char *buffer = (char *)arena_alloc(arena, len);

  if (!buffer)
    return (StringView){0};

  for (size_t i = 0; i < len; i += sv.len) {
    memcpy(buffer + i, sv.data, sv.len);
  }

  return (StringView){
      .data = buffer,
      .len = len,
  };
}

static inline StringViewArray sv_split(Arena *arena, StringView sv,
                                       StringView delimeter) {
  if (delimeter.len == 0) {
    StringView *elements =
        (StringView *)arena_alloc(arena, sizeof(StringView) * sv.len);

    if (!elements)
      return (StringViewArray){0};

    for (size_t i = 0; i < sv.len; i++) {
      elements[i] = (StringView){
          .data = sv.data + i,
          .len = 1,
      };
    }

    return (StringViewArray){
        .elements = elements,
        .len = sv.len,
    };
  }

  size_t count = sv_count(sv, delimeter) + 1;
  StringView *elements =
      (StringView *)arena_alloc(arena, sizeof(StringView) * count);

  if (!elements)
    return (StringViewArray){0};

  size_t idx = 0;
  while (sv.len) {
    ptrdiff_t pos = sv_index_of(sv, delimeter);
    if (pos == -1)
      break;

    elements[idx++] = (StringView){
        .data = sv.data,
        .len = (size_t)pos,
    };

    sv.len -= delimeter.len + pos;
    sv.data += delimeter.len + pos;
  }

  elements[idx++] = sv;

  return (StringViewArray){
      .elements = elements,
      .len = idx,
      .arena = arena,
  };
}

typedef void (*SvArr_Iterator)(Arena *, StringView, void *);

static inline void sv_arr_iterate(StringViewArray sv_arr,
                                  SvArr_Iterator iterator, void *data) {
  for (size_t i = 0; i < sv_arr.len; i++) {
    iterator(sv_arr.arena, sv_arr.elements[i], data);
  }
}

static inline size_t sv_arr_sum_len(StringViewArray sv_arr) {
  size_t len = 0;

  for (size_t i = 0; i < sv_arr.len; i++) {
    len += sv_arr.elements[i].len;
  }

  return len;
}

static inline StringView sv_join(Arena *arena, StringViewArray sv_arr,
                                 StringView delimeter) {
  if (sv_arr.len == 0)
    return (StringView){NULL, 0};

  size_t len = (sv_arr.len - 1) * delimeter.len + sv_arr_sum_len(sv_arr);
  char *buffer = (char *)arena_alloc(arena, len);

  if (!buffer)
    return (StringView){0};

  char *out = buffer;

  for (size_t i = 0; i < sv_arr.len; i++) {
    StringView sv = sv_arr.elements[i];
    memcpy(out, sv.data, sv.len);
    out += sv.len;

    if (i != sv_arr.len - 1) {
      memcpy(out, delimeter.data, delimeter.len);
      out += delimeter.len;
    }
  }

  return (StringView){
      .data = buffer,
      .len = len,
  };
}

static inline char *sv_to_cstr(Arena *arena, StringView sv) {
  char *buffer = (char *)arena_alloc(arena, sv.len + 1);

  if (!buffer)
    return NULL;

  memcpy(buffer, sv.data, sv.len);
  buffer[sv.len] = '\0';

  return buffer;
}

static inline int sv_try_split_once(StringView *sv, StringView sep,
                                    StringView *tok) {
  ptrdiff_t pos = sv_index_of(*sv, sep);

  if (pos == -1) {
    *tok = *sv;
    sv->len = 0;
    return 0;
  }

  *tok = sv_slice(*sv, 0, pos);
  sv->data += pos + sep.len;
  sv->len -= pos + sep.len;

  return 1;
}

static inline StringView sv_split_once(StringView *sv, StringView sep) {
  StringView tok;
  sv_try_split_once(sv, sep, &tok);
  return tok;
}

static inline size_t sv_hash(StringView sv, size_t capacity) {
  size_t h = 5381;
  for (size_t i = 0; i < sv.len; i++) {
    h = h * 33 + sv.data[i];
  }
  return h % capacity;
}

static inline StringView sv_fmt(Arena *arena, const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  size_t len = vsnprintf(NULL, 0, fmt, args);
  va_end(args);

  char *buffer = (char *)arena_alloc(arena, len + 1);

  va_start(args, fmt);
  vsnprintf(buffer, len + 1, fmt, args);
  va_end(args);

  return (StringView){
      .data = buffer,
      .len = len,
  };
}

static inline StringView sv_read_file(Arena *arena, const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return (StringView){0};

  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);
  rewind(f);

  char *buffer = (char *)arena_alloc(arena, size);

  if (!buffer) {
    fclose(f);
    return (StringView){0};
  }

  fread(buffer, 1, size, f);
  fclose(f);

  return (StringView){.data = buffer, .len = size};
}

static inline int sv_write_file(const char *path, StringView sv) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return 0;
  fwrite(sv.data, 1, sv.len, f);
  fclose(f);
  return 1;
}

// ================================================================
// HASH TABLE
// ================================================================

#define HT_CAPACITY 67

typedef struct HashTableEntry {
  StringView key;
  void *value;
  struct HashTableEntry *next;
} HashTableEntry;

typedef struct HashTable {
  HashTableEntry *buckets[HT_CAPACITY];
  Arena *arena;
  size_t size;
} HashTable;

static inline HashTable ht_init(Arena *arena) {
  HashTable ht;
  memset(ht.buckets, 0, sizeof(ht.buckets));
  ht.size = 0;
  ht.arena = arena;

  return ht;
}

static inline void ht_set(HashTable *ht, StringView sv_key, void *value) {
  size_t i = sv_hash(sv_key, HT_CAPACITY);
  HashTableEntry *entry = ht->buckets[i];

  while (entry) {
    if (sv_eq(entry->key, sv_key)) {
      entry->value = value;
      return;
    }

    entry = entry->next;
  }

  HashTableEntry *new_entry =
      (HashTableEntry *)arena_alloc(ht->arena, sizeof(HashTableEntry));

  if (!new_entry)
    return;

  char *key_data = (char *)arena_alloc(ht->arena, sv_key.len);
  memcpy(key_data, sv_key.data, sv_key.len);

  new_entry->key = (StringView){.data = key_data, .len = sv_key.len};
  new_entry->value = value;
  new_entry->next = ht->buckets[i];
  ht->buckets[i] = new_entry;
  ht->size++;
}

static inline void *ht_get(HashTable *ht, StringView sv_key) {
  size_t i = sv_hash(sv_key, HT_CAPACITY);
  HashTableEntry *entry = ht->buckets[i];

  while (entry) {
    if (sv_eq(entry->key, sv_key)) {
      return entry->value;
    }

    entry = entry->next;
  }

  return NULL;
}

static inline void ht_delete(HashTable *ht, StringView sv_key) {
  size_t i = sv_hash(sv_key, HT_CAPACITY);
  HashTableEntry *entry = ht->buckets[i];
  HashTableEntry *prev = NULL;

  while (entry) {
    if (sv_eq(entry->key, sv_key)) {
      if (prev)
        prev->next = entry->next;
      else
        ht->buckets[i] = entry->next;

      ht->size--;
      return;
    }

    prev = entry;
    entry = entry->next;
  }
}

typedef void (*HT_Iterator)(HashTable *, HashTableEntry *, void *, void *);

static inline void ht_iterate(HashTable *ht, HT_Iterator iterator, void *data) {
  for (size_t i = 0; i < HT_CAPACITY; i++) {
    HashTableEntry *entry = ht->buckets[i];

    while (entry) {
      iterator(ht, entry, entry->value, data);
      entry = entry->next;
    }
  }
}

// ================================================================
// DYNAMIC ARRAY
// ================================================================

typedef struct DynamicArray {
  void **data;
  size_t capacity;
  size_t len;
  Arena *arena;
} DynamicArray;

static inline DynamicArray da_init(Arena *arena, size_t capacity) {
  DynamicArray da = {.data =
                         (void **)arena_alloc(arena, sizeof(void *) * capacity),
                     .capacity = capacity,
                     .len = 0,
                     .arena = arena};

  return da;
}

static inline void da_push(DynamicArray *da, void *item) {
  if (da->len >= da->capacity) {
    size_t new_cap = da->capacity * 2;

    void **new_data = (void **)arena_alloc(da->arena, sizeof(void *) * new_cap);

    if (!new_data)
      return;

    memcpy(new_data, da->data, sizeof(void *) * da->len);
    da->data = new_data;
    da->capacity = new_cap;
  }

  da->data[da->len++] = item;
}

static inline void da_pop(DynamicArray *da) {
  if (da->len == 0)
    return;

  da->data[da->len - 1] = NULL;
  da->len--;
}

static inline void *da_get(DynamicArray *da, size_t index) {
  if (da->len == 0 || index >= da->len)
    return NULL;

  return da->data[index];
}

typedef void (*DA_Iterator)(DynamicArray *, void *, void *);

static inline void da_iterate(DynamicArray *da, DA_Iterator iterator,
                              void *data) {
  for (size_t i = 0; i < da->len; i++) {
    iterator(da, da->data[i], data);
  }
}

// ================================================================
// LINKED LIST
// ================================================================

typedef struct ListEntry {
  void *data;
  struct ListEntry *next;
  struct ListEntry *prev;
} ListEntry;

typedef struct List {
  ListEntry *head;
  ListEntry *tail;
  size_t len;
  Arena *arena;
} List;

static inline List list_init(Arena *arena) {
  return (List){
      .arena = arena,
      .head = NULL,
      .tail = NULL,
      .len = 0,
  };
}

static inline void list_append(List *list, void *data) {
  ListEntry *entry = (ListEntry *)arena_alloc(list->arena, sizeof(ListEntry));
  entry->data = data;
  entry->next = NULL;

  if (list->head == NULL) {
    entry->prev = NULL;
    list->head = entry;
    list->tail = entry;
  } else {
    entry->prev = list->tail;
    list->tail->next = entry;
    list->tail = entry;
  }

  list->len++;
}

typedef void (*List_Iterator)(void *data, void *user_data);

static inline void list_iterate(List *list, List_Iterator iterator,
                                void *user_data) {
  ListEntry *entry = list->head;
  while (entry) {
    iterator(entry->data, user_data);
    entry = entry->next;
  }
}

// ================================================================
// BINARY SEARCH TREE
// ================================================================

typedef int (*CmpFn)(void *a, void *b);

typedef struct BST_Node {
  void *data;
  struct BST_Node *left;
  struct BST_Node *right;
} BST_Node;

typedef struct BST {
  BST_Node *root;
  CmpFn cmp;
  Arena *arena;
} BST;

typedef enum BST_IterateType {
  BST_PREORDER,
  BST_INORDER,
  BST_POSTORDER
} BST_IterateType;

static inline int cmp_int(void *a, void *b) {
  int ia = *(int *)a, ib = *(int *)b;
  return (ia > ib) - (ia < ib);
}

static inline int cmp_cstr(void *a, void *b) {
  return strcmp((char *)a, (char *)b);
}

static inline int cmp_sv(void *a, void *b) {
  return sv_cmp(*(StringView *)a, *(StringView *)b);
}

static inline int cmp_float(void *a, void *b) {
  float fa = *(float *)a, fb = *(float *)b;
  return (fa > fb) - (fa < fb);
}

static inline int cmp_size(void *a, void *b) {
  size_t sa = *(size_t *)a, sb = *(size_t *)b;
  return (sa > sb) - (sa < sb);
}

static inline BST bst_init(Arena *arena, CmpFn cmp) {
  return (BST){
      .cmp = cmp,
      .arena = arena,
      .root = NULL,
  };
}

static inline BST_Node *bst_insert_helper(BST *bst, BST_Node *node,
                                          void *data) {
  if (node == NULL) {
    BST_Node *new_node = (BST_Node *)arena_alloc(bst->arena, sizeof(BST_Node));
    if (!new_node)
      return NULL;

    new_node->data = data;
    new_node->left = NULL;
    new_node->right = NULL;

    return new_node;
  }

  int cmp = bst->cmp(data, node->data);

  if (cmp < 0)
    node->left = bst_insert_helper(bst, node->left, data);
  else
    node->right = bst_insert_helper(bst, node->right, data);

  return node;
}

static inline void bst_insert(BST *bst, void *data) {
  bst->root = bst_insert_helper(bst, bst->root, data);
}

static inline BST_Node *bst_search_helper(BST *bst, BST_Node *node,
                                          void *data) {
  if (node == NULL) {
    return NULL;
  }

  int cmp = bst->cmp(data, node->data);

  if (cmp == 0)
    return node;
  else if (cmp < 0)
    return bst_search_helper(bst, node->left, data);
  else
    return bst_search_helper(bst, node->right, data);
}

static inline BST_Node *bst_search(BST *bst, void *data) {
  if (bst->root == NULL)
    return NULL;

  return bst_search_helper(bst, bst->root, data);
}

static inline BST_Node *bst_min_node(BST_Node *node) {
  while (node->left)
    node = node->left;

  return node;
}

static inline BST_Node *bst_remove_helper(BST *bst, BST_Node *node,
                                          void *data) {
  if (node == NULL)
    return NULL;

  int cmp = bst->cmp(data, node->data);

  if (cmp < 0)
    node->left = bst_remove_helper(bst, node->left, data);
  else if (cmp > 0)
    node->right = bst_remove_helper(bst, node->right, data);
  else {
    if (node->left == NULL)
      return node->right;
    else if (node->right == NULL)
      return node->left;

    BST_Node *successor = bst_min_node(node->right);
    node->data = successor->data;
    node->right = bst_remove_helper(bst, node->right, successor->data);
  }

  return node;
}

static inline void bst_delete(BST *bst, void *data) {
  bst->root = bst_remove_helper(bst, bst->root, data);
}

typedef void (*BST_Iterator)(BST_Node *node, void *data);

static inline void
bst_iterate_preorder_helper(BST_Node *node, BST_Iterator iterator, void *data) {
  if (!node)
    return;

  iterator(node, data);
  bst_iterate_preorder_helper(node->left, iterator, data);
  bst_iterate_preorder_helper(node->right, iterator, data);
}

static inline void
bst_iterate_inorder_helper(BST_Node *node, BST_Iterator iterator, void *data) {
  if (!node)
    return;

  bst_iterate_inorder_helper(node->left, iterator, data);
  iterator(node, data);
  bst_iterate_inorder_helper(node->right, iterator, data);
}

static inline void bst_iterate_postorder_helper(BST_Node *node,
                                                BST_Iterator iterator,
                                                void *data) {
  if (!node)
    return;

  bst_iterate_postorder_helper(node->left, iterator, data);
  bst_iterate_postorder_helper(node->right, iterator, data);
  iterator(node, data);
}

static inline void bst_iterate_preorder(BST *bst, BST_Iterator iterator,
                                        void *data) {
  bst_iterate_preorder_helper(bst->root, iterator, data);
}

static inline void bst_iterate_inorder(BST *bst, BST_Iterator iterator,
                                       void *data) {
  bst_iterate_inorder_helper(bst->root, iterator, data);
}

static inline void bst_iterate_postorder(BST *bst, BST_Iterator iterator,
                                         void *data) {
  bst_iterate_postorder_helper(bst->root, iterator, data);
}

static inline void bst_iterate(BST *bst, BST_IterateType iterate_type,
                               BST_Iterator iterator, void *data) {
  switch (iterate_type) {
  case BST_PREORDER:
    bst_iterate_preorder_helper(bst->root, iterator, data);
    break;
  case BST_INORDER:
    bst_iterate_inorder_helper(bst->root, iterator, data);
    break;
  case BST_POSTORDER:
    bst_iterate_postorder_helper(bst->root, iterator, data);
    break;
  }
}

// ================================================================
// STRING BUILDER
// ================================================================

typedef struct StringBuilder {
  char *data;
  size_t len;
  size_t capacity;
  Arena *arena;
} StringBuilder;

static inline StringBuilder sb_init(Arena *arena, size_t capacity) {
  return (StringBuilder){.data = (char *)arena_alloc(arena, capacity),
                         .len = 0,
                         .capacity = capacity,
                         .arena = arena};
}

static inline void sb_append(StringBuilder *sb, StringView sv) {
  if (sb->len + sv.len >= sb->capacity) {
    size_t new_cap = (sb->capacity * 2) + sv.len;
    char *new_data = (char *)arena_alloc(sb->arena, new_cap);
    memcpy(new_data, sb->data, sb->len);

    sb->data = new_data;
    sb->capacity = new_cap;
  }

  memcpy(sb->data + sb->len, sv.data, sv.len);
  sb->len += sv.len;
}

static inline StringView sb_view(StringBuilder *sb) {
  return (StringView){.data = sb->data, .len = sb->len};
}

static inline char *sb_cstr(StringBuilder *sb) {
  return sv_to_cstr(sb->arena, sb_view(sb));
}
