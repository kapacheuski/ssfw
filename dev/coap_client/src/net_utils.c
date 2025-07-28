#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>

// OpenThread headers
#include <openthread/thread.h>
#include <openthread/thread_ftd.h>
#include <openthread/instance.h>
#include <openthread/netdata.h>
#include <openthread/link.h>
#include <openthread/nat64.h>
#include <openthread/border_router.h>
#include <openthread/dataset.h>
#include <openthread/dns_client.h>

// Zephyr OpenThread integration
#include <zephyr/net/openthread.h>

// Standard C headers
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Bluetooth NUS (if used for printf output)
#include "ble_utils.h"

LOG_MODULE_REGISTER(net_utils, LOG_LEVEL_INF);

/**
 * Display OpenThread Network Data
 */
void display_openthread_netdata(void)
{
    struct openthread_context *context = openthread_get_default_context();
    if (!context)
    {
        LOG_ERR("OpenThread context not available");
        bt_nus_printf("OpenThread context not available\n");
        return;
    }

    otInstance *instance = context->instance;
    if (!instance)
    {
        LOG_ERR("OpenThread instance not available");
        bt_nus_printf("OpenThread instance not available\n");
        return;
    }

    LOG_INF("=== OpenThread Network Data ===");
    bt_nus_printf("=== OpenThread Network Data ===\n");

    // Display device role and basic info
    otDeviceRole role = otThreadGetDeviceRole(instance);
    const char *role_str = "Unknown";

    switch (role)
    {
    case OT_DEVICE_ROLE_DISABLED:
        role_str = "Disabled";
        break;
    case OT_DEVICE_ROLE_DETACHED:
        role_str = "Detached";
        break;
    case OT_DEVICE_ROLE_CHILD:
        role_str = "Child";
        break;
    case OT_DEVICE_ROLE_ROUTER:
        role_str = "Router";
        break;
    case OT_DEVICE_ROLE_LEADER:
        role_str = "Leader";
        break;
    }

    LOG_INF("Device Role: %s", role_str);
    bt_nus_printf("Device Role: %s\n", role_str);

    if (role == OT_DEVICE_ROLE_DISABLED || role == OT_DEVICE_ROLE_DETACHED)
    {
        LOG_WRN("Device not attached to Thread network");
        bt_nus_printf("Device not attached to Thread network\n");
        return;
    }

    // Display network name and PAN ID

    const char *networkName = otThreadGetNetworkName(instance);
    if (networkName)
    {
        LOG_INF("Network Name: %s", networkName);
        bt_nus_printf("Network Name: %s\n", networkName);
    }

    uint16_t panId = otLinkGetPanId(instance);
    LOG_INF("PAN ID: 0x%04x", panId);
    bt_nus_printf("PAN ID: 0x%04x\n", panId);

    uint8_t channel = otLinkGetChannel(instance);
    LOG_INF("Channel: %d", channel);
    bt_nus_printf("Channel: %d\n", channel);

    // Display mesh local prefix
    const otMeshLocalPrefix *mlp = otThreadGetMeshLocalPrefix(instance);
    if (mlp)
    {
        char prefix_str[INET6_ADDRSTRLEN];
        struct in6_addr ml_prefix;
        memcpy(&ml_prefix, mlp->m8, 8);
        memset(&ml_prefix.s6_addr[8], 0, 8); // Clear the rest

        if (zsock_inet_ntop(AF_INET6, &ml_prefix, prefix_str, sizeof(prefix_str)))
        {
            LOG_INF("Mesh Local Prefix: %s/64", prefix_str);
            bt_nus_printf("Mesh Local Prefix: %s/64\n", prefix_str);
        }
    }

    // Display on-mesh prefixes
    LOG_INF("--- On-Mesh Prefixes ---");
    bt_nus_printf("--- On-Mesh Prefixes ---\n");

    otNetworkDataIterator iterator = OT_NETWORK_DATA_ITERATOR_INIT;
    otBorderRouterConfig config;
    int prefix_count = 0;

    while (otNetDataGetNextOnMeshPrefix(instance, &iterator, &config) == OT_ERROR_NONE)
    {
        char prefix_str[INET6_ADDRSTRLEN];
        if (zsock_inet_ntop(AF_INET6, &config.mPrefix.mPrefix, prefix_str, sizeof(prefix_str)))
        {
            LOG_INF("Prefix %d: %s/%d", prefix_count, prefix_str, config.mPrefix.mLength);
            bt_nus_printf("Prefix %d: %s/%d\n", prefix_count, prefix_str, config.mPrefix.mLength);

            LOG_INF("  Flags: %s%s%s%s%s",
                    config.mPreferred ? "P" : "",
                    config.mSlaac ? "A" : "",
                    config.mDhcp ? "D" : "",
                    config.mConfigure ? "C" : "",
                    config.mDefaultRoute ? "R" : "");
            bt_nus_printf("  Flags: %s%s%s%s%s\n",
                          config.mPreferred ? "P" : "",
                          config.mSlaac ? "A" : "",
                          config.mDhcp ? "D" : "",
                          config.mConfigure ? "C" : "",
                          config.mDefaultRoute ? "R" : "");
        }
        prefix_count++;
    }

    if (prefix_count == 0)
    {
        LOG_INF("No on-mesh prefixes found");
        bt_nus_printf("No on-mesh prefixes found\n");
    }

    // Display external routes
    LOG_INF("--- External Routes ---");
    bt_nus_printf("--- External Routes ---\n");

    iterator = OT_NETWORK_DATA_ITERATOR_INIT;
    otExternalRouteConfig routeConfig;
    int route_count = 0;

    while (otNetDataGetNextRoute(instance, &iterator, &routeConfig) == OT_ERROR_NONE)
    {
        char route_str[INET6_ADDRSTRLEN];
        if (zsock_inet_ntop(AF_INET6, &routeConfig.mPrefix.mPrefix, route_str, sizeof(route_str)))
        {
            LOG_INF("Route %d: %s/%d", route_count, route_str, routeConfig.mPrefix.mLength);
            bt_nus_printf("Route %d: %s/%d\n", route_count, route_str, routeConfig.mPrefix.mLength);

            const char *pref_str = "Low";
            if (routeConfig.mPreference == OT_ROUTE_PREFERENCE_MED)
                pref_str = "Medium";
            else if (routeConfig.mPreference == OT_ROUTE_PREFERENCE_HIGH)
                pref_str = "High";

            LOG_INF("  Preference: %s, NAT64: %s, Stable: %s",
                    pref_str,
                    routeConfig.mNat64 ? "Yes" : "No",
                    routeConfig.mStable ? "Yes" : "No");
            bt_nus_printf("  Preference: %s, NAT64: %s, Stable: %s\n",
                          pref_str,
                          routeConfig.mNat64 ? "Yes" : "No",
                          routeConfig.mStable ? "Yes" : "No");
        }
        route_count++;
    }

    if (route_count == 0)
    {
        LOG_INF("No external routes found");
        bt_nus_printf("No external routes found\n");
    }

    // Display services
    LOG_INF("--- Services ---");
    bt_nus_printf("--- Services ---\n");

    iterator = OT_NETWORK_DATA_ITERATOR_INIT;
    otServiceConfig serviceConfig;
    int service_count = 0;

    while (otNetDataGetNextService(instance, &iterator, &serviceConfig) == OT_ERROR_NONE)
    {
        LOG_INF("Service %d: Enterprise Number: %u", service_count, serviceConfig.mEnterpriseNumber);
        bt_nus_printf("Service %d: Enterprise Number: %u\n", service_count, serviceConfig.mEnterpriseNumber);

        // Display service data in hex
        char service_data_hex[128] = {0};
        for (uint8_t i = 0; i < serviceConfig.mServiceDataLength && i < 32; i++)
        {
            snprintf(service_data_hex + (i * 2), sizeof(service_data_hex) - (i * 2),
                     "%02x", serviceConfig.mServiceData[i]);
        }

        LOG_INF("  Data: %s", service_data_hex);
        bt_nus_printf("  Data: %s\n", service_data_hex);

        service_count++;
    }

    if (service_count == 0)
    {
        LOG_INF("No services found");
        bt_nus_printf("No services found\n");
    }

    // Check for NAT64 prefix specifically
    LOG_INF("--- NAT64 Information ---");
    bt_nus_printf("--- NAT64 Information ---\n");

    // Alternative method: Look for NAT64 routes in external routes
    iterator = OT_NETWORK_DATA_ITERATOR_INIT;
    otExternalRouteConfig nat64RouteConfig;
    bool nat64Found = false;

    while (otNetDataGetNextRoute(instance, &iterator, &nat64RouteConfig) == OT_ERROR_NONE)
    {
        if (nat64RouteConfig.mNat64)
        {
            char nat64_str[INET6_ADDRSTRLEN];
            if (zsock_inet_ntop(AF_INET6, &nat64RouteConfig.mPrefix.mPrefix, nat64_str, sizeof(nat64_str)))
            {
                LOG_INF("NAT64 Route: %s/%d", nat64_str, nat64RouteConfig.mPrefix.mLength);
                bt_nus_printf("NAT64 Route: %s/%d\n", nat64_str, nat64RouteConfig.mPrefix.mLength);

                const char *pref_str = "Low";
                if (nat64RouteConfig.mPreference == OT_ROUTE_PREFERENCE_MED)
                    pref_str = "Medium";
                else if (nat64RouteConfig.mPreference == OT_ROUTE_PREFERENCE_HIGH)
                    pref_str = "High";

                LOG_INF("  Preference: %s, Stable: %s", pref_str, nat64RouteConfig.mStable ? "Yes" : "No");
                bt_nus_printf("  Preference: %s, Stable: %s\n", pref_str, nat64RouteConfig.mStable ? "Yes" : "No");

                nat64Found = true;
            }
        }
    }

    if (!nat64Found)
    {
        LOG_INF("No NAT64 routes found in network data");
        bt_nus_printf("No NAT64 routes found in network data\n");

#ifdef CONFIG_OPENTHREAD_NAT64_TRANSLATOR
        // Check if local NAT64 is enabled (if supported)
        // This is a fallback check
        LOG_INF("Checking local NAT64 translator status...");
        bt_nus_printf("Checking local NAT64 translator status...\n");

        // Alternative: Try to get any /96 prefix that could be NAT64
        iterator = OT_NETWORK_DATA_ITERATOR_INIT;
        while (otNetDataGetNextRoute(instance, &iterator, &nat64RouteConfig) == OT_ERROR_NONE)
        {
            if (nat64RouteConfig.mPrefix.mLength == 96)
            {
                char potential_nat64_str[INET6_ADDRSTRLEN];
                if (zsock_inet_ntop(AF_INET6, &nat64RouteConfig.mPrefix.mPrefix, potential_nat64_str, sizeof(potential_nat64_str)))
                {
                    LOG_INF("Potential NAT64 prefix (/96): %s/%d", potential_nat64_str, nat64RouteConfig.mPrefix.mLength);
                    bt_nus_printf("Potential NAT64 prefix (/96): %s/%d\n", potential_nat64_str, nat64RouteConfig.mPrefix.mLength);
                }
            }
        }
#endif
    }

    LOG_INF("=== End Network Data ===");
    bt_nus_printf("=== End Network Data ===\n");
}

