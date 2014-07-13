#!/usr/bin/env python3

import os
import sys
import socket
import signal
import array
import struct


def handle_connection(conn):
    while True:
        data = conn.recv(1)
        if not data:
            break

        sock_type, = struct.unpack("B", data)

        if sock_type == socket.SOCK_STREAM:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        elif sock_type == socket.SOCK_DGRAM:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        else:
            exit(1)

        fds = [sock.fileno()]
        conn.sendmsg(
            [b'\x00'],
            [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", fds))])
        sock.close()

    conn.close()
    exit(0)


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
