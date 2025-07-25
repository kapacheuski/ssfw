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
    size_t nameLength;
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

        // Try to check if NAT64 translator is enabled locally
        bool nat64Enabled = false;

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

    uint8_t data[256];
    uint8_t length = sizeof(data);

    otError error = otNetDataGet(instance, false, data, &length);
    if (error == OT_ERROR_NONE)
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
    length = sizeof(data);
    error = otNetDataGet(instance, true, data, &length);
    if (error == OT_ERROR_NONE)
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
    display_raw_netdata();
    k_sleep(K_MSEC(500));
    find_nat64_prefixes();
    k_sleep(K_MSEC(500));
    get_netdata_routes();
    k_sleep(K_MSEC(500));
    check_thread_status();
}