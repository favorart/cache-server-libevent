#include "stdafx.h"
#include "cache_error.h"
#include "cache_hash.h"
#include "cache.h"

void  cache_handling (hashtable *ht, struct evbuffer *buf_in, struct evbuffer *buf_out);
//-----------------------------------------
void  cache_ac_err_cb (evutil_socket_t fd, short ev, void *arg)
{
  // struct event_base *base = (struct event_base*) arg;
  struct client *Client = (struct client*) arg;

  int err = EVUTIL_SOCKET_ERROR ();
  fprintf (stderr, "Got an error %d (%s) on the listener. "
           "Shutting down.\n", err, evutil_socket_error_to_string (err));

  event_base_loopexit (Client->base, NULL);
}
void  cache_accept_cb (evutil_socket_t fd, short ev, void *arg)
{
  //----------------------------------------------------------------------
  int  SlaveSocket = accept (fd, 0, 0);
  if ( SlaveSocket == -1 )
  { fprintf (stderr, "%s\n", strerror (errno));
    return;
  }
  //----------------------------------------------------------------------
  set_nonblock (SlaveSocket);
  //----------------------------------------------------------------------
  wc_t r = (rand () % server_conf.workers);
  child_worker_send (&server_conf.child_workers[r], CHWMSG_TASK, SlaveSocket);
  //----------------------------------------------------------------------
#ifdef _DEBUG
  printf ("connection accepted\n");
#endif
  //----------------------------------------------------------------------
}
//-----------------------------------------
void  cache_connect_cb (evutil_socket_t fd, short ev, void *arg)
{
  struct client *ClientParams = (struct client*) arg;
  //----------------------------------------------------------------------
  /* Making the new client */
  struct client *Client = (struct client*) calloc (1U, sizeof (*Client));
  if ( !Client )
  { fprintf (stderr, "%s\n", strerror (errno));
    return;
  }

  Client->base = ClientParams->base;
  Client->ht   = ClientParams->ht;
  //----------------------------------------------------------------------
  evutil_socket_t  SlaveSocket = -1;
  chwmsg_enum msg;
  child_worker_recv (server_conf.myself, &msg, &SlaveSocket);

  if ( msg == CHWMSG_TERM )
  { event_loopexit (0); }
  else
  {
    //----------------------------------------------------------------------
    /* Create new bufferized event, linked with client's socket */
    Client->b_ev = bufferevent_socket_new (ClientParams->base, SlaveSocket, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb (Client->b_ev, cache_read_cb, NULL, cache_error_cb, Client);
    /* Ready to get data */
    bufferevent_enable (Client->b_ev, EV_READ | EV_WRITE | EV_PERSIST);
    //----------------------------------------------------------------------
    bufferevent_setwatermark (Client->b_ev, EV_WRITE, SRV_BUF_LOWMARK, 0);
    //----------------------------------------------------------------------
#ifdef _DEBUG
    printf ("connection to server\n");
#endif
  }
}
//-----------------------------------------
void  cache_error_cb (struct bufferevent *b_ev, short events, void *arg)
{
  /*   Структура event превращается в триггер  таймаута с помощью функции timeout_set().
   *   Само собой триггер необходимо "взвести" - для этого служит функция timeout_add().
   *   Чтобы "разрядить" триггер и отменить событие таймаута, необходимо воспользоваться
   *   функцией timeout_del().
   */  
  /*
   *   В заголовочном файле <event2/bufferevent.h> определены флаги,
   *   позволяющие получить информацию о причинах возникновения особой ситуации:
   *   1. Событие произошло во время чтения (BEV_EVENT_READING 0x01) или записи (BEV_EVENT_WRITING 0x02).
   *   2. Обнаружен признак конца файла (BEV_EVENT_EOF 0x10), произошёл критический сбой (BEV_EVENT_ERROR 0x20),
   *      истёк заданный интервал (BEV_EVENT_TIMEOUT 0x40), соединение было установлено (BEV_EVENT_CONNECTED 0x80).
   */
       if ( events & BEV_EVENT_CONNECTED )
  {
#ifdef _DEBUG
    printf ("connection established\n");
#endif
  }
  else if ( events & BEV_EVENT_EOF       )
  {
    struct client   *Client = (struct client*) arg;
    
#ifdef _DEBUG
    printf ("got a close. length = %u\n", evbuffer_get_length (bufferevent_get_input (Client->b_ev)) );
#endif // _DEBUG

    bufferevent_free (Client->b_ev);
    free (Client);

#ifdef _DEBUG
    printf ("connection closed\n");
#endif // _DEBUG

  }
  else if ( events & BEV_EVENT_ERROR     )
  {
    struct client *Client = (struct client*) arg;

    fprintf (stderr, "Error from bufferevent: '%s'\n",
             evutil_socket_error_to_string (EVUTIL_SOCKET_ERROR ()) );
    
    bufferevent_free (Client->b_ev);
    free (Client);

#ifdef _DEBUG
    printf ("connection closed");
#endif // _DEBUG
  }
}
void  cache_read_cb  (struct bufferevent *b_ev, void *arg)
{

#ifdef _DEBUG
  printf ("data reseived\n");
#endif // _DEBUG

  /* This callback is invoked when there is data to read on b_ev_read */
  struct client  *Client = (struct client*) arg;
  
  struct evbuffer *buf_in  = bufferevent_get_input  (b_ev);
  struct evbuffer *buf_out = bufferevent_get_output (b_ev);

  /* Copy all the data from the input buffer to the output buffer. */
  // evbuffer_remove_buffer (buf_in, buf_out, evbuffer_get_length (buf_in));

  cache_handling (Client->ht, buf_in, buf_out);
  
#ifdef _DEBUG
  printf ("response ready\n");
#endif // _DEBUG
}
//-----------------------------------------
/*  Протокол:  get <key> || set <TTL> <key> <val>
 *  Ответ:      ok <key> <val> ИЛИ error <err_text>
 *  
 *  key - no spaces,  val and key - no '\n'
 */
void  cache_handling (hashtable *ht, struct evbuffer *buf_in, struct evbuffer *buf_out)
{
  const char * const  err_prefix = "cache handling";
  
  const char * const  cmd_get = "get";
  const char * const  cmd_set = "set";
  //-----------------------------------------------------------------
  bool   answer_positive = true;
  const char *err_answer = "";

  //-----------------------------------------------------------------
  char   *cmd = NULL;
  size_t  cmd_length = 0U;

  while ( (cmd = evbuffer_readln (buf_in, &cmd_length, EVBUFFER_EOL_ANY)) )
  {
    // size_t cmds_length = evbuffer_remove (buffer, cmds, BUF_SIZE - 2);
    // cmds[cmds_length] = '\0';

    char*   cmd_cur_char = cmd;
    size_t  n = 0;
    //-----------------------------------------------------------------
    char cmd_head[3];
    /* пробуем считать команду */
    if ( 1 != sscanf (cmd_cur_char, "%3s%n", cmd_head, &n) )
    { continue;
      // goto ERR_REQ;
    }
    else { cmd_cur_char += n; }

    ht_rec hd = { 0 };
         if ( !strcmp (cmd_head, cmd_get) )
    {
      if ( 1 != sscanf (cmd_cur_char, "%d%n", &hd.key, &n) )
      { continue;
        // goto ERR_REQ;
      }
      else { cmd_cur_char += n; }

      if ( hashtable_get (ht, &hd) )
      {
        answer_positive = false;
        err_answer = "no such key in table";
      }
      
    }
    else if ( !strcmp (cmd_head, cmd_set) )
    {
      int32_t ttl = 0;
      /* пробуем считать параметры */
      if ( 3 != sscanf (cmd_cur_char, "%d%d%d%n", &ttl, &hd.key, &hd.val, &n) )
      { continue;
        // goto ERR_REQ;
      }
      else { cmd_cur_char += n; }

      hd.ttl = ttl_converted (ttl);
      if ( hashtable_set (ht, &hd) )
      {
        answer_positive = false;
        err_answer = "cannot insert the key";
      }
    }

#ifdef _DEBUG
    printf ("key=% 4d ttl=% 3.2lf answ=%d\n", hd.key,
            !hd.ttl ? 0. : difftime (time (NULL), hd.ttl),
            answer_positive);
#endif

    if ( answer_positive && buf_out )
    { evbuffer_add_printf (buf_out, "ok %d %d\n", hd.key, hd.val); }
    // else
    // { evbuffer_add_printf (buf_out, "error %s\n", err_answer); }

    free (cmd);
    continue;

REQ_ERR:;
    my_errno = SRV_ERR_RCMMN;
    fprintf (stderr, "%s%s", err_prefix, strmyerror ());
    free (cmd);
  }
  //-----------------------------------------------------------------
HNDL_FREE:;
  free (cmd);
  evbuffer_drain (buf_in, -1);

  return;
}
//-----------------------------------------
int main ()
{
  hashtable ht = { 0 };
  hashtable_init (&ht, 20, 0);

  struct evbuffer *buf = evbuffer_new ();

  srand ((unsigned int) clock ());
  for ( int i = 0; i < 15; ++i )
  {
    int32_t ttl = rand () % 5 + 2,
            key = rand () % 20,
            val = 1;

    evbuffer_add_printf (buf, "set %d %d %d\n", ttl, key, val);
  }
  cache_handling  (&ht, buf, NULL);

  hashtable_print_debug (&ht, stdout);

  sleep (4);

  for ( int i = 0; i < 8; ++i )
  {
    int32_t key = rand () % 5 + 2;
    evbuffer_add_printf (buf, "get %d\n", key);
  }
  cache_handling  (&ht, buf, NULL);

  hashtable_print_debug (&ht, stdout);

  hashtable_free (&ht);
  evbuffer_free  (buf);

  return 0;
}