/**
 * Display network data in raw format
 */
void display_raw_netdata(void)
{
    struct openthread_context *context = openthread_get_default_context();
    if (!context)
    {
        LOG_ERR("OpenThread context not available");
        bt_nus_printf("OpenThread context not available\n");
        return;
    }

    otInstance *instance = context->instance;
    if (!instance)
    {
        LOG_ERR("OpenThread instance not available");
        bt_nus_printf("OpenThread instance not available\n");
        return;
    }

    LOG_INF("=== Raw Network Data ===");
    bt_nus_printf("=== Raw Network Data ===\n");

    uint8_t data[255];
    uint8_t length = 255;

    otError error = otNetDataGet(instance, false, data, &length);
    if (error == (uint8_t)OT_ERROR_NONE)
    {
        LOG_INF("Network Data Length: %d bytes", length);
        bt_nus_printf("Network Data Length: %d bytes\n", length);

        // Display data in hex format
        char hex_str[512] = {0};
        for (uint8_t i = 0; i < length && i < 128; i++)
        {
            snprintf(hex_str + (i * 3), sizeof(hex_str) - (i * 3), "%02x ", data[i]);
        }

        LOG_INF("Raw Data: %s", hex_str);
        bt_nus_printf("Raw Data: %s\n", hex_str);
    }
    else
    {
        LOG_ERR("Failed to get network data: %d", error);
        bt_nus_printf("Failed to get network data: %d\n", error);
    }

    // Also get stable network data
    length = (uint8_t)sizeof(data);
    error = otNetDataGet(instance, true, data, &length);
    if (error == (uint8_t)OT_ERROR_NONE)
    {
        LOG_INF("Stable Network Data Length: %d bytes", length);
        bt_nus_printf("Stable Network Data Length: %d bytes\n", length);

        char hex_str[512] = {0};
        for (uint8_t i = 0; i < length && i < 128; i++)
        {
            snprintf(hex_str + (i * 3), sizeof(hex_str) - (i * 3), "%02x ", data[i]);
        }

        LOG_INF("Stable Data: %s", hex_str);
        bt_nus_printf("Stable Data: %s\n", hex_str);
    }
    else
    {
        LOG_ERR("Failed to get stable network data: %d", error);
        bt_nus_printf("Failed to get stable network data: %d\n", error);
    }

    LOG_INF("=== End Raw Network Data ===");
    bt_nus_printf("=== End Raw Network Data ===\n");
}

