#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <libnl3/netlink/netlink.h>
#include <libnl3/netlink/route/addr.h>
#include <libnl3/netlink/route/rule.h>
#include <libnl3/netlink/route/route.h>
#include <libnl3/netlink/route/nexthop.h>

enum {
    ROUTE_PROTOCOL_KERNEL = 2
};

static void dump_obj(struct nl_object *obj, int action, const char *prefix)
{
    struct nl_dump_params dp = {
        .dp_type = NL_DUMP_LINE,
        .dp_fd = stdout
    };

    if (action == NL_ACT_NEW)
        fputs("  new ", dp.dp_fd);
    else if (action == NL_ACT_DEL)
        fputs("  del ", dp.dp_fd);
    else if (action == NL_ACT_CHANGE)
        fputs("  chg ", dp.dp_fd);

    fputs(prefix, dp.dp_fd);
    fputc(' ', dp.dp_fd);
    nl_object_dump(obj, &dp);
}

static void route_flush_ours(struct nl_object *obj, void *sk)
{
    struct rtnl_route *rt = (struct rtnl_route*) obj;

    if (rtnl_route_get_table(rt) <= 1000)
        return;

    dump_obj((struct nl_object*) rt, NL_ACT_DEL, "route");
    errno = rtnl_route_delete((struct nl_sock*) sk, rt, 0);
    if (errno < 0)
        nl_perror(errno, __func__);
}

static void rule_flush_ours(struct nl_object *obj, void *sk)
{
    struct rtnl_rule *rule = (struct rtnl_rule*) obj;

    if (rtnl_rule_get_table(rule) <= 1000 || rtnl_rule_get_action(rule) != 1)
        return;

    dump_obj((struct nl_object*) rule, NL_ACT_DEL, "rule");
    errno = rtnl_rule_delete((struct nl_sock*) sk, rule, 0);
    if (errno < 0)
        nl_perror(errno, __func__);
}

static void mirror_route_update(struct nl_cache *cache, struct nl_object *obj, int action, void *sk)
{
    struct rtnl_route *rt = (struct rtnl_route*) obj;
    struct rtnl_nexthop *nh;
    int ifindex;

    if (action != NL_ACT_NEW && action != NL_ACT_DEL) {
        printf("%s: unhandled action %d\n", __func__, action);
        return;
    }

    if (rtnl_route_get_table(rt) != RT_TABLE_MAIN)
        return;
    // Skip multipath destinations
    if (rtnl_route_get_nnexthops(rt) != 1)
        return;
    nh = rtnl_route_nexthop_n(rt, 0);
    if (!nh)
        return;
    // Skip invalid and loopback interface (ifindex 1)
    ifindex = rtnl_route_nh_get_ifindex(nh);
    if (ifindex <= 1)
        return;
    rt = (struct rtnl_route*) nl_object_clone(obj);
    if (!rt)
        return;

    rtnl_route_set_table(rt, 1000 + ifindex);
    dump_obj((struct nl_object*) rt, action, "route");

    if (action == NL_ACT_NEW) {
        errno = rtnl_route_add((struct nl_sock*) sk, rt, NLM_F_EXCL);
    } else if (action == NL_ACT_DEL) {
        // We replicate routes created by the kernel (e.g. directly attached routes),
        // and the kernel seems to remove our replicated routes as well. This seems
        // to give the intended behavior, so we ignore such removal failures.
        errno = rtnl_route_delete((struct nl_sock*) sk, rt, NLM_F_EXCL);

        if (errno == -NLE_OBJ_NOTFOUND && rtnl_route_get_protocol(rt) == ROUTE_PROTOCOL_KERNEL)
            errno = 0;
    }

    if (errno < 0)
        nl_perror(errno, __func__);

    rtnl_route_put(rt);
}

static void mirror_route_new(struct nl_object *obj, void *sk)
{
    mirror_route_update(NULL, obj, NL_ACT_NEW, sk);
}

