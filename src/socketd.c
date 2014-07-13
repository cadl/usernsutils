#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
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
static char *socket_path = NULL;


static struct option options[] = {
  {"listen",       required_argument, NULL, 'l'},

  {"verbose",      no_argument,       NULL, 'v'},
  {"help",         no_argument,       NULL, 'h'},

  {NULL,           no_argument,       NULL, 0}
};


void show_usage(char const *name) {
  printf("Usage: %s [options] [--] [command]\n", name);
  printf("\n"
	 "  -l, --listen=PATH          PATH to unix domain socket\n"
	 "\n"
	 "  -v, --verbose              verbose\n"
	 "  -h, --help                 print help message and exit\n"
	 );
  exit(0);
}


void handle_connection(int fd);
void cleanup();
void sigint_handler(int signum);


int main(int argc, char *const argv[]) {
  int opt, index;

  while((opt = getopt_long(argc, argv, "+l:hv", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto err;

    case 'h':
      show_usage(argv[0]);
      break;

    case 'l':
      socket_path = optarg;
      break;

    case 'v':
      verbose = 1;
      break;

    default:
      break;
    }
  }

  if (!socket_path) {
    fprintf(stderr, "listen path expected\n");
    goto err;
  }

  int listen_fd = -1;

  PERROR(==-1, listen_fd = socket, AF_UNIX, SOCK_STREAM, 0);

  struct sockaddr_un addr = {.sun_family = AF_UNIX};

  if(strlen(socket_path) > sizeof(addr.sun_path) - 1) {
    fprintf(stderr, "socket path too long\n");
    exit(EXIT_FAILURE);
  }

  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
  PERROR(==-1, bind, listen_fd, &addr, sizeof(addr));

  signal(SIGINT, sigint_handler);
  signal(SIGCHLD, SIG_IGN);

  PERROR(==-1, listen, listen_fd, SOMAXCONN);
  VERBOSE("start listening on '%s'\n", socket_path);

  for(;;) {
    int fd = -1;

    struct sockaddr_un peer;
    socklen_t size;
    pid_t pid;

    PERROR(==-1, fd = accept, listen_fd, &peer, &size);
    PERROR(==-1, pid = fork);

    if (pid == 0) {
      close(listen_fd);
      signal(SIGINT, SIG_DFL);
      signal(SIGCHLD, SIG_DFL);
      handle_connection(fd);
      break;
    }

    close(fd);
  }

  exit(EXIT_FAILURE);
err:
  fprintf(stderr, "Try '%s --help'\n", argv[0]);
  exit(EXIT_FAILURE);

}


void cleanup(){
  VERBOSE("unlinking '%s'\n", socket_path);
  unlink(socket_path);
}


void sigint_handler(int signum) {
  cleanup();

  signal(SIGINT, SIG_DFL);
  raise(SIGINT);
}


void handle_connection(int fd) {
  for(;;) {
    char sock_type = -1;
    ssize_t length = recv(fd, &sock_type, 1, 0);

    if (length < 0) {
      continue;
    }

    if (length == 0) {
      break;
    }

    int sockfd = -1;

    if (sock_type == SOCK_STREAM) {
      sockfd = socket(AF_INET, SOCK_STREAM, 0);
    } else if (sock_type == SOCK_DGRAM) {
      sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    } else {
      fprintf(stderr, "invalid socket type %d\n", sock_type);
      exit(EXIT_FAILURE);
    }

    size_t controllen = sizeof(int);
    char control[CMSG_SPACE(sizeof(int))];
    char n = 0;

    struct iovec iov = {.iov_base = &n, .iov_len = 1};
    struct msghdr msg = {
      .msg_name = NULL,
      .msg_namelen = 0,
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = control,
      .msg_controllen = CMSG_LEN(controllen),
      .msg_flags = 0,
    };

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = msg.msg_controllen;

    int fds[1] = {sockfd};
    memcpy((int *) CMSG_DATA(cmsg), fds, controllen);
    PERROR(==-1, sendmsg, fd, &msg, 0);
    close(sockfd);
  }

  close(fd);
  exit(0);
}