/**
 * Display Thread network topology information
 */
void display_thread_topology(void)
{
    struct openthread_context *context = openthread_get_default_context();
    if (!context)
    {
        LOG_ERR("OpenThread context not available");
        bt_nus_printf("OpenThread context not available\n");
        return;
    }

    otInstance *instance = context->instance;
    if (!instance)
    {
        LOG_ERR("OpenThread instance not available");
        bt_nus_printf("OpenThread instance not available\n");
        return;
    }

    LOG_INF("=== Thread Topology ===");
    bt_nus_printf("=== Thread Topology ===\n");

    // Router information
    // uint8_t maxRouterId = otThreadGetMaxRouterId(instance);
    // LOG_INF("Max Router ID: %d", maxRouterId);
    // bt_nus_printf("Max Router ID: %d\n", maxRouterId);

    // Leader information
    otLeaderData leaderData;
    otError error = otThreadGetLeaderData(instance, &leaderData);
    if (error == OT_ERROR_NONE)
    {
        LOG_INF("Leader Router ID: %d", leaderData.mLeaderRouterId);
        LOG_INF("Partition ID: 0x%08x", leaderData.mPartitionId);
        LOG_INF("Weighting: %d", leaderData.mWeighting);
        LOG_INF("Data Version: %d", leaderData.mDataVersion);
        LOG_INF("Stable Data Version: %d", leaderData.mStableDataVersion);

        bt_nus_printf("Leader Router ID: %d\n", leaderData.mLeaderRouterId);
        bt_nus_printf("Partition ID: 0x%08x\n", leaderData.mPartitionId);
        bt_nus_printf("Weighting: %d\n", leaderData.mWeighting);
        bt_nus_printf("Data Version: %d\n", leaderData.mDataVersion);
        bt_nus_printf("Stable Data Version: %d\n", leaderData.mStableDataVersion);
    }

    LOG_INF("=== End Thread Topology ===");
    bt_nus_printf("=== End Thread Topology ===\n");
}

/**
 * Find NAT64 prefixes using alternative methods
 */
void find_nat64_prefixes(void)
{
    struct openthread_context *context = openthread_get_default_context();
    if (!context)
    {
        LOG_ERR("OpenThread context not available");
        bt_nus_printf("OpenThread context not available\n");
        return;
    }

    otInstance *instance = context->instance;
    if (!instance)
    {
        LOG_ERR("OpenThread instance not available");
        bt_nus_printf("OpenThread instance not available\n");
        return;
    }

    LOG_INF("=== Searching for NAT64 Prefixes ===");
    bt_nus_printf("=== Searching for NAT64 Prefixes ===\n");

    // Method 1: Look for routes marked as NAT64
    otNetworkDataIterator iterator = OT_NETWORK_DATA_ITERATOR_INIT;
    otExternalRouteConfig routeConfig;
    int nat64_count = 0;

    LOG_INF("Method 1: Checking external routes for NAT64 flag...");
    bt_nus_printf("Method 1: Checking external routes for NAT64 flag...\n");

    while (otNetDataGetNextRoute(instance, &iterator, &routeConfig) == OT_ERROR_NONE)
    {
        if (routeConfig.mNat64)
        {
            char route_str[INET6_ADDRSTRLEN];
            if (zsock_inet_ntop(AF_INET6, &routeConfig.mPrefix.mPrefix, route_str, sizeof(route_str)))
            {
                LOG_INF("  NAT64 Route %d: %s/%d", nat64_count, route_str, routeConfig.mPrefix.mLength);
                bt_nus_printf("  NAT64 Route %d: %s/%d\n", nat64_count, route_str, routeConfig.mPrefix.mLength);
                nat64_count++;
            }
        }
    }

    // Method 2: Look for common NAT64 prefix patterns
    LOG_INF("Method 2: Checking for common NAT64 prefix patterns...");
    bt_nus_printf("Method 2: Checking for common NAT64 prefix patterns...\n");

    iterator = OT_NETWORK_DATA_ITERATOR_INIT;
    int potential_count = 0;

    while (otNetDataGetNextRoute(instance, &iterator, &routeConfig) == OT_ERROR_NONE)
    {
        // Check for /96 prefixes (typical for NAT64)
        if (routeConfig.mPrefix.mLength == 96)
        {
            char route_str[INET6_ADDRSTRLEN];
            if (zsock_inet_ntop(AF_INET6, &routeConfig.mPrefix.mPrefix, route_str, sizeof(route_str)))
            {
                LOG_INF("  Potential NAT64 (/96) %d: %s/%d", potential_count, route_str, routeConfig.mPrefix.mLength);
                bt_nus_printf("  Potential NAT64 (/96) %d: %s/%d\n", potential_count, route_str, routeConfig.mPrefix.mLength);
                potential_count++;
            }
        }

        // Check for well-known NAT64 prefixes
        uint8_t *prefix_bytes = (uint8_t *)&routeConfig.mPrefix.mPrefix;

        // RFC 6052 Well-Known Prefix: 64:ff9b::/96
        if (prefix_bytes[0] == 0x00 && prefix_bytes[1] == 0x64 &&
            prefix_bytes[2] == 0xff && prefix_bytes[3] == 0x9b)
        {
            char route_str[INET6_ADDRSTRLEN];
            if (zsock_inet_ntop(AF_INET6, &routeConfig.mPrefix.mPrefix, route_str, sizeof(route_str)))
            {
                LOG_INF("  RFC 6052 Well-Known: %s/%d", route_str, routeConfig.mPrefix.mLength);
                bt_nus_printf("  RFC 6052 Well-Known: %s/%d\n", route_str, routeConfig.mPrefix.mLength);
            }
        }
    }

    // Method 3: Generate Thread mesh local based NAT64 prefix
    LOG_INF("Method 3: Generating Thread mesh local NAT64 prefix...");
    bt_nus_printf("Method 3: Generating Thread mesh local NAT64 prefix...\n");

    const otMeshLocalPrefix *mlp = otThreadGetMeshLocalPrefix(instance);
    if (mlp)
    {
        struct in6_addr thread_nat64_prefix;
        memset(&thread_nat64_prefix, 0, sizeof(thread_nat64_prefix));

        // Use mesh local prefix + NAT64 suffix
        memcpy(thread_nat64_prefix.s6_addr, mlp->m8, 8); // First 8 bytes from mesh local
        thread_nat64_prefix.s6_addr[10] = 0xff;          // NAT64 indicator
        thread_nat64_prefix.s6_addr[11] = 0xff;

        char thread_nat64_str[INET6_ADDRSTRLEN];
        if (zsock_inet_ntop(AF_INET6, &thread_nat64_prefix, thread_nat64_str, sizeof(thread_nat64_str)))
        {
            LOG_INF("  Thread mesh NAT64: %s/96", thread_nat64_str);
            bt_nus_printf("  Thread mesh NAT64: %s/96\n", thread_nat64_str);
        }
    }

    LOG_INF("=== NAT64 Search Complete ===");
    bt_nus_printf("=== NAT64 Search Complete ===\n");
    LOG_INF("Found %d explicit NAT64 routes, %d potential /96 prefixes", nat64_count, potential_count);
    bt_nus_printf("Found %d explicit NAT64 routes, %d potential /96 prefixes\n", nat64_count, potential_count);
}

