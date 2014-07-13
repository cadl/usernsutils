#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
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


static char *pid_str = NULL;
static int flags = 0;
static int verbose = 0;


static struct option options[] = {
  {"pid",          required_argument, NULL, 'p'},

  {"utsns",        no_argument,       NULL, CLONE_NEWUTS},
  {"ipcns",        no_argument,       NULL, CLONE_NEWIPC},
  {"userns",       no_argument,       NULL, CLONE_NEWUSER},
  {"netns",        no_argument,       NULL, CLONE_NEWNET},

  {"verbose",      no_argument,       NULL, 'v'},
  {"help",         no_argument,       NULL, 'h'},
  {NULL,           no_argument,       NULL, 0}
};


void show_usage(char const *name) {
  printf("Usage: %s [options] [--] [command]\n", name);
  printf("\n"
	 "  -p, --pid=PID               pid\n"
	 "\n"
	 "  --utsns                     switch UTS namespace\n"
	 "  --ipcns                     switch IPC namespace\n"
	 "  --userns                    switch USER namespace\n"
	 "  --netns                     switch NET namespace\n"
	 "\n"
	 "  -v, --verbose               verbose\n"
	 "  -h, --help                  print help message and exit\n"
	 );
  exit(0);
}



int main(int argc, char *const argv[]) {
  int opt, index;

  while((opt = getopt_long(argc, argv, "+p:hv", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto err;

    case 'p':
      pid_str = optarg;
      break;

    case 'h':
      show_usage(argv[0]);
      break;

    case 'v':
      verbose = 1;
      break;

    default:
      flags |= opt;
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "%s: command expected\n", argv[0]);
    goto err;
  }

  errno = 0;
  char *endptr = NULL;
  long pid = strtol(pid_str, &endptr, 10);

  if (errno || (endptr == pid_str)) {
    fprintf(stderr, "bad pid '%s'\n", pid_str);
    exit(EXIT_FAILURE);
  }

  char pid_dir[PATH_MAX] = {0};
  snprintf(pid_dir, PATH_MAX, "/proc/%ld", pid);

  struct stat buf;

  PERROR(==-1, stat, pid_dir, &buf);

  if (!S_ISDIR(buf.st_mode)) {
    fprintf(stderr, "bad pid '%s'\n", pid_str);
    exit(EXIT_FAILURE);
  }

  static int mask[] = {
    CLONE_NEWUSER,
    CLONE_NEWNS,
    CLONE_NEWUTS,
    CLONE_NEWIPC,
    CLONE_NEWPID,
    CLONE_NEWNET
  };

  static char const* filename[] = {
      "/proc/%s/ns/user",
      "/proc/%s/ns/mnt",
      "/proc/%s/ns/uts",
      "/proc/%s/ns/ipc",
      "/proc/%s/ns/pid",
      "/proc/%s/ns/net",
  };

  int i;

  for(i=0;i<6;i++) {
    if (!(flags & mask[i])) {
      continue;
    }

    char ns_path[PATH_MAX] = {0};
    snprintf(ns_path, PATH_MAX, filename[i], pid_str);

    int fd = -1;
    PERROR(==-1, fd = open, ns_path, O_RDONLY);
    PERROR(==-1, setns, fd, 0);
    close(fd);
  }


  char *const *arg = argv+optind;
  PERROR(==-1, execvp, arg[0], arg);
  exit(EXIT_FAILURE);

err:
  fprintf(stderr, "Try '%s --help'\n", argv[0]);
  exit(EXIT_FAILURE);
}
