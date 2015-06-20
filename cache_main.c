#include "stdafx.h"
#include "cache_error.h"
#include "cache_hash.h"
#include "cache.h"

//-----------------------------------------
int main2 (int argc, char **argv)
{
  int result = 0;
  //-------------------------------------------
  /* Считывание конфигурации сервера с параметров командной строки */
  // if ( parse_console_parameters (argc, argv, &server_conf) )
  // {
  //   fprintf (stderr, "%s\n", strerror (errno));
  //   result = 1;
  //   goto MARK_FREE;
  // }
  struct server_config  server_conf = { 0 };
  server_config_init  (&server_conf, NULL, NULL, NULL);
  server_config_print (&server_conf, stdout);

  hashtable ht = {0};
  hashtable_init (&ht, CACHELINES, CACHE_SIZE);
  
  /* Создание слушающего сокета */
  int  MasterSocket = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if ( MasterSocket == -1 )
  {
    fprintf (stderr, "%s\n", strerror (errno));
    result = 1;
    goto MARK_FREE;
  }
  
  int  so_reuseaddr = 1; /* Манипулируем флагами, установленными на сокете */
  if ( setsockopt (MasterSocket, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof (so_reuseaddr)) )
  {
    fprintf (stderr, "%s\n", strerror (errno));
    result = 1;
    goto MARK_FREE;
  }

  struct sockaddr_in  SockAddr = {0};
  SockAddr.sin_family      = AF_INET;
  SockAddr.sin_port        = htons     (server_conf.port);
  SockAddr.sin_addr.s_addr = inet_addr (server_conf.ip);
  
  int  Result = bind (MasterSocket, (struct sockaddr*) &SockAddr, sizeof (SockAddr));
  if ( Result == -1 )
  {
    fprintf (stderr, "%s\n", strerror (errno)); 
    result = 1;
    goto MARK_FREE;
  }
  
  set_nonblock (MasterSocket);
  
  Result = listen (MasterSocket, SOMAXCONN);
  if ( Result == -1 )
  {
    fprintf (stderr, "%s\n", strerror (errno)); 
    result = 1;
    goto MARK_FREE;
  }

  //-------------------------------------------
  /*  Cервер, который обслуживает одновременно несколько клиентов.
   *  Понадобится слушающий сокет, который может асинхронно принимать
   *  подключения, обработчик подключения и логика работы с клиентом.
   */
  
  /* Initialize new event_base to do not overlay the global context */
  struct event_base  *base = event_base_new ();
  if ( !base )
  {
    fprintf (stderr, "%s\n", strerror (errno));
    result = 1;
    goto MARK_FREE;
  }
  //-------------------------------------------
  struct client Client = { 0 };
  Client.base = base;
  Client.ht   = &ht;
  //-------------------------------------------
  /* Init events */
  struct event  ev;

  /* Master Process */
  if ( !server_conf.myself )
  {
    /* Create a new event, that react to read drom server's socket - event of connecting the new client */
    /* Set connection callback (on_connect()) to read event on server socket */
    event_set (&ev, MasterSocket, EV_READ | EV_PERSIST, cache_accept_cb, &Client);
    /* Attach ev_connect event to my event base */
    event_base_set (base, &ev);
    /* Add server event without timeout */
    event_add (&ev, NULL);
  }
  else
  {
    event_set (&ev, server_conf.myself->fd, EV_READ | EV_PERSIST, cache_connect_cb, &Client);
    event_base_set (base, &ev);
    event_add (&ev, NULL);
  }
  /* Dispatch events */
  event_base_loop (base, 0);
  //-------------------------------------------

MARK_FREE:
  //-------------------------------------------
  event_base_free (base);

  shutdown (MasterSocket, SHUT_RDWR);
  close    (MasterSocket);
  
  server_config_free (&server_conf);

  hashtable_free (&ht);
  //-------------------------------------------
  return result;
}