void get_netdata_routes(void)
{
    bt_nus_printf("=== Network Interface Information ===\n");

    struct net_if *iface = net_if_get_default();
    if (!iface)
    {
        bt_nus_printf("No default network interface found\n");
        return;
    }

    // Display interface information
    char iface_name[16];
    net_if_get_name(iface, iface_name, sizeof(iface_name));
    bt_nus_printf("Default Interface: %s\n", iface_name);
    bt_nus_printf("Interface Index: %d\n", net_if_get_by_iface(iface));
    bt_nus_printf("MTU: %d\n", net_if_get_mtu(iface));

    // Display IPv6 addresses
    bt_nus_printf("--- IPv6 Addresses ---\n");

    int addr_count = 0;

    // Get IPv6 configuration for this interface
    struct net_if_ipv6 *ipv6 = iface->config.ip.ipv6;
    if (ipv6)
    {
        for (int i = 0; i < NET_IF_MAX_IPV6_ADDR; i++)
        {
            struct net_if_addr *addr = &ipv6->unicast[i];
            if (!addr->is_used)
                continue;

            char addr_str[NET_IPV6_ADDR_LEN];
            net_addr_ntop(AF_INET6, &addr->address.in6_addr, addr_str, sizeof(addr_str));

            const char *state_str = "Unknown";
            switch (addr->addr_state)
            {
            case NET_ADDR_TENTATIVE:
                state_str = "Tentative";
                break;
            case NET_ADDR_PREFERRED:
                state_str = "Preferred";
                break;
            case NET_ADDR_DEPRECATED:
                state_str = "Deprecated";
                break;
            case NET_ADDR_ANY_STATE:
                state_str = "Any State";
                break;
            }

            const char *type_str = "Unknown";
            switch (addr->addr_type)
            {
            case NET_ADDR_MANUAL:
                type_str = "Manual";
                break;
            case NET_ADDR_DHCP:
                type_str = "DHCP";
                break;
            case NET_ADDR_AUTOCONF:
                type_str = "AutoConf";
                break;
            case NET_ADDR_ANY:
                type_str = "Any";
                break;
            case NET_ADDR_OVERRIDABLE:
                type_str = "Overridable";
                break;
            }

            bt_nus_printf("  Address %d: %s\n", addr_count, addr_str);
            bt_nus_printf("    State: %s, Type: %s\n", state_str, type_str);
            bt_nus_printf("    Infinite: %s\n", addr->is_infinite ? "Yes" : "No");

            addr_count++;
        }
        k_sleep(K_MSEC(1000));
        // Display multicast addresses
        bt_nus_printf("--- IPv6 Multicast Addresses ---\n");

        int mcast_count = 0;
        for (int i = 0; i < NET_IF_MAX_IPV6_MADDR; i++)
        {
            struct net_if_mcast_addr *maddr = &ipv6->mcast[i];
            if (!maddr->is_used)
                continue;

            char maddr_str[NET_IPV6_ADDR_LEN];
            net_addr_ntop(AF_INET6, &maddr->address.in6_addr, maddr_str, sizeof(maddr_str));

            bt_nus_printf("  Multicast %d: %s\n", mcast_count, maddr_str);
            mcast_count++;
        }

        if (mcast_count == 0)
        {
            bt_nus_printf("No multicast addresses found\n");
        }
    }

    if (addr_count == 0)
    {
        bt_nus_printf("No IPv6 addresses found\n");
    }
    k_sleep(K_MSEC(1000));
    // Display OpenThread specific routing info
    bt_nus_printf("--- OpenThread Network Routes ---\n");

    struct openthread_context *ot_context = openthread_get_default_context();
    if (ot_context)
    {
        otInstance *instance = ot_context->instance;
        if (instance)
        {
            // Display external routes from OpenThread network data
            otNetworkDataIterator iterator = OT_NETWORK_DATA_ITERATOR_INIT;
            otExternalRouteConfig routeConfig;
            int ot_route_count = 0;

            while (otNetDataGetNextRoute(instance, &iterator, &routeConfig) == OT_ERROR_NONE)
            {
                char route_str[INET6_ADDRSTRLEN];
                if (zsock_inet_ntop(AF_INET6, &routeConfig.mPrefix.mPrefix, route_str, sizeof(route_str)))
                {
                    bt_nus_printf("  OT Route %d: %s/%d\n", ot_route_count, route_str, routeConfig.mPrefix.mLength);

                    const char *pref_str = "Low";
                    if (routeConfig.mPreference == OT_ROUTE_PREFERENCE_MED)
                        pref_str = "Medium";
                    else if (routeConfig.mPreference == OT_ROUTE_PREFERENCE_HIGH)
                        pref_str = "High";

                    bt_nus_printf("    Preference: %s, NAT64: %s, Stable: %s\n",
                                  pref_str,
                                  routeConfig.mNat64 ? "Yes" : "No",
                                  routeConfig.mStable ? "Yes" : "No");
                    ot_route_count++;
                }
            }

            if (ot_route_count == 0)
            {
                bt_nus_printf("No OpenThread external routes found\n");
            }
        }
    }

    bt_nus_printf("=== End Network Interface Information ===\n");
}

/**
 * Initialize and start Thread network
 */
// void init_thread_network(void)
// {
//     struct net_if *iface = net_if_get_default();
//     if (!iface)
//     {
//         bt_nus_printf("No network interface available\n");
//         return;
//     }

//     otInstance *instance = net_if_l2_data(iface);
//     if (!instance)
//     {
//         bt_nus_printf("OpenThread instance not available\n");
//         return;
//     }

//     bt_nus_printf("=== Initializing Thread Network ===\n");

//     // Check current state
//     otDeviceRole role = otThreadGetDeviceRole(instance);
//     bt_nus_printf("Current role: %s\n",
//                   role == OT_DEVICE_ROLE_DISABLED ? "Disabled" : role == OT_DEVICE_ROLE_DETACHED ? "Detached"
//                                                              : role == OT_DEVICE_ROLE_CHILD      ? "Child"
//                                                              : role == OT_DEVICE_ROLE_ROUTER     ? "Router"
//                                                              : role == OT_DEVICE_ROLE_LEADER     ? "Leader"
//                                                                                                  : "Unknown");

