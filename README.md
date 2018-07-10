# Symmetric Routing Daemon

The symrouted program uses routing policies to allow traffic from different
local IP addresses to be forwarded to a different next hop (gateway).

This is an experimental solution to allow hosts with multiple network interfaces to send
replies on the same interface that received the request, by preferring to use device-local
routing entries before any other entry in the main routing table.

## Installation

The developers platform is CentOS 7.5.

1. Install neccessary build utilities. Something like (YMMV):
```shell
$ sudo yum install gcc rpm-build libnl3-devel
```

2. Build RPM using supplied script:
```shell
$ ./buildrpm.sh 
```

3. Install RPM:
```shell
$ sudo rpm -i ~/rpmbuild/RPMS/x86_64/symrouted-0.1.1-1.el7.x86_64.rpm
```

4. Activate symrouted via systemd:
```shell
$ sudo systemctl start symrouted
```

5. Enable symrouted to start on system boot:
```shell
$ sudo systemctl enable symrouted
```

## Behind the Scenes

symrouted works by replicating the main routing table into multiple distinct routing tables, one per
network device, and using policy routing rules to steer traffic into these tables based on the
source/local IPv4/IPv6 address. The effect is that any routing entry tied directly to the network
device will be preferred over other entries in the main routing table.

Consider a routing table with two interfaces, each with a default gateway:
```shell
$ ip route
default via 10.0.0.1 dev em1 proto static metric 105
default via 192.168.0.1 dev em2 proto static metric 106
10.0.0.0/16 dev em1 proto kernel scope link src 10.0.0.2 metric 105
192.168.0.0/24 dev em2 proto kernel scope link src 192.168.0.2 metric 106
```

This is replicated into two tables, with one steering rule for each address on the interfaces:
```shell
$ ip rule
0:	from all lookup local 
32764:	from 10.0.0.2 lookup 1002
32765:	from 192.168.0.2 lookup 1003
32766:	from all lookup main 
32767:	from all lookup default
$ ip route show table 1002
default via 10.0.0.1 dev em1 proto static metric 105
10.0.0.0/16 dev em1 proto kernel scope link src 10.0.0.2 metric 105
$ ip route show table 1003
default via 192.168.0.1 dev em2 proto static metric 106
192.168.0.0/24 dev em2 proto kernel scope link src 192.168.0.2 metric 106
```

One potential caveat of the current implementation is that is assumes ownership of all
table ids above 1000. Please file a bug if you know this to clash with any other program.

Naming suggestions welcome.
