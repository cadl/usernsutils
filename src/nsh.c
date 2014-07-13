#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pty.h>



#define PERROR(condition, func, ...)		\
  if( (func(__VA_ARGS__))condition) {		\
    perror(#func"("#__VA_ARGS__")");		\
    exit(EXIT_FAILURE);				\
  }						\


#define VERBOSE(...)				\
  if (verbose) {				\
    fprintf(stderr, __VA_ARGS__);		\
  }						\


#define RETRY_ON_INTR(func, ...)       		\
  while ( (func(__VA_ARGS__)) == -1) {		\
    if (errno != EINTR) {			\
      break;					\
    }						\
  }						\


static int verbose = 0;
static char *socket_path = NULL;



static struct option options[] = {
  {"connect",      required_argument, NULL, 'c'},

  {"verbose",      no_argument,       NULL, 'v'},
  {"help",         no_argument,       NULL, 'h'},

  {NULL,           no_argument,       NULL, 0}
};


void show_usage(char const *name) {
  printf("Usage: %s [options]\n", name);
  printf("\n"
	 "  -c, --connect=PATH         path to unix domain socket\n"
	 "\n"
	 "  -v, --verbose              verbose\n"
	 "  -h, --help                 print help message and exit\n"
	 );
  exit(0);
}


void makeraw(struct termios *mode);


int main(int argc, char *const argv[]) {
  int opt, index;

  while((opt = getopt_long(argc, argv, "+c:hv", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto err;

    case 'h':
      show_usage(argv[0]);

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

  if (!socket_path) {
    fprintf(stderr, "socket path expected\n");
    goto err;
  }

  int fd = -1;
  PERROR(==-1, fd = socket, AF_UNIX, SOCK_STREAM, 0);

  struct sockaddr_un addr = {.sun_family = AF_UNIX};

  if(strlen(socket_path) > sizeof(addr.sun_path) - 1) {
    fprintf(stderr, "socket path too long\n");
    exit(EXIT_FAILURE);
  }

  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);

  PERROR(==-1, connect, fd, &addr, sizeof(addr));

  int master, slave;
  PERROR(==-1, openpty, &master, &slave, NULL, NULL, NULL);

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

  int fds[1] = {slave};
  memcpy((int *) CMSG_DATA(cmsg), fds, controllen);
  PERROR(==-1, sendmsg, fd, &msg, 0);
  close(slave);
  close(fd);

  void setwinsz() {
    struct winsize win;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &win);
    ioctl(master, TIOCSWINSZ, &win);
  }

  setwinsz();

  struct termios old_mode;
  PERROR(==-1, tcgetattr, STDIN_FILENO, &old_mode);

  struct termios mode;
  PERROR(==-1, tcgetattr, STDIN_FILENO, &mode);
  makeraw(&mode);
  PERROR(==-1, tcsetattr, STDIN_FILENO, TCSAFLUSH, &mode);

  void sigwinch_handler(int signum) {
    setwinsz();
  }

  signal(SIGWINCH, sigwinch_handler);

  void resettty() {
    PERROR(==-1, tcsetattr, STDIN_FILENO, TCSAFLUSH, &old_mode);
    signal(SIGWINCH, SIG_DFL);
  }
  atexit(resettty);

  for(;;) {
    fd_set rfds;
    struct timeval tv;
    char buffer[1024];
    int nfd;
    ssize_t read_count;
    ssize_t write_count;

    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    FD_SET(master, &rfds);
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    RETRY_ON_INTR(nfd = select, master+1, &rfds, NULL, NULL, &tv);
    if (nfd == -1) {
      perror("select");
      exit(EXIT_FAILURE);
    }

    if (FD_ISSET(STDIN_FILENO, &rfds)) {
      RETRY_ON_INTR(read_count = read, STDIN_FILENO, buffer, 1024);

      if (read_count == -1) {
        close(master);

        if (errno != EIO) {
          perror("read from stdin");
        }
        break;
      }

      while(read_count) {
        RETRY_ON_INTR(write_count = write, master, buffer, read_count);

        if (write_count == -1) {
          if (errno != EIO) {
            perror("write to master");
          }
          break;
        }

        read_count -= write_count;
      }
    }

    if (FD_ISSET(master, &rfds)) {
      RETRY_ON_INTR(read_count = read, master, buffer, 1024);

      if (read_count == -1) {
        if (errno != EIO) {
          perror("read from master");
        }
        break;
      }

      while(read_count) {
        RETRY_ON_INTR(write_count = write, STDOUT_FILENO, buffer, read_count);

        if (write_count == -1) {
          close(master);

          if (errno != EIO) {
            perror("write to stdout");
          }
          break;
        }

        read_count -= write_count;
      }
    }
  }

  return 0;
err:
  fprintf(stderr, "Try '%s --help'\n", argv[0]);
  exit(EXIT_FAILURE);
}


void makeraw(struct termios *mode) {
  mode->c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  mode->c_oflag &= ~OPOST;
  mode->c_cflag &= ~(CSIZE | PARENB);
  mode->c_cflag |= CS8;
  mode->c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  mode->c_cc[VMIN] = 1;
  mode->c_cc[VTIME] = 0;
}