//     // Enable Thread interface if disabled
//     if (role == OT_DEVICE_ROLE_DISABLED)
//     {
//         bt_nus_printf("Enabling Thread interface...\n");
//         otError error = otIp6SetEnabled(instance, true);
//         if (error != OT_ERROR_NONE)
//         {
//             bt_nus_printf("Failed to enable IPv6: %d\n", error);
//             return;
//         }

//         error = otThreadSetEnabled(instance, true);
//         if (error != OT_ERROR_NONE)
//         {
//             bt_nus_printf("Failed to enable Thread: %d\n", error);
//             return;
//         }
//     }

//     // Set network credentials (replace with your network's credentials)
//     bt_nus_printf("Setting network credentials...\n");

//     // Set network name
//     otError error = otThreadSetNetworkName(instance, "OpenThreadDemo");
//     if (error != OT_ERROR_NONE)
//     {
//         bt_nus_printf("Failed to set network name: %d\n", error);
//     }

//     // Set network key (example key - use your own)
//     otNetworkKey networkKey;
//     const uint8_t key[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
//                            0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
//     memcpy(networkKey.m8, key, sizeof(networkKey.m8));

//     error = otThreadSetNetworkKey(instance, &networkKey);
//     if (error != OT_ERROR_NONE)
//     {
//         bt_nus_printf("Failed to set network key: %d\n", error);
//     }

//     // Set PAN ID
//     error = otLinkSetPanId(instance, 0x1234);
//     if (error != OT_ERROR_NONE)
//     {
//         bt_nus_printf("Failed to set PAN ID: %d\n", error);
//     }

//     // Set channel
//     error = otLinkSetChannel(instance, 15);
//     if (error != OT_ERROR_NONE)
//     {
//         bt_nus_printf("Failed to set channel: %d\n", error);
//     }

//     // Start Thread operation
//     bt_nus_printf("Starting Thread operation...\n");
//     error = otThreadSetEnabled(instance, true);
//     if (error != OT_ERROR_NONE)
//     {
//         bt_nus_printf("Failed to start Thread: %d\n", error);
//         return;
//     }

//     bt_nus_printf("Thread network initialization started\n");
//     bt_nus_printf("Device will attempt to join network...\n");
// }

/**
 * Display Thread operational dataset information
 */
