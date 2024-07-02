# Symmetric Routing Daemon

The `symrouted` program mirrors the global routing table into per-interface routing tables. It utilizes policy-based routing to steer packets from an IP address on interface N to the routing table for interface N. Unlike manually configured rules, it doesn't require configuration and works with DHCP-assigned addresses.

Some of the usage scenarios:
- Prevent traffic mixing across Data/Management or Red/Blue networks
- Load balancing a service across two or more interfaces

## Installation

The daemon should work on most Linux platforms, but the tested platforms are CentOS/RHEL 7/8/9 and Ubuntu 24.04.

### On Ubuntu or Debian

1. Build the package
```shell
dpkg-buildpackage -us -uc
```

2. Install the package
```shell
sudo apt install ../symrouted_*.deb
```

3. Verify that the process is running
```shell
systemctl status -n 50 symrouted
```

### On CentOS, Red Hat or other RPM-based distributions:
1. Build the package
```shell
./buildrpm.sh
```

2. Install the package
```shell
sudo rpm -i ~/rpmbuild/RPMS/x86_64/symrouted-1.0-1.el7.x86_64.rpm
```

3. Activate and enable `symrouted`:
```shell
sudo systemctl start symrouted
sudo systemctl enable symrouted
```

4. Verify that the process is running
```shell
systemctl status -n 50 symrouted
```

## Behind the Scenes

Consider a global routing table for two interfaces where each has a default gateway:
```shell
$ ip route
default via 10.0.0.1 dev em1 proto static metric 105
default via 192.168.0.1 dev em2 proto static metric 106
10.0.0.0/16 dev em1 proto kernel scope link src 10.0.0.2 metric 105
192.168.0.0/24 dev em2 proto kernel scope link src 192.168.0.2 metric 106
```

When `symrouted` is running, a rule for each interface address on the system will steer packets into per-interface routing tables:
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

One potential caveat of the current implementation is that it assumes ownership of all table IDs above 1000. Please file a bug if you know this clashes with any other program.