static void addr_rule_update(struct nl_cache *cache, struct nl_object *obj, int action, void *sk)
{
    struct rtnl_addr *addr = (struct rtnl_addr*) obj;
    struct rtnl_rule *rule;
    struct nl_addr *local;
    int ifindex;

    if (action != NL_ACT_NEW && action != NL_ACT_DEL) {
        // Ignore attribute changes
        if (action == NL_ACT_CHANGE)
            return;
        printf("%s: unhandled action %d\n", __func__, action);
        return;
    }

    // Filter non-global addresses:
    if (rtnl_addr_get_scope(addr) != 0)
        return;
    if (rtnl_addr_get_family(addr) != AF_INET && rtnl_addr_get_family(addr) != AF_INET6)
        return;
    // Skip invalid and loopback interface (ifindex 1)
    ifindex = rtnl_addr_get_ifindex(addr);
    if (ifindex <= 1)
        return;
    if (!(local = rtnl_addr_get_local(addr)))
        return;
    if (!(local = nl_addr_clone(local)))
        return;
    if (!(rule = rtnl_rule_alloc())) {
        nl_addr_put(local);
        return;
    }

    if (rtnl_addr_get_family(addr) == AF_INET)
        nl_addr_set_prefixlen(local, 32);
    else if (rtnl_addr_get_family(addr) == AF_INET6)
        nl_addr_set_prefixlen(local, 128);

    rtnl_rule_set_src(rule, local);
    rtnl_rule_set_table(rule, 1000 + ifindex);
    rtnl_rule_set_action(rule, 1);
    dump_obj((struct nl_object*) rule, action, "rule");

    if (action == NL_ACT_NEW)
        errno = rtnl_rule_add((struct nl_sock*) sk, rule, 0);
    else if (action == NL_ACT_DEL)
        errno = rtnl_rule_delete((struct nl_sock*) sk, rule, 0);

    if (errno < 0)
        nl_perror(errno, __func__);

    rtnl_rule_put(rule);
    nl_addr_put(local);
}

static void addr_rule_new(struct nl_object *obj, void *sk)
{
     addr_rule_update(NULL, obj, NL_ACT_NEW, sk);
}

int main(int argc, char **argv)
{
    struct nl_cache_mngr *mngr;
    struct nl_cache *routes;
    struct nl_cache *rules;
    struct nl_cache *addrs;
    struct nl_sock *sk;
    int err;

    // No options yet
    if (getopt(argc, argv, "") != -1) {
        fprintf(stderr, "Usage: %s\n", argv[0]);
        return EINVAL;
    }

    if (!(sk = nl_socket_alloc()))
        return ENOMEM;
    if ((err = nl_connect(sk, NETLINK_ROUTE)) < 0)
        goto out_nlerr;
    if ((err = nl_cache_mngr_alloc(NULL, NETLINK_ROUTE, NL_AUTO_PROVIDE, &mngr)) < 0)
        goto out_nlerr;
    if ((err = nl_cache_mngr_add(mngr, "route/route", mirror_route_update, sk, &routes)) < 0)
        goto out_nlerr;
    if ((err = nl_cache_mngr_add(mngr, "route/addr", addr_rule_update, sk, &addrs)) < 0)
        goto out_nlerr;

    // Use line buffering to improve the debug utility of systemd's journal:
    setlinebuf(stdout);

    // In later kernels, a protocol field attached to each rule can be used as an
    // selector instead of table id ranges
    printf("Deleting routing policy rules matching lookup table > 1000\n");
    if ((err = rtnl_rule_alloc_cache(sk, AF_UNSPEC, &rules)) < 0)
        goto out_nlerr;
    nl_cache_foreach(rules, rule_flush_ours, sk);
    nl_cache_free(rules);

    printf("Deleting route tables with id > 1000\n");
    nl_cache_foreach(routes, route_flush_ours, sk);

    printf("Replicating main route table into device-specific route tables\n");
    nl_cache_foreach(routes, mirror_route_new, sk);
    printf("Creating network source-specific lookup rules\n");
    nl_cache_foreach(addrs, addr_rule_new, sk);

    printf("Waiting for changes\n");

    while (1) {
        nl_cache_mngr_poll(mngr, -1);
    }

    nl_cache_mngr_free(mngr);
    nl_socket_free(sk);
    return 0;

out_nlerr:
    nl_perror(err, "netlink");
    return err;
}
