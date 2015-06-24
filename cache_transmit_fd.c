﻿#include "stdafx.h"
#include "cache_error.h"
#include "cache.h"


// struct msghdr
// {
//    /* не используются для потокавых буферов и должны быть равны нулю */
//    void           *msg_name;       /* дополнительный адрес */
//    unsigned int    msg_namelen;    /* размер msg_name */
//
//    /* описывают набор буферов, которые отправляют или принимают, чтение вразброс и сборная запись */
//    struct iovec   *msg_iov;        /* массив для чтения вразброс/сборной записи*/
//    unsigned int    msg_iovlen;     /* количество элементов в msg_iov */
//
//    /* предоставляют возможность передачи файлового дескриптора */
//    void           *msg_control;    /* вспомогательные данные */
//    unsigned int    msg_controllen; /* длина буфера вспомогательных данных */
//    /* msg_control - указывает на массив заголовков управляющих сообщений */
//
//    /* в настоящее время не используется и должен быть равен нулю */
//    int             msg_flags;      /* флаги на получаемом сообщении */
// };
//
//  #include <sys/socket.h>
//  struct cmsghdr
//  {
//    unsigned int cmsg_len; /* длина управляющего сообщения */
//    int cmsg_level;        /* SOL_SOCKET */
//    int cmsg_type;         /* SCM_RIGHTS */
//    int cmsg_data[0];      /* здесь должен быть файловый дескриптор */
//    /* Это расширение gcc, которое позволяет приложению копировать данные в конец структуры */
//  };

//-----------------------------------------
/* The function writes some data and an optional file descriptor */
size_t  sock_fd_write (int sock, void *buf, size_t buf_len, int  fd)
{
  struct cmsghdr *cmsg;
  struct  msghdr   msg;
  struct   iovec   iov;
  union
  { struct cmsghdr cmsghdr;
    char           control[CMSG_SPACE (sizeof (int))];
  } cmsgu;
  //-----------------------
  
  iov.iov_base = buf;
  iov.iov_len  = buf_len;

  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  //-----------------------
  if (fd == -1)
  { perror ("fd not passed");
    return 0U;
  }
  
  msg.msg_control    = cmsgu.control;
  msg.msg_controllen = sizeof (cmsgu.control);

  cmsg             = CMSG_FIRSTHDR (&msg);
  cmsg->cmsg_len   = CMSG_LEN (sizeof (int));
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type  = SCM_RIGHTS;

  *((int *) CMSG_DATA (cmsg)) = fd;
  
  //-----------------------
  size_t    size;
  if ( 0 > (size = sendmsg (sock, &msg, 0)) )
  { perror ("sendmsg"); }
  //-----------------------
  return size;
}
/* And here's the matching receiver function */
size_t  sock_fd_read  (int sock, void *buf, size_t buf_len, int *fd)
{
  size_t  size;
  //-----------------------
  if ( fd )
  {
    struct cmsghdr  *cmsg;
    struct  msghdr    msg;
    struct   iovec    iov;
    union
    { struct cmsghdr  cmsghdr;
      char            control[CMSG_SPACE (sizeof (int))];
    } cmsgu;
    
    //-----------------------  
    iov.iov_base = buf;
    iov.iov_len  = buf_len;

    msg.msg_name    = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov     = &iov;
    msg.msg_iovlen  = 1;

    msg.msg_control = cmsgu.control;
    msg.msg_controllen = sizeof(cmsgu.control);
    
    //-----------------------
    if ( 0 > (size = recvmsg (sock, &msg, 0)) )
    { perror ("recvmsg");
      exit(EXIT_FAILURE);
    }
    //-----------------------
    cmsg = CMSG_FIRSTHDR (&msg);
    if ( cmsg && cmsg->cmsg_len == CMSG_LEN (sizeof (int)) )
    {
      if ( cmsg->cmsg_level != SOL_SOCKET )
      { fprintf (stderr, "invalid cmsg_level %d\n", cmsg->cmsg_level);
        exit (EXIT_FAILURE);
      }
      //-----------------------
      if (cmsg->cmsg_type != SCM_RIGHTS)
      { fprintf (stderr, "invalid cmsg_type %d\n", cmsg->cmsg_type);
        exit (EXIT_FAILURE);
      }
      //-----------------------
      *fd = *((int *) CMSG_DATA(cmsg));
    }
    else (*fd = -1);
  }
  else
  {
    if ( 0 > (size = read (sock, buf, buf_len)) )
    { perror ("read");
      exit (EXIT_FAILURE);
    }
  }
  //-----------------------
  return size;
}
//-----------------------------------------
const char * const  chwmsg_task = "task";
const char * const  chwmsg_term = "term";

/* Create a socketpair and fork */
int   child_worker_init (struct child_worker *chw)
{
  fd_t sv[2];
  //-----------------------
  if ( socketpair (AF_LOCAL, SOCK_STREAM, 0, sv) < 0 )
  { perror ("socketpair");
    exit (EXIT_FAILURE);
  }
  //-----------------------
  pid_t pid = fork ();
  if ( !pid )
  {
    close (sv[0]); // child (sv[1]);
    chw->fd  = sv[1];
    chw->pid = getpid ();
  }
  else if ( pid < 0 )
  { perror ("fork");
    exit (EXIT_FAILURE);
  }
  else
  {
    close (sv[1]); // parent (sv[0]);
    chw->fd  = sv[0];
    chw->pid = pid;
  }
  //-----------------------
  return pid;
}
void  child_worker_free (struct child_worker *chw)
{
  close (chw->fd);
}
/* Pass a fd from a parent to a child */
int  child_worker_recv (struct child_worker *chw, chwmsg_enum *msg, fd_t *fd)
{
  char msg_char[CHWMSG_MAXLEN] = { 0 };
  //-----------------------
  /* Reading without accepting a file descriptor */
  size_t size = sock_fd_read (chw->fd, msg_char, sizeof (msg_char), fd);
  if ( size <= 0 )
    return -1;

  if ( !strcmp (msg_char, chwmsg_term) )
  { *msg = CHWMSG_TERM; }
  else if ( !strcmp (msg_char, chwmsg_task) )
  { *msg = CHWMSG_TASK; }
  else
  {
    my_errno = SRV_ERR_FDTRS;
    fprintf (stderr, "Incorrect command: %s", strmyerror ());
  }
  //-----------------------
  return size;
}
int  child_worker_send (struct child_worker *chw, chwmsg_enum  msg, fd_t  fd)
{
  //-----------------------
  const char *msg_char;
  switch ( msg )
  {
    default: return 1;
    case CHWMSG_TASK: msg_char = chwmsg_task; break;
    case CHWMSG_TERM: msg_char = chwmsg_term; break;
  }
  //-----------------------
  size_t size = sock_fd_write (chw->fd, msg_char, strlen (msg_char), fd);
  //-----------------------
  return size;
}

/*  Summary:
*  -  read and recvmsg don't merge data across a file descriptor message boundary.
*  -  failing to accept an fd in the receiver results in the fd being closed by the kernel.
*  -  a fd is not passed when no data are included in the write.
*/
//-----------------------------------------
