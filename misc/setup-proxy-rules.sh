#!/usr/bin/env bash

ip link set lo up
ip link add veth0 type veth peer name veth1
ip address add 10.0.0.1 dev veth0
ip link set veth0 up
route add default dev veth0

iptables -F

# copied from sshuttle
iptables -t nat -F
iptables -t nat -N proxy
iptables -t nat -F proxy
iptables -t nat -A OUTPUT -j proxy
iptables -t nat -A PREROUTING -j proxy

iptables -t nat -A proxy -d 127.0.0.0/8 -p tcp -j RETURN
iptables -t nat -A proxy -p tcp -j REDIRECT --to-ports 3128
#

iptables -t mangle -F
iptables -t mangle -A PREROUTING ! -d 127.0.0.0/8 -p udp -m ttl ! --ttl-eq 255 -j TPROXY --on-port 3128 --on-ip 0.0.0.0 --tproxy-mark 1
iptables -t mangle -A OUTPUT -p udp -j MARK --set-xmark 1

ip rule add fwmark 1 lookup 100
ip route add local 0.0.0.0/0 dev lo table 100
