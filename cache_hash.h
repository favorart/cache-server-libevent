#include "stdafx.h"

#ifndef _MYSEM_MYSHM_H_
#define _MYSEM_MYSHM_H_
#define _LOCK
//-----------------------------------------
typedef int myshm_t;
typedef int mysem_t;
typedef unsigned short mysem_val_t;
typedef unsigned short mysem_inx_t;

inline mysem_t  mysem_open   (key_t  key);
inline mysem_t  mysem_create (mysem_inx_t num, mysem_val_t *vals);
inline void     mysem_lock   (mysem_t     sid, mysem_inx_t member);
inline void     mysem_unlock (mysem_t     sid, mysem_inx_t member);
inline void     mysem_remove (mysem_t     sid);

inline myshm_t  myshm_create (size_t size);
inline void     myshm_remove (myshm_t mid);
inline char*    myshm_append (myshm_t mid);
//-----------------------------------------
#endif // _MYSEM_MYSHM_H_
//=========================================
#ifndef _CACHE_HASH_H_
#define _CACHE_HASH_H_
//-----------------------------------------
#define CACHE_SIZE  1024U*1024U /* bytes */
#define CACHELINES  256U
//-----------------------------------------
typedef   int32_t ht_key_t;
typedef   int32_t ht_val_t;
typedef  uint16_t hash_t;

typedef struct hashtable_record ht_rec;
struct hashtable_record
{
  ht_key_t  key;
  ht_val_t  val;
    time_t  ttl;
};
//-----------------------------------------
typedef struct hashtable_line ht_line;
struct hashtable_line
{ bool  /*rip,*/ busy;
  ht_rec         data;
};
//-----------------------------------------
/* hash table with open adressing */
typedef struct hash_table hashtable;
struct hash_table
{
  char    *shmemory;
  size_t   shm_size;
  //------------------------
  size_t   lines_count;
  ht_line *lines;
  //------------------------
#ifdef _LOCK
  mysem_t  ln_semid;
  mysem_t  tb_semid;
  myshm_t  tb_shmid;
#endif // _LOCK
  //------------------------
};
//-----------------------------------------
clock_t  ttl_converted (int32_t ttl);
//-----------------------------------------
void hashtable_init (hashtable *ht, size_t lines_count, size_t shm_size);
void hashtable_free (hashtable *ht);

bool hashtable_get (hashtable *ht, ht_rec *data);
bool hashtable_set (hashtable *ht, ht_rec *data);
//-----------------------------------------
#ifdef  _DEBUG_HASH
void  hashtable_print_debug (hashtable *ht, FILE *f);
#endif // _DEBUG_HASH
//-----------------------------------------
#endif // _CACHE_HASH_H_
