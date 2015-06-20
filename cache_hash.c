#include "stdafx.h"
#include "cache_error.h"
#include "cache_hash.h"

//-----------------------------------------
#include <time.h>
/* Relative TTL to absolute TTL (in seconds) */
time_t ttl_converted (int32_t ttl)
{ return (time (NULL) + ttl); }
bool   ttl_completed (time_t  ttl)
{ return (time (NULL) > ttl); }
//-----------------------------------------
void  hashtable_init (hashtable *ht, size_t lines_count, size_t shm_size)
{
  //------------------------------------------
  ht->lines_count = lines_count;
  if ( sizeof (ht_line) * lines_count > shm_size )
  {
    perror ("ht size");
    ht->shm_size = lines_count * sizeof (ht_line);
  }
  else
  {
    ht->shm_size = shm_size;
  }
  //-----------------------------------------
#ifdef _DEBUG
  static char mem[CACHELINES * sizeof (ht_line)] = { 0 };
  ht->shmemory = mem;
  ht->lines = (ht_line*) ht->shmemory;
#elif _LOCK
  /* Создание одного семафора, проинициализированного */
  ht->tb_semid = mysem_create (IPC_PRIVATE, 1U, 1);
   
  /* Создание общей памяти */
  ht->tb_shmid = mymem_create (IPC_PRIVATE, size);
  ht->shmemory = mymem_append (ht->tb_shmid);
   
  /* Инициализация общей памяти */
  memset (ht->shmemory, 0, shm_size);
#endif // _LOCK
  //-----------------------------------------
  return;
}
void  hashtable_free (hashtable *ht)
{
#ifdef _LOCK
  mymem_remove (ht->tb_shmid);
  mysem_remove (ht->tb_semid);
#endif // _LOCK
}
//-----------------------------------------
// std::hash<string>
static hash_t  hash (int32_t key)
{
  char *k = &key;
  uint32_t h = 2139062143;

  for ( int i = 0; i < sizeof (key); ++i )
    h = h * 37 + k[i];

  return (hash_t) h;
}
static hash_t  hashtable_walk1 (hashtable *ht, ht_rec *data, hash_t he)
{
  hash_t h = he, shift = 1U;
  //-----------------------------------------
#ifdef _DEBUG
  printf ("hash=%d ", he);
#endif // _DEBUG
  //-----------------------------------------
  /* идём по открытой адресации */
  while ( (h < ht->lines_count) && ht->lines[h].busy )
  { /* если встречаем оконченный ttl */
    if ( ttl_completed (ht->lines[h].data.ttl) )
    { ht->lines[h].busy = false;

      /* если впереди нет ничего в открытой адресации */
      if ( ((h + shift) >= ht->lines_count) || !ht->lines[h + shift].busy )
      { break; } /* нашли необходимый номер - заканчиваем */

      /* check for current hash is less then its index */
      hash_t hn = hash (ht->lines[h + shift].data.key) % ht->lines_count;
      if ( (h + shift) > hn )
      { /* сдвигаем cледующего на данное место */
        SWAP (&ht->lines[h], &ht->lines[h + shift]);
        ++shift;
      }
      else { break; } /* не сдвигаем, нашли необходимый номер - заканчиваем */
    }
    else if ( ht->lines[h].data.key == data->key )
    {
      break; /* here get or set */
    }
    else if ( he < hash (ht->lines[h].data.key) % ht->lines_count )
    {
      // hashtable_walk (ht, data, he);
      break;
    }
    else
    { ++h; if ( shift > 1 ) --shift; }
  } // end while
  //-----------------------------------------
  /* here set */
  return h;
}

hash_t  hashtable_walk (hashtable *ht, ht_rec *data, hash_t he);
void    hashtable_cttl (hashtable *ht, ht_rec *data);
//-----------------------------------------
bool  hashtable_get (hashtable *ht, ht_rec *data)
{
#ifdef _LOCK
  mysem_lock (ht->tb_semid, 0);
#endif // _LOCK
  //-----------------------------------------
  // hash_t he = hash (data->key) % ht->lines_count;
  hashtable_cttl (ht, data);
  hash_t h = hashtable_walk (ht, data, 0);
  //-----------------------------------------
  if ( (h < ht->lines_count) && ht->lines[h].busy
    && (ht->lines[h].data.key == data->key) )
  { *data  = ht->lines[h].data; }
  else { h = ht->lines_count;   }
  //-----------------------------------------
#ifdef _LOCK
  mysem_unlock (ht->tb_semid, 0);
#endif // _LOCK
  //-----------------------------------------
  return (h == ht->lines_count);
}
bool  hashtable_set (hashtable *ht, ht_rec *data)
{
#ifdef _LOCK
  mysem_lock (ht->tb_semid, 0);
#endif // _LOCK
  // hash_t he = hash (data->key) % ht->lines_count;
  //-----------------------------------------
  hashtable_cttl (ht, data);
  hash_t h = hashtable_walk (ht, data, 0);
  if ( (h < ht->lines_count) && (!ht->lines[h].busy
    || (ht->lines[h].data.key == data->key)) )
  { 
    ht->lines[h].data = *data;
    ht->lines[h].busy =  true;
  }
  else h = ht->lines_count;
  //-----------------------------------------
#ifdef _LOCK
  mysem_unlock (ht->tb_semid, 0);
#endif // _LOCK
  //-----------------------------------------
  return (h == ht->lines_count);
}


hash_t  hashtable_walk (hashtable *ht, ht_rec *data, hash_t hee)
{
  hash_t he = hash (data->key) % ht->lines_count;
  //-----------------------------------------
  /* идём по открытой адресации */
  hash_t h = he;
  while ( (h < ht->lines_count) && ht->lines[h].busy )
  {
    if ( data->key == ht->lines[h].data.key )
    { return h; }

    if ( h > hash (ht->lines[h].data.key) % ht->lines_count )
    { ++h; }
    else break;
  }
  //-----------------------------------------
  return h;
}
void    hashtable_cttl  (hashtable *ht, ht_rec *data)
{
  hash_t he = hash (data->key) % ht->lines_count;
  //-----------------------------------------
  
  /* идём по открытой адресации */
  hash_t h = he;
  while ( (h < ht->lines_count) && ht->lines[h].busy )
  {
    if ( h > hash (ht->lines[h].data.key) % ht->lines_count )
    { ++h; }
    else break;
  }
  
  /* сдвигаем ttl */
  hash_t shift = 1U;
  for ( hash_t i = he; i < h; )
  {
    /* если встречаем оконченный ttl */
    if ( !ht->lines[i + shift].busy
        || ttl_completed (ht->lines[i + shift].data.ttl) )
    {
      ht->lines[i] = ht->lines[i + shift];
      ht->lines[i + shift].busy = false;
      ++shift;
    }
    else
    { ++i; (shift > 1U) ? --shift : shift;  }
  }
  //-----------------------------------------
}
//-----------------------------------------
#ifdef _DEBUG
void  hashtable_print_debug (hashtable *ht, FILE *f)
{
  printf ("\n");
  for ( size_t i = 0U; i < ht->lines_count; ++i )
  {
    printf ("%3u %s ttl=% 3.2lf key=%3d val=%3d\n", i,
            (ht->lines[i].busy ? "Busy" : "Free"),
            !ht->lines[i].data.ttl ? 0. :
            difftime (time (NULL), ht->lines[i].data.ttl),
            ht->lines[i].data.key, ht->lines[i].data.val);
  }
}
#endif // _DEBUG
