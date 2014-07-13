#!/usr/bin/env python3

import sys
import socket
import struct
import array
import select


SO_ORIGINAL_DST = 80
IP_TRANSPARENT = 19
IP_ORIGDSTADDR = 20


def recv(s):
    msg, ancdata, flags, addr = s.recvmsg(4096, socket.CMSG_LEN(16))
    orig_dest = None

    for cmsg_level, cmsg_type, cmsg_data in ancdata:
        if cmsg_level == socket.SOL_IP:
            if cmsg_type == IP_ORIGDSTADDR:
                (proto, port, a,b,c,d) = struct.unpack("!HHBBBB", cmsg_data[:8])
                orig_dest = ("%d.%d.%d.%d"%(a,b,c,d), port)

    return msg, addr, orig_dest


def get_sendsock(s):
    s.send(struct.pack("B", socket.SOCK_DGRAM))
    fds = array.array("i")
    _, ancdata, _, _ = s.recvmsg(1, socket.CMSG_LEN(fds.itemsize))
    fds.fromstring(ancdata[0][2])
    return socket.fromfd(fds[0], socket.AF_INET, socket.SOCK_DGRAM)


def get_recvsock(addr):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_IP, IP_TRANSPARENT, 1)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_TTL, 255)
    s.bind(addr)
    return s


class Entry(object):

    def __init__(self, addr, sendsock, last_access):
        self.addr = addr
        self.sendsock = sendsock
        self.last_access = last_access


def main(path):
    table_size = 8
    counter = 0

    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.setsockopt(socket.SOL_IP, IP_TRANSPARENT, 1)
    listener.setsockopt(socket.SOL_IP, IP_ORIGDSTADDR, 1)
    listener.bind(('0.0.0.0', 3128))

    socketd_client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    socketd_client.connect(path)

    table = []

    def find_entry_by_addr(addr):
        for e in table:
            if e.addr == addr:
                return e

    def find_least_recent_access_index():
        min_index = 0
        min_access = table[0].last_access

        for i, e in enumerate(table[1:]):
            if e.last_access < min_access:
                min_access = e.last_access
                min_index = i+1

        return min_index

    def find_entry_by_sendfd(fd):
        for e in table:
            if e.sendsock.fileno() == fd:
                return e


    poll = select.poll()
    poll.register(listener, select.POLLIN)

    while True:
        for fd, event in poll.poll():
            if fd == listener.fileno():
                data, addr, dest = recv(listener)

                entry = find_entry_by_addr(addr)
                if entry is None:
                    if len(table) >= table_size:
                        index = find_least_recent_access_index()
                        old_entry = table.pop(index)
                        print('closing', old_entry.addr)
                        poll.unregister(old_entry.sendsock)
                        old_entry.sendsock.close()

                    entry = Entry(addr, get_sendsock(socketd_client), counter)
                    poll.register(entry.sendsock, select.POLLIN)
                    table.append(entry)

                entry.last_access = counter
                sendsock = entry.sendsock
                counter += 1
                sendsock.sendto(data, dest)
            else:
                entry = find_entry_by_sendfd(fd)
                entry.last_access = counter
                counter += 1

                data, addr = entry.sendsock.recvfrom(4096)
                recvsock = get_recvsock(addr)
                recvsock.sendto(data, entry.addr)
                recvsock.close()


if __name__ == '__main__':
    main(sys.argv[1])