void display_operational_dataset(void)
{
    struct openthread_context *context = openthread_get_default_context();
    if (!context)
    {
        LOG_ERR("OpenThread context not available");
        bt_nus_printf("OpenThread context not available\n");
        return;
    }

    otInstance *instance = context->instance;
    if (!instance)
    {
        LOG_ERR("OpenThread instance not available");
        bt_nus_printf("OpenThread instance not available\n");
        return;
    }

    LOG_INF("=== Operational Dataset ===");
    bt_nus_printf("=== Operational Dataset ===\n");

    // Get active operational dataset
    otOperationalDataset dataset;
    otError error = otDatasetGetActive(instance, &dataset);

    if (error != OT_ERROR_NONE)
    {
        LOG_ERR("Failed to get active operational dataset: %d", error);
        bt_nus_printf("Failed to get active operational dataset: %d\n", error);
        return;
    }

    // Display network name
    if (dataset.mComponents.mIsNetworkNamePresent)
    {
        // Network name is null-terminated string, calculate length safely
        size_t name_len = strnlen((const char *)dataset.mNetworkName.m8, OT_NETWORK_NAME_MAX_SIZE);
        LOG_INF("Network Name: %.*s", (int)name_len, dataset.mNetworkName.m8);
        bt_nus_printf("Network Name: %.*s\n", (int)name_len, dataset.mNetworkName.m8);
    }
    else
    {
        LOG_INF("Network Name: Not set");
        bt_nus_printf("Network Name: Not set\n");
    }

    // Display extended PAN ID
    if (dataset.mComponents.mIsExtendedPanIdPresent)
    {
        LOG_INF("Extended PAN ID: %02x%02x%02x%02x%02x%02x%02x%02x",
                dataset.mExtendedPanId.m8[0], dataset.mExtendedPanId.m8[1],
                dataset.mExtendedPanId.m8[2], dataset.mExtendedPanId.m8[3],
                dataset.mExtendedPanId.m8[4], dataset.mExtendedPanId.m8[5],
                dataset.mExtendedPanId.m8[6], dataset.mExtendedPanId.m8[7]);
        bt_nus_printf("Extended PAN ID: %02x%02x%02x%02x%02x%02x%02x%02x\n",
                      dataset.mExtendedPanId.m8[0], dataset.mExtendedPanId.m8[1],
                      dataset.mExtendedPanId.m8[2], dataset.mExtendedPanId.m8[3],
                      dataset.mExtendedPanId.m8[4], dataset.mExtendedPanId.m8[5],
                      dataset.mExtendedPanId.m8[6], dataset.mExtendedPanId.m8[7]);
    }

    // Display network key
    if (dataset.mComponents.mIsNetworkKeyPresent)
    {
        LOG_INF("Network Key: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                dataset.mNetworkKey.m8[0], dataset.mNetworkKey.m8[1], dataset.mNetworkKey.m8[2], dataset.mNetworkKey.m8[3],
                dataset.mNetworkKey.m8[4], dataset.mNetworkKey.m8[5], dataset.mNetworkKey.m8[6], dataset.mNetworkKey.m8[7],
                dataset.mNetworkKey.m8[8], dataset.mNetworkKey.m8[9], dataset.mNetworkKey.m8[10], dataset.mNetworkKey.m8[11],
                dataset.mNetworkKey.m8[12], dataset.mNetworkKey.m8[13], dataset.mNetworkKey.m8[14], dataset.mNetworkKey.m8[15]);
        bt_nus_printf("Network Key: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
                      dataset.mNetworkKey.m8[0], dataset.mNetworkKey.m8[1], dataset.mNetworkKey.m8[2], dataset.mNetworkKey.m8[3],
                      dataset.mNetworkKey.m8[4], dataset.mNetworkKey.m8[5], dataset.mNetworkKey.m8[6], dataset.mNetworkKey.m8[7],
                      dataset.mNetworkKey.m8[8], dataset.mNetworkKey.m8[9], dataset.mNetworkKey.m8[10], dataset.mNetworkKey.m8[11],
                      dataset.mNetworkKey.m8[12], dataset.mNetworkKey.m8[13], dataset.mNetworkKey.m8[14], dataset.mNetworkKey.m8[15]);
    }

    // Display mesh local prefix
    if (dataset.mComponents.mIsMeshLocalPrefixPresent)
    {
        char prefix_str[INET6_ADDRSTRLEN];
        struct in6_addr ml_prefix;
        memcpy(&ml_prefix, dataset.mMeshLocalPrefix.m8, 8);
        memset(&ml_prefix.s6_addr[8], 0, 8); // Clear the rest

        if (zsock_inet_ntop(AF_INET6, &ml_prefix, prefix_str, sizeof(prefix_str)))
        {
            LOG_INF("Mesh Local Prefix: %s/64", prefix_str);
            bt_nus_printf("Mesh Local Prefix: %s/64\n", prefix_str);
        }
    }

    // Display PAN ID
    if (dataset.mComponents.mIsPanIdPresent)
    {
        LOG_INF("PAN ID: 0x%04x", dataset.mPanId);
        bt_nus_printf("PAN ID: 0x%04x\n", dataset.mPanId);
    }

    // Display channel
    if (dataset.mComponents.mIsChannelPresent)
    {
        LOG_INF("Channel: %d", dataset.mChannel);
        bt_nus_printf("Channel: %d\n", dataset.mChannel);
    }

    // Display PSKc (Pre-Shared Key for the Commissioner)
    if (dataset.mComponents.mIsPskcPresent)
    {
        LOG_INF("PSKc: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                dataset.mPskc.m8[0], dataset.mPskc.m8[1], dataset.mPskc.m8[2], dataset.mPskc.m8[3],
                dataset.mPskc.m8[4], dataset.mPskc.m8[5], dataset.mPskc.m8[6], dataset.mPskc.m8[7],
                dataset.mPskc.m8[8], dataset.mPskc.m8[9], dataset.mPskc.m8[10], dataset.mPskc.m8[11],
                dataset.mPskc.m8[12], dataset.mPskc.m8[13], dataset.mPskc.m8[14], dataset.mPskc.m8[15]);
        bt_nus_printf("PSKc: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
                      dataset.mPskc.m8[0], dataset.mPskc.m8[1], dataset.mPskc.m8[2], dataset.mPskc.m8[3],
                      dataset.mPskc.m8[4], dataset.mPskc.m8[5], dataset.mPskc.m8[6], dataset.mPskc.m8[7],
                      dataset.mPskc.m8[8], dataset.mPskc.m8[9], dataset.mPskc.m8[10], dataset.mPskc.m8[11],
                      dataset.mPskc.m8[12], dataset.mPskc.m8[13], dataset.mPskc.m8[14], dataset.mPskc.m8[15]);
    }

    // Display security policy
    if (dataset.mComponents.mIsSecurityPolicyPresent)
    {
        LOG_INF("Security Policy:");
        LOG_INF("  Rotation Time: %d hours", dataset.mSecurityPolicy.mRotationTime);
        LOG_INF("  Flags: 0x%02x", dataset.mSecurityPolicy.mObtainNetworkKeyEnabled |
                                       (dataset.mSecurityPolicy.mNativeCommissioningEnabled << 1) |
                                       (dataset.mSecurityPolicy.mRoutersEnabled << 2) |
                                       (dataset.mSecurityPolicy.mExternalCommissioningEnabled << 3) |
                                       (dataset.mSecurityPolicy.mCommercialCommissioningEnabled << 5) |
                                       (dataset.mSecurityPolicy.mAutonomousEnrollmentEnabled << 6) |
                                       (dataset.mSecurityPolicy.mNetworkKeyProvisioningEnabled << 7));

        bt_nus_printf("Security Policy:\n");
        bt_nus_printf("  Rotation Time: %d hours\n", dataset.mSecurityPolicy.mRotationTime);
        bt_nus_printf("  Network Key: %s\n", dataset.mSecurityPolicy.mObtainNetworkKeyEnabled ? "Enabled" : "Disabled");
        bt_nus_printf("  Native Commissioning: %s\n", dataset.mSecurityPolicy.mNativeCommissioningEnabled ? "Enabled" : "Disabled");
        bt_nus_printf("  Routers: %s\n", dataset.mSecurityPolicy.mRoutersEnabled ? "Enabled" : "Disabled");
        bt_nus_printf("  External Commissioning: %s\n", dataset.mSecurityPolicy.mExternalCommissioningEnabled ? "Enabled" : "Disabled");
        bt_nus_printf("  Commercial Commissioning: %s\n", dataset.mSecurityPolicy.mCommercialCommissioningEnabled ? "Enabled" : "Disabled");
    }

    // Display channel mask
    if (dataset.mComponents.mIsChannelMaskPresent)
    {
        LOG_INF("Channel Mask: 0x%08x", dataset.mChannelMask);
        bt_nus_printf("Channel Mask: 0x%08x\n", dataset.mChannelMask);

        // Show available channels
        bt_nus_printf("Available Channels: ");
        for (int i = 11; i <= 26; i++)
        {
            if (dataset.mChannelMask & (1 << i))
            {
                bt_nus_printf("%d ", i);
            }
        }
        bt_nus_printf("\n");
    }

    // Display active timestamp
    if (dataset.mComponents.mIsActiveTimestampPresent)
    {
        LOG_INF("Active Timestamp: %llu.%03u",
                dataset.mActiveTimestamp.mSeconds,
                (dataset.mActiveTimestamp.mTicks * 1000) / 32768);
        bt_nus_printf("Active Timestamp: %llu.%03u\n",
                      dataset.mActiveTimestamp.mSeconds,
                      (dataset.mActiveTimestamp.mTicks * 1000) / 32768);
    }

    // Display pending timestamp
    if (dataset.mComponents.mIsPendingTimestampPresent)
    {
        LOG_INF("Pending Timestamp: %llu.%03u",
                dataset.mPendingTimestamp.mSeconds,
                (dataset.mPendingTimestamp.mTicks * 1000) / 32768);
        bt_nus_printf("Pending Timestamp: %llu.%03u\n",
                      dataset.mPendingTimestamp.mSeconds,
                      (dataset.mPendingTimestamp.mTicks * 1000) / 32768);
    }

    // Display delay timer
    if (dataset.mComponents.mIsDelayPresent)
    {
        LOG_INF("Delay Timer: %u ms", dataset.mDelay);
        bt_nus_printf("Delay Timer: %u ms\n", dataset.mDelay);
    }

    LOG_INF("=== End Operational Dataset ===");
    bt_nus_printf("=== End Operational Dataset ===\n");
}

/**
 * Check Thread network attachment status
 */
