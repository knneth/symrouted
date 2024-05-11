#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <string>
#include <unistd.h>
#include <vector>

#include <libnl3/netlink/netlink.h>
#include <libnl3/netlink/route/addr.h>
#include <libnl3/netlink/route/nexthop.h>
#include <libnl3/netlink/route/route.h>
#include <libnl3/netlink/route/rule.h>

enum {
    ROUTE_PROTOCOL_KERNEL = 2
};

static std::vector<std::pair<int, unsigned int>> g_set_route_metrics; //< Metrics supplied via --set-route-metric command line option, keyed by netlink metric value
static bool g_dump; //< Dump cache on startup for debugging

/**
 * Extract the first two tokens from a string as a destructuring convenience, e.g.,
 *   auto [key, value] = string_tokenize_pair(str, "=");
 * If a string contains less than two tokens, empty string(s) are returned
 */
constexpr std::pair<std::string_view, std::string_view> string_tokenize_pair(std::string_view str, std::string_view delimiters)
{
    const size_t first_token = str.find_first_not_of(delimiters);
    constexpr const std::string_view empty;
    if (first_token == std::string::npos) {
        // Empty string or string with only delimiters
        return std::make_pair(str, empty);
    }
    const size_t first_delim = str.find_first_of(delimiters, first_token);
    if (first_delim == std::string::npos) {
        // No delimiters found after first token
        return std::make_pair(str.substr(first_token), empty);
    }
    const size_t second_token = str.find_first_not_of(delimiters, first_delim);
    if (second_token == std::string::npos) {
        // No second token found
        return std::make_pair(str.substr(first_token, first_delim - first_token), empty);
    }
    const size_t second_delim = str.find_first_of(delimiters, second_token);
    return std::make_pair(str.substr(first_token, first_delim - first_token),
                          str.substr(second_token, second_delim - second_token));
}

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

    // Ignore events outside Main Routing Table
    if (rtnl_route_get_table(rt) != RT_TABLE_MAIN)
        return;

    if (action != NL_ACT_NEW && action != NL_ACT_DEL && action != NL_ACT_CHANGE) {
        printf("%s: unhandled action %d\n", __func__, action);
        return;
    }

    // Skip multipath destinations
    if (rtnl_route_get_nnexthops(rt) != 1)
        return;
    // Retrieve nexthop for route
    nh = rtnl_route_nexthop_n(rt, 0);
    if (!nh)
        return;
    // Skip invalid and loopback interface (ifindex 1)
    ifindex = rtnl_route_nh_get_ifindex(nh);
    if (ifindex <= 1)
        return;
    // Skip IPv6 link-local destinations
    if (rtnl_route_get_family(rt) == AF_INET6) {
        const uint8_t *addr = reinterpret_cast<uint8_t*>(nl_addr_get_binary_addr(rtnl_route_get_dst(rt)));
        if (addr[0] == 0xfe) {
            return;
        }
    }
    // Clone route so that we may configure our modified version
    rt = (struct rtnl_route*) nl_object_clone(obj);
    if (!rt)
        return;

    for (const auto &[metric, value] : g_set_route_metrics) {
        if (rtnl_route_set_metric(rt, metric, value) < 0)
            nl_perror(errno, __func__);
    }

    rtnl_route_set_table(rt, 1000 + ifindex);
    dump_obj((struct nl_object*) rt, action, "route");

    if (action == NL_ACT_NEW || action == NL_ACT_CHANGE) {
        int flags = 0;

        if (action == NL_ACT_CHANGE)
            flags |= NLM_F_REPLACE;

        errno = rtnl_route_add((struct nl_sock*) sk, rt, flags);
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
    dump_obj((struct nl_object*) obj, NL_ACT_NEW, "route-init");
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

static bool parse_opts(int argc, char **argv)
{
    enum long_option {
        SET_ROUTE_METRIC,
        DUMP,
        HELP,
        NUM_OPTIONS,
    };
    static const struct option long_options[]{
        [SET_ROUTE_METRIC] = {"set-route-metric", required_argument},
        [DUMP] = {"dump", no_argument},
        [HELP] = {"help", no_argument},
        [NUM_OPTIONS] = {},
    };

    while (true) {
        int option_index = -1;
        if (getopt_long_only(argc, argv, "", long_options, &option_index) == -1)
            return true;

        switch (option_index) {
        case SET_ROUTE_METRIC: {
            const auto [metric_name, metric_value_str] = string_tokenize_pair(optarg, "=");
            if (metric_name.empty() || metric_value_str.empty()) {
                fprintf(stderr, "Invalid syntax for --%s %s option\n", long_options[option_index].name, optarg);
                fprintf(stderr, "Expected: --%s <key>=<value>\n", long_options[option_index].name);
                return false;
            }
            const int metric = rtnl_route_str2metric(std::string(metric_name).c_str());
            if (metric < 0) {
                nl_perror(metric, ("Unable to parse metric " + std::string(metric_name)).c_str());
                return false;
            }
            const unsigned int metric_value = std::stoul(std::string(metric_value_str));
            g_set_route_metrics.emplace_back(metric, metric_value);
            break;
        }

        case DUMP:
            g_dump = true;
            break;

        case HELP:
        default:
            fprintf(stderr, "Usage: %s [...]\n\n", argv[0]);
            fprintf(stderr, "\t--%-16s %-16s - %s\n", long_options[SET_ROUTE_METRIC].name, "<name>=<value>",
                    "Adds the specified metric to every replicated route; can be specified multiple times");
            fprintf(stderr, "\t--%-16s %-16s - %s\n", long_options[DUMP].name, "",
                    "Dump all observed attributes on startup");
            return false;
        }
    }
}

int main(int argc, char **argv)
{
    struct nl_cache_mngr *mngr;
    struct nl_cache *routes;
    struct nl_cache *rules;
    struct nl_cache *addrs;
    struct nl_sock *sk;
    int err;

    if (!parse_opts(argc, argv))
        return EINVAL;

    if (!(sk = nl_socket_alloc()))
        return ENOMEM;
    if ((err = nl_connect(sk, NETLINK_ROUTE)) < 0 ||
        (err = nl_cache_mngr_alloc(NULL, NETLINK_ROUTE, NL_AUTO_PROVIDE, &mngr)) < 0 ||
        (err = nl_cache_mngr_add(mngr, "route/route", mirror_route_update, sk, &routes)) < 0 ||
        (err = nl_cache_mngr_add(mngr, "route/addr", addr_rule_update, sk, &addrs)) < 0) {
        nl_perror(err, "netlink");
        return err;
    }

    // Use line buffering to improve the debug utility of systemd's journal:
    setlinebuf(stdout);

    // In later kernels, a protocol field attached to each rule can be used as an
    // selector instead of table id ranges
    printf("Deleting routing policy rules matching lookup table > 1000\n");
    if ((err = rtnl_rule_alloc_cache(sk, AF_UNSPEC, &rules)) < 0) {
        nl_perror(err, "netlink");
        return err;
    }

    nl_cache_foreach(rules, rule_flush_ours, sk);
    nl_cache_free(rules);

    printf("Deleting route tables with id > 1000\n");
    nl_cache_foreach(routes, route_flush_ours, sk);

    printf("Replicating main route table into device-specific route tables\n");
    nl_cache_foreach(routes, mirror_route_new, sk);
    printf("Creating network source-specific lookup rules\n");
    nl_cache_foreach(addrs, addr_rule_new, sk);

    if (g_dump) {
        struct nl_dump_params dp = {
            .dp_type = NL_DUMP_STATS,
            .dp_fd = stdout,
        };
        nl_cache_mngr_info(mngr, &dp);
    }

    printf("Waiting for changes\n");

    while (1) {
        nl_cache_mngr_poll(mngr, -1);
    }

    nl_cache_mngr_free(mngr);
    nl_socket_free(sk);
    return 0;
}
