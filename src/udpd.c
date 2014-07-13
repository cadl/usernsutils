#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>


#define PERROR(condition, func, ...)		\
  if( (func(__VA_ARGS__))condition) {		\
    perror(#func"("#__VA_ARGS__")");		\
    exit(EXIT_FAILURE);				\
  }						\


#define VERBOSE(...)				\
  if (verbose) {				\
    fprintf(stderr, __VA_ARGS__);		\
  }						\


static int verbose = 0;
static char *ip = "127.0.0.1";
static char *port = "3128";
static char *socket_path = NULL;


static struct option options[] = {
  {"ip",           required_argument, NULL, 'a'},
  {"port",         required_argument, NULL, 'p'},
  {"connect",      required_argument, NULL, 'c'},

  {"verbose",      no_argument,       NULL, 'v'},
  {"help",         no_argument,       NULL, 'h'},

  {NULL,           no_argument,       NULL, 0}
};


void show_usage(char const *name) {
  printf("Usage: %s [options] [--] [command]\n", name);
  printf("\n"
         "      --ip=IP                listen IP\n"
	 "  -p, --port=PORT            PORT to listen on\n"
	 "  -c, --connect=PATH         PATH to socketd\n"
	 "\n"
	 "  -v, --verbose              verbose\n"
	 "  -h, --help                 print help message and exit\n"
	 );
  exit(0);
}


int main(int argc, char *const argv[]) {
int opt, index;

  while((opt = getopt_long(argc, argv, "+p:c:hv", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto err;

    case 'h':
      show_usage(argv[0]);
      break;

    case 'a':
      ip = optarg;
      break;

    case 'p':
      port = optarg;
      break;

    case 'c':
      socket_path = optarg;
      break;

    case 'v':
      verbose = 1;
      break;

    default:
      break;
    }
  }


  int listen_fd = -1;
  PERROR(==-1, listen_fd = socket, AF_INET, SOCK_DGRAM, 0);

  struct sockaddr_in listen_addr;
  listen_addr.sin_family = AF_INET;
  PERROR(!=1, inet_pton, AF_INET, ip, &(listen_addr.sin_addr));

  errno = 0;
  listen_addr.sin_port = strtol(port, NULL, 10);
  if (errno) {
    fprintf(stderr, "invalid port number '%s'", port);
    exit(EXIT_FAILURE);
  }

  char optval = 1;

  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, 1);
  setsockopt(listen_fd, SOL_IP, IP_TRANSPARENT, &optval, 1);
  setsockopt(listen_fd, SOL_IP, IP_ORIGDSTADDR, &optval, 1);
  PERROR(==-1, bind, listen_fd, &listen_addr, sizeof(listen_addr));

  int fd = -1;
  PERROR(==-1, fd = socket, AF_UNIX, SOCK_STREAM, 0);

  struct sockaddr_un addr = {.sun_family = AF_UNIX};

  if(strlen(socket_path) > sizeof(addr.sun_path) - 1) {
    fprintf(stderr, "socket path too long\n");
    exit(EXIT_FAILURE);
  }

  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);

  PERROR(==-1, connect, fd, &addr, sizeof(addr));

  char sock_type = SOCK_DGRAM;

  PERROR(==-1, send, fd, &sock_type, 1, 0);

  char control[CMSG_SPACE(sizeof(int))];
  char n = 0;

  struct iovec iov = {.iov_base = &n, .iov_len = 1};
  struct msghdr msg = {
    .msg_name = NULL,
    .msg_namelen = 0,
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = control,
    .msg_controllen = CMSG_LEN(sizeof(int)),
    .msg_flags = 0,
  };

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  *((int *)CMSG_DATA(cmsg)) = -1;

  PERROR(==-1, recvmsg, fd, &msg, 0);

  cmsg = CMSG_FIRSTHDR(&msg);

  if (cmsg==NULL ||
      cmsg->cmsg_level != SOL_SOCKET ||
      cmsg->cmsg_type != SCM_RIGHTS) {
    fprintf(stderr, "cmsg: bad message!\n");
    exit(EXIT_FAILURE);
  }

  int socketd_fd = *((int *)CMSG_DATA(cmsg));
  close(fd);


  int table_size = 8;
  int counter = 0;
  struct sockaddr_in addr_table[8];
  int sendsock_table[8];
  int last_access_table[8];

  int is_sockaddr_equal(struct sockaddr_in addr1, struct sockaddr_in addr2) {
    if ((addr1.sin_family == AF_INET) && (addr2.sin_family == AF_INET)) {
      if (addr1.sin_port == addr2.sin_port) {
        if (addr1.sin_addr.s_addr == addr2.sin_addr.s_addr) {
          return 1;
        }
      }
    }

    return 0;
  }

  int find_entry_by_addr(struct sockaddr_in addr) {
    int i;
    for(i=0;i<table_size;i++) {
      if (is_sockaddr_equal(addr, addr_table[i])) {
        return i;
      }
    }

    return 0;
  }

  int find_least_recent_access_index() {
    int min_index = 0;
    int min_access = last_access_table[0];

    int i;
    for(i=1;i<table_size;i++) {
      if (last_access_table[i] < min_access) {
        min_access = last_access_table[i];
        min_index = i;
      }
    }

    return min_index;
  }

  int find_entry_by_sendfd(int fd) {
    int i;
    for(i=0;i<table_size;i++) {
      if(sendsock_table[i] == fd) {
        return i;
      }
    }

    return -1;
  }

  listen(listen_fd, SO_MAXCONN);


/*  = { */
  /*   .sin_family = AF_INET, */
  /*   .sin_port =  */
  /* }; */




err:
  fprintf(stderr, "Try '%s --help'\n", argv[0]);
  exit(EXIT_FAILURE);
}