void check_thread_status(void)
{
    struct openthread_context *context = openthread_get_default_context();
    if (!context)
    {
        bt_nus_printf("OpenThread context not available\n");
        return;
    }

    otInstance *instance = context->instance;
    if (!instance)
    {
        bt_nus_printf("OpenThread instance not available\n");
        return;
    }

    bt_nus_printf("=== Thread Status ===\n");

    // Device role
    otDeviceRole role = otThreadGetDeviceRole(instance);
    const char *role_str =
        role == OT_DEVICE_ROLE_DISABLED ? "Disabled" : role == OT_DEVICE_ROLE_DETACHED ? "Detached"
                                                   : role == OT_DEVICE_ROLE_CHILD      ? "Child"
                                                   : role == OT_DEVICE_ROLE_ROUTER     ? "Router"
                                                   : role == OT_DEVICE_ROLE_LEADER     ? "Leader"
                                                                                       : "Unknown";

    bt_nus_printf("Device Role: %s\n", role_str);

    // Network state
    if (role != OT_DEVICE_ROLE_DISABLED && role != OT_DEVICE_ROLE_DETACHED)
    {
        const char *networkName = otThreadGetNetworkName(instance);
        bt_nus_printf("Network: %s\n", networkName ? networkName : "Unknown");

        uint16_t panId = otLinkGetPanId(instance);
        bt_nus_printf("PAN ID: 0x%04x\n", panId);

        uint8_t channel = otLinkGetChannel(instance);
        bt_nus_printf("Channel: %d\n", channel);

        // Get our addresses
        const otNetifAddress *addr = otIp6GetUnicastAddresses(instance);
        int addr_count = 0;
        while (addr)
        {
            char addr_str[OT_IP6_ADDRESS_STRING_SIZE];
            otIp6AddressToString(&addr->mAddress, addr_str, sizeof(addr_str));
            bt_nus_printf("Address %d: %s\n", addr_count, addr_str);
            addr = addr->mNext;
            addr_count++;
        }
    }
    else
    {
        bt_nus_printf("Not attached to any network\n");

        // Show why we might not be attached
        bt_nus_printf("Possible reasons:\n");
        bt_nus_printf("1. Thread interface disabled\n");
        bt_nus_printf("2. No network credentials set\n");
        bt_nus_printf("3. No Thread network in range\n");
        bt_nus_printf("4. Network credentials mismatch\n");
    }

    bt_nus_printf("=== End Thread Status ===\n");
}

/**
 * Create a new Thread network (become leader)
 */
// void create_thread_network(void)
// {
//     struct net_if *iface = net_if_get_default();
//     if (!iface)
//     {
//         bt_nus_printf("No network interface available\n");
//         return;
//     }

//     otInstance *instance = net_if_l2_data(iface);
//     if (!instance)
//     {
//         bt_nus_printf("OpenThread instance not available\n");
//         return;
//     }

//     bt_nus_printf("=== Creating Thread Network ===\n");

//     // Enable IPv6 first
//     otError error = otIp6SetEnabled(instance, true);
//     if (error != OT_ERROR_NONE)
//     {
//         bt_nus_printf("Failed to enable IPv6: %d\n", error);
//         return;
//     }

//     // Enable Thread
//     error = otThreadSetEnabled(instance, true);
//     if (error != OT_ERROR_NONE)
//     {
//         bt_nus_printf("Failed to enable Thread: %d\n", error);
//         return;
//     }

//     // Wait a moment for Thread to initialize
//     k_sleep(K_MSEC(500));

//     // Create new operational dataset
//     otOperationalDataset dataset;
//     memset(&dataset, 0, sizeof(dataset));

//     // Set network name
//     const char *networkName = "MyThreadNet";
//     strncpy((char *)dataset.mNetworkName.m8, networkName, OT_NETWORK_NAME_MAX_SIZE);
//     dataset.mComponents.mIsNetworkNamePresent = true;

//     // Set network key
//     const uint8_t key[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
//                            0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
//     memcpy(dataset.mNetworkKey.m8, key, sizeof(dataset.mNetworkKey.m8));
//     dataset.mComponents.mIsNetworkKeyPresent = true;

//     // Set PAN ID
//     dataset.mPanId = 0x1234;
//     dataset.mComponents.mIsPanIdPresent = true;

//     // Set channel
//     dataset.mChannel = 15;
//     dataset.mComponents.mIsChannelPresent = true;

//     // Set mesh local prefix
//     const uint8_t mlPrefix[] = {0xfd, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
//     memcpy(dataset.mMeshLocalPrefix.m8, mlPrefix, sizeof(dataset.mMeshLocalPrefix.m8));
//     dataset.mComponents.mIsMeshLocalPrefixPresent = true;

//     // Set the dataset
//     error = otDatasetSetActive(instance, &dataset);
//     if (error != OT_ERROR_NONE)
//     {
//         bt_nus_printf("Failed to set active dataset: %d\n", error);
//         return;
//     }

//     bt_nus_printf("Dataset configured, starting as Leader...\n");

//     // Start Thread network
//     error = otThreadSetEnabled(instance, true);
//     if (error != OT_ERROR_NONE)
//     {
//         bt_nus_printf("Failed to start Thread: %d\n", error);
//         return;
//     }

//     bt_nus_printf("Thread network creation initiated\n");
//     bt_nus_printf("Device should become Leader shortly\n");
// }

/**
 * Display current DNS configuration
 */
