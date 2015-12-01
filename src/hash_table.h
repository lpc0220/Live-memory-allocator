/* add by lpc */

#ifndef _HASH_TABLE_H_
#define _HASH_TABLE_H_

#include "utility.h"
#include <sys/mman.h>
#include <iostream>
#include <stdlib.h>

typedef struct _link_list_t {
  struct list_head list;
  void * ptr;
  long long time;
}link_list_t;

typedef struct _hash_table_t {
  struct hlist_head * elem_bucket;
  unsigned int bucket_size;
  unsigned int bucket_mask;
  unsigned int total_num;
}hash_table_t;

typedef struct _hash_table_elem_t{
  struct hlist_node list;
  void * ptr;
  link_list_t *link_node;
}hash_table_elem_t;

#define DEFAULT_BUCKET_SIZE 15

/* for queue */
inline int queue_empty(struct list_head * head)
{
    return list_empty(head); 
}

inline int queue_size(struct list_head *head)
{
    int size = 0;
    link_list_t * tmp;
    link_list_t * n;
    list_for_each_entry_safe( tmp, n, head, list) {
        size++;
    }

    return size;
}

inline long long delete_queue_node(link_list_t * node)
{
    list_del(&node->list);
    long long time = node->time;

    int ret = munmap(node, sizeof(link_list_t));
    if ( ret == -1 ) {
        std::cout << "Error: mumap for link list element failed!\n";
        abort();
    }

    return time;
}

inline long long dequeue(struct list_head * head)
{
    link_list_t * first = list_first_entry(head, link_list_t, list);
    list_del(&first->list);
   
    long long time = first->time;    

    int ret = munmap(first, sizeof(link_list_t));
    if ( ret == -1 ) {
        std::cout << "Error: mumap for link list element failed!\n";
        abort();
    } 

    return time;
}

inline link_list_t * enqueue(struct list_head * head, void * ptr, long long time)
{
    link_list_t *new_node =
                 (link_list_t *)mmap(0, sizeof(link_list_t),
                 PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if ( new_node == (link_list_t *)-1 ) {
        std::cout << "Error: mmap for hash table element failed!\n";
        abort();
    }
    new_node->ptr  = ptr;
    new_node->time = time;
    list_add_tail(&new_node->list, head);

    return new_node;
}

/* for stack */
inline int stack_empty(struct list_head * head)
{
    return list_empty(head); 
}

inline void stack_pop(struct list_head * head)
{
    link_list_t * first = list_first_entry(head, link_list_t, list);
    list_del(&first->list);

    int ret = munmap(first, sizeof(link_list_t));
    if ( ret == -1 ) {
        std::cout << "Error: mumap for link list element failed!\n";
        abort();
    } 
}

inline long long stack_top(struct list_head * head)
{
    link_list_t * first = list_first_entry(head, link_list_t, list);
    return first->time;
}

inline void stack_push(struct list_head * head, void * ptr, long long time)
{
    link_list_t *new_node =
                 (link_list_t *)mmap(0, sizeof(link_list_t),
                 PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if ( new_node == (link_list_t *)-1 ) {
        std::cout << "Error: mmap for hash table element failed!\n";
        abort();
    }
    new_node->ptr  = ptr;
    new_node->time = time;
    list_add(&new_node->list, head);
}

/* for hash table */
const unsigned int hash_rd = 0x20141023;

inline unsigned int hashkey( hash_table_t * ht , void * ptr)
{
    return ((unsigned long long)ptr) % ht->bucket_mask ;
}

inline hash_table_t * create_hash_table()
{
    hash_table_t * ht;
    ht = (hash_table_t *)mmap(0, sizeof(hash_table_t),
                 PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if ( ht == (hash_table_t *)-1 ) {
        std::cout << "Error: mmap for hash table element failed!\n";
        abort();
    }
    ht->bucket_size = 1 << DEFAULT_BUCKET_SIZE;
    ht->bucket_mask = ht->bucket_size - 1;
    ht->total_num   = 0;
    ht->elem_bucket = (struct hlist_head *)(0, sizeof(struct hlist_head) * ht->bucket_size,
                  PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if ( ht->elem_bucket == (struct hlist_head *)-1 ) {
        std::cout << "Error: mmap for hash table element failed!\n";
        abort();
    }
    for(unsigned int idx = 0; idx < ht->bucket_size; idx++) {
      if ( &ht->elem_bucket[idx] == NULL ) printf("null\n");
    //  INIT_HLIST_HEAD(&ht->elem_bucket[idx]);
    }
}

inline void destroy_hash_table(hash_table_t *ht) 
{
    int ret = munmap(ht->elem_bucket, sizeof(struct hlist_head) * ht->bucket_size);
    if ( ret == -1 ) {
        std::cout << "Error: mumap for hash table element failed!\n";
        abort();
    }

    ret = munmap(ht, sizeof(hash_table_t));
    if ( ret == -1 ) {
        std::cout << "Error: mumap for hash table element failed!\n";
        abort();
    }
}

inline void hash_table_add_by_elem(hash_table_t * ht, hash_table_elem_t * elem)
{
    unsigned int hash = hashkey(ht, elem->ptr);
    hlist_add_head( &elem->list, &ht->elem_bucket[hash] );
}

inline void hash_table_add(hash_table_t *ht, void *ptr, link_list_t* link_node)
{
    hash_table_elem_t * new_hash_node =
                 (hash_table_elem_t *)mmap(0, sizeof(hash_table_elem_t),
                 PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if ( new_hash_node == (hash_table_elem_t *)-1 ) {
        std::cout << "Error: mmap for hash table element failed!\n";
        abort();
    }
    new_hash_node->ptr = ptr;
    new_hash_node->link_node = link_node;
    INIT_HLIST_NODE(&new_hash_node->list);
    hash_table_add_by_elem(ht, new_hash_node);
}

inline hash_table_elem_t * hash_table_get_elem(hash_table_t *ht, void * ptr)
{
    unsigned int hash = hashkey(ht, ptr);
    hash_table_elem_t * elem;
    struct hlist_node *n;

    hlist_for_each_entry(elem, n, &ht->elem_bucket[hash], list) {
        if ( ptr == elem->ptr ) {
            return elem;
        }
    }

    return NULL;
}

inline link_list_t * hash_table_get( hash_table_t * ht, void * ptr )
{
  unsigned int hash = hashkey(ht, ptr);
  hash_table_elem_t * elem;
  struct hlist_node *n;

  hlist_for_each_entry(elem, n, &ht->elem_bucket[hash], list) {
      if ( ptr == elem->ptr ) {
          return elem->link_node;
      }
  }

  return NULL;
}

inline void hash_table_del(hash_table_t * ht, void * ptr)
{
    hash_table_elem_t *elem = hash_table_get_elem(ht, ptr);
    if ( elem == NULL ) return;

    hlist_del(&elem->list);
    int ret = munmap(elem, sizeof(hash_table_elem_t));
    if ( ret == -1 ) {
        std::cout << "Error: mumap for hash table element failed!\n";
        abort();
    }
}

#endif
