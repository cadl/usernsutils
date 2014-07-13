#!/usr/bin/env python3

import os
import sys
import socket
import struct
import array
import select
import signal


SO_ORIGINAL_DST = 80
IP_TRANSPARENT = 19
IP_ORIGDSTADDR = 20



def get_sock(s):
    s.send(struct.pack("B", socket.SOCK_STREAM))
    fds = array.array("i")
    _, ancdata, _, _ = s.recvmsg(1, socket.CMSG_LEN(fds.itemsize))
    fds.fromstring(ancdata[0][2])
    return socket.fromfd(fds[0], socket.AF_INET, socket.SOCK_STREAM)


def make_mask(wait_read, wait_write):
    return (select.POLLIN if wait_read else 0) | (select.POLLOUT if wait_write else 0)


def handle_connection(src_addr, in_sock, out_sock):
    raw_addr = in_sock.getsockopt(socket.SOL_IP, SO_ORIGINAL_DST, 16)
    (proto, port, a,b,c,d) = struct.unpack("!HHBBBB", raw_addr[:8])
    dst_addr = ("%d.%d.%d.%d"%(a,b,c,d), port)

    poll = select.poll()

    out_sock.connect(dst_addr)
    in_sock.setblocking(False)
    out_sock.setblocking(False)

    in_sock_wait_read = False
    out_sock_wait_read = False
    in_sock_wait_write = True
    out_sock_wait_write = True

    poll.register(in_sock, make_mask(in_sock_wait_read, in_sock_wait_write))
    poll.register(out_sock, make_mask(out_sock_wait_read, out_sock_wait_write))

    while True:
        for fd, event in poll.poll():
            if event == select.POLLOUT:
                if fd == in_sock.fileno():
                    in_sock_wait_write = False
                    out_sock_wait_read = True
                elif fd == out_sock.fileno():
                    out_sock_wait_write = False
                    in_sock_wait_read = True
                else:
                    assert False
            elif event == select.POLLIN:
                if fd == in_sock.fileno():
                    s1 = in_sock
                    s2 = out_sock
                    in_sock_wait_read = False
                    out_sock_wait_write = True
                elif fd == out_sock.fileno():
                    s1 = out_sock
                    s2 = in_sock
                    out_sock_wait_read = False
                    in_sock_wait_write = True
                else:
                    assert False

                data = s1.recv(2048)
                if not data:
                    break
                s2.send(data)

            else:
                print(event)
                assert False
        else:
            poll.modify(in_sock, make_mask(in_sock_wait_read, in_sock_wait_write))
            poll.modify(out_sock, make_mask(out_sock_wait_read, out_sock_wait_write))
            continue

        break

    poll.unregister(in_sock)
    poll.unregister(out_sock)
    in_sock.close()
    out_sock.close()
    exit(0)



def main(path):
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(('0.0.0.0',3128))

    socketd_client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    socketd_client.connect(path)

    signal.signal(signal.SIGCHLD, signal.SIG_IGN)
    listener.listen(socket.SOMAXCONN)

    while True:
        conn, addr = listener.accept()
        sock = get_sock(socketd_client)

        pid = os.fork()

        if pid == 0:
            signal.signal(signal.SIGCHLD, signal.SIG_DFL)
            listener.close()
            socketd_client.close()
            handle_connection(addr, conn, sock)
            exit(1)

        conn.close()
        sock.close()



if __name__ == '__main__':
    main(sys.argv[1])