void display_dns_config(void)
{
    struct openthread_context *context = openthread_get_default_context();
    if (!context)
    {
        LOG_ERR("OpenThread context not available");
        bt_nus_printf("OpenThread context not available\n");
        return;
    }

    otInstance *instance = context->instance;
    if (!instance)
    {
        LOG_ERR("OpenThread instance not available");
        bt_nus_printf("OpenThread instance not available\n");
        return;
    }

    LOG_INF("=== DNS Configuration ===");
    bt_nus_printf("=== DNS Configuration ===\n");

    // Get default DNS query configuration
    const otDnsQueryConfig *defaultConfig = otDnsClientGetDefaultConfig(instance);
    if (defaultConfig)
    {
        LOG_INF("Default DNS Configuration:");
        bt_nus_printf("Default DNS Configuration:\n");

                // Display server socket address
        char server_addr_str[INET6_ADDRSTRLEN];
        if (zsock_inet_ntop(AF_INET6, &defaultConfig->mServerSockAddr.mAddress, server_addr_str, sizeof(server_addr_str)))
        {
            LOG_INF("  Server Address: %s", server_addr_str);
            LOG_INF("  Server Port: %u", defaultConfig->mServerSockAddr.mPort);
            bt_nus_printf("  Server Address: %s\n", server_addr_str);
            bt_nus_printf("  Server Port: %u\n", defaultConfig->mServerSockAddr.mPort);
        }

        LOG_INF("  Response Timeout: %u ms", defaultConfig->mResponseTimeout);
        LOG_INF("  Max Tx Attempts: %u", defaultConfig->mMaxTxAttempts);
        LOG_INF("  Recursion Desired: %s", defaultConfig->mRecursionFlag == OT_DNS_FLAG_RECURSION_DESIRED ? "Yes" : "No");

        bt_nus_printf("  Response Timeout: %u ms\n", defaultConfig->mResponseTimeout);
        bt_nus_printf("  Max Tx Attempts: %u\n", defaultConfig->mMaxTxAttempts);
        bt_nus_printf("  Recursion Desired: %s\n", defaultConfig->mRecursionFlag == OT_DNS_FLAG_RECURSION_DESIRED ? "Yes" : "No");

        // Display NAT64 mode
        const char *nat64_mode_str = "Unknown";
        switch (defaultConfig->mNat64Mode)
        {
        case OT_DNS_NAT64_UNSPECIFIED:
            nat64_mode_str = "Unspecified";
            break;
        case OT_DNS_NAT64_ALLOW:
            nat64_mode_str = "Allow";
            break;
        case OT_DNS_NAT64_DISALLOW:
            nat64_mode_str = "Disallow";
            break;
        }

        LOG_INF("  NAT64 Mode: %s", nat64_mode_str);
        bt_nus_printf("  NAT64 Mode: %s\n", nat64_mode_str);

        // Display transport protocol
        const char *transport_str = defaultConfig->mTransportProto == OT_DNS_TRANSPORT_UDP ? "UDP" : "TCP";
        LOG_INF("  Transport Protocol: %s", transport_str);
        bt_nus_printf("  Transport Protocol: %s\n", transport_str);
    }
    else
    {
        LOG_WRN("No default DNS configuration available");
        bt_nus_printf("No default DNS configuration available\n");
    }

    return;

    // Try to get DNS servers from network data
    LOG_INF("--- DNS Servers from Network Data ---");
    bt_nus_printf("--- DNS Servers from Network Data ---\n");

    otNetworkDataIterator iterator = OT_NETWORK_DATA_ITERATOR_INIT;
    otServiceConfig serviceConfig;
    int dns_server_count = 0;
    bool found_dns_service = false;

    while (otNetDataGetNextService(instance, &iterator, &serviceConfig) == OT_ERROR_NONE)
    {
        // Look for DNS service (enterprise number 44970 is used for Thread DNS)
        if (serviceConfig.mEnterpriseNumber == 44970)
        {
            found_dns_service = true;
            LOG_INF("DNS Service found:");
            bt_nus_printf("DNS Service found:\n");

            // Parse service data for DNS server addresses
            if (serviceConfig.mServiceDataLength >= 16) // At least one IPv6 address
            {
                for (uint8_t i = 0; i < serviceConfig.mServiceDataLength; i += 16)
                {
                    if (i + 16 <= serviceConfig.mServiceDataLength)
                    {
                        char dns_server_str[INET6_ADDRSTRLEN];
                        if (zsock_inet_ntop(AF_INET6, &serviceConfig.mServiceData[i], dns_server_str, sizeof(dns_server_str)))
                        {
                            LOG_INF("  DNS Server %d: %s", dns_server_count, dns_server_str);
                            bt_nus_printf("  DNS Server %d: %s\n", dns_server_count, dns_server_str);
                            dns_server_count++;
                        }
                    }
                }
            }

            // Display raw service data
            char service_data_hex[256] = {0};
            for (uint8_t i = 0; i < serviceConfig.mServiceDataLength && i < 64; i++)
            {
                snprintf(service_data_hex + (i * 3), sizeof(service_data_hex) - (i * 3),
                         "%02x ", serviceConfig.mServiceData[i]);
            }
            LOG_INF("  Service Data: %s", service_data_hex);
            bt_nus_printf("  Service Data: %s\n", service_data_hex);
        }
    }

    if (!found_dns_service)
    {
        LOG_INF("No DNS services found in network data");
        bt_nus_printf("No DNS services found in network data\n");
    }

    // Check for DNS servers in border router configurations
    LOG_INF("--- DNS from Border Router Services ---");
    bt_nus_printf("--- DNS from Border Router Services ---\n");

    iterator = OT_NETWORK_DATA_ITERATOR_INIT;
    otServiceConfig brServiceConfig;
    bool found_br_dns = false;

    while (otNetDataGetNextService(instance, &iterator, &brServiceConfig) == OT_ERROR_NONE)
    {
        // Look for any service that might contain DNS information
        if (brServiceConfig.mServiceDataLength > 0)
        {
            // Check if service data contains what looks like IPv6 addresses
            for (uint8_t i = 0; i <= brServiceConfig.mServiceDataLength - 16; i += 16)
            {
                // Basic check: see if it could be an IPv6 address
                bool could_be_ipv6 = false;

                // Check for common IPv6 patterns
                if (brServiceConfig.mServiceData[i] == 0xfe && brServiceConfig.mServiceData[i + 1] == 0x80) // Link-local
                    could_be_ipv6 = true;
                else if (brServiceConfig.mServiceData[i] == 0xfd) // ULA
                    could_be_ipv6 = true;
                else if (brServiceConfig.mServiceData[i] == 0x20 && brServiceConfig.mServiceData[i + 1] == 0x01) // Global unicast
                    could_be_ipv6 = true;

                if (could_be_ipv6)
                {
                    char potential_dns_str[INET6_ADDRSTRLEN];
                    if (zsock_inet_ntop(AF_INET6, &brServiceConfig.mServiceData[i], potential_dns_str, sizeof(potential_dns_str)))
                    {
                        LOG_INF("  Potential DNS Server: %s (Enterprise: %u)", potential_dns_str, brServiceConfig.mEnterpriseNumber);
                        bt_nus_printf("  Potential DNS Server: %s (Enterprise: %u)\n", potential_dns_str, brServiceConfig.mEnterpriseNumber);
                        found_br_dns = true;
                    }
                }
            }
        }
    }

    if (!found_br_dns)
    {
        LOG_INF("No potential DNS servers found in border router services");
        bt_nus_printf("No potential DNS servers found in border router services\n");
    }

    // Display DNS resolver status
    LOG_INF("--- DNS Client Status ---");
    bt_nus_printf("--- DNS Client Status ---\n");

    // Check if DNS client is operational by checking device role
    otDeviceRole role = otThreadGetDeviceRole(instance);
    if (role == OT_DEVICE_ROLE_DISABLED || role == OT_DEVICE_ROLE_DETACHED)
    {
        LOG_WRN("DNS client not operational - device not attached to Thread network");
        bt_nus_printf("DNS client not operational - device not attached to Thread network\n");
    }
    else
    {
        LOG_INF("DNS client operational - device attached to Thread network");
        bt_nus_printf("DNS client operational - device attached to Thread network\n");
    }

    LOG_INF("=== End DNS Configuration ===");
    bt_nus_printf("=== End DNS Configuration ===\n");
}

/**
 * Command to display all network information
 */
void cmd_show_netdata(void)
{

    //   init_thread_network();
    //   k_sleep(K_MSEC(1000));
    display_openthread_netdata();
    k_sleep(K_MSEC(500));
    display_thread_topology();
    k_sleep(K_MSEC(500));
    display_operational_dataset();
    k_sleep(K_MSEC(500));
    display_dns_config();
    k_sleep(K_MSEC(500));
    display_raw_netdata();
    k_sleep(K_MSEC(500));
    find_nat64_prefixes();
    k_sleep(K_MSEC(500));
    get_netdata_routes();
    k_sleep(K_MSEC(500));
    check_thread_status();
}