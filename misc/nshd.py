#!/usr/bin/env python3

import os
import sys
import socket
import fcntl
import termios
import array
import signal

STDIN_FILENO = 0
STDOUT_FILENO = 1
STDERR_FILENO = 2



def handle_connection(conn):
    fds = array.array("i")
    _, ancdata, _, _ = conn.recvmsg(1, socket.CMSG_LEN(fds.itemsize))
    fds.fromstring(ancdata[0][2])
    conn.close()

    slave = fds[0]

    try:
        fd = os.open("/dev/tty", os.O_RDWR | os.O_NOCTTY)
    except OSError:
        pass
    else:
        try:
            fcntl.ioctl(fd, termios.TIOCNOTTY, '')
        except:
            pass
        os.close(fd)

    os.setsid()

    fcntl.ioctl(slave, termios.TIOCSCTTY, '')

    pid = os.fork()

    if pid == 0:
        os.dup2(slave, STDIN_FILENO)
        os.dup2(slave, STDOUT_FILENO)
        os.dup2(slave, STDERR_FILENO)
        os.close(slave)
        os.execv("/bin/bash", ["/bin/bash"])
    else:
        os.close(slave)

        while True:
            _, status = os.waitpid(pid, 0)
            if not os.WIFSTOPPED(status):
                break


def main(path):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind(path)

    def sigint(signum, frame):
        os.unlink(path)

    signal.signal(signal.SIGINT, sigint)
    signal.signal(signal.SIGCHLD, signal.SIG_IGN)

    s.listen(socket.SOMAXCONN)

    while True:
        conn, _ = s.accept()
        pid = os.fork()

        if pid == 0:
            signal.signal(signal.SIGINT, signal.SIG_DFL)
            signal.signal(signal.SIGCHLD, signal.SIG_DFL)

            s.close()
            handle_connection(conn)
            exit(1)

        conn.close()


if __name__ == '__main__':
    main(sys.argv[1])
