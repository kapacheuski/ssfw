/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/sys/printk.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

// Include your BLE utilities for bt_nus_printf
#include "ble_utils.h"

// Add these includes with your other includes
#include <openthread/nat64.h>
#include <openthread/instance.h>
#include <openthread/thread.h>
#include <openthread/netdata.h>    // Add this for otNetDataGetNat64Prefix
#include <openthread/dns_client.h> // Add this for DNS client functions
#include <zephyr/net/openthread.h> // Add this for openthread_get_default_context

#define CONFIG_COAP_SAMPLE_SERVER_PORT 5683

LOG_MODULE_REGISTER(dns_utils, CONFIG_LOG_DEFAULT_LEVEL);

// DNS resolution work structure
static struct k_work dns_resolve_work;
static struct k_work dns_result_work; // New work for handling DNS results
static char target_hostname[64];
static struct sockaddr_in6 resolved_addr;

extern bool thread_is_connected;

// DNS result data structure for OpenThread
struct dns_result_data
{
    otError error;
    otIp6Address ipv6_address;
    uint32_t ttl;
    char hostname[64];
};

static struct dns_result_data dns_result;

// Flag to track if we have a valid resolved address
static bool address_resolved = false;

// DNS resolution result callback
typedef void (*dns_resolve_callback_t)(int result, struct sockaddr_in6 *addr);

/**
 * Convert IPv4 address to IPv6 using OpenThread NAT64 synthesis
 * @param ipv4_addr Pointer to IPv4 address (4 bytes)
 * @param ipv6_addr Pointer to store the resulting IPv6 address
 * @return true on success, false on failure
 */
bool convert_ipv4_to_ipv6_nat64(uint8_t *ipv4_addr, struct in6_addr *ipv6_addr)
{
    char ipv4_str[INET_ADDRSTRLEN];
    char ipv6_str[INET6_ADDRSTRLEN];
    if (!ipv4_addr || !ipv6_addr)
    {
        LOG_ERR("Invalid parameters for NAT64 synthesis");
        bt_nus_printf("Invalid parameters for NAT64 synthesis\n");
        return false;
    }

    // Get OpenThread instance
    struct net_if *iface = net_if_get_default();
    if (!iface)
    {
        LOG_ERR("No network interface available");
        bt_nus_printf("No network interface available\n");
        return false;
    }
    if (thread_is_connected)
    {
        bt_nus_printf("Device is connected\n");
    }
    else
    {
        bt_nus_printf("Device is not connected\n");
    }

    struct openthread_context *context = openthread_get_default_context();
    if (!context || !context->instance)
    {
        LOG_ERR("OpenThread context or instance not available");
        bt_nus_printf("OpenThread context or instance not available\n");
        return false;
    }
    // Check if Thread is attached before attempting NAT64
    otDeviceRole role = otThreadGetDeviceRole(context->instance);
    if (role == OT_DEVICE_ROLE_DISABLED || role == OT_DEVICE_ROLE_DETACHED)
    {
        LOG_WRN("OpenThread not attached to network (role: %d),", role);
        bt_nus_printf("OpenThread not attached to network (role: %d),\n", role);
        return false;
    }

    // Prepare OpenThread types
    otIp4Address otIpv4Addr;
    otIp6Address otIpv6Addr;

    // Copy IPv4 address to OpenThread format
    memcpy(&otIpv4Addr, ipv4_addr, sizeof(otIp4Address));

    // Use OpenThread NAT64 synthesis
    otError error = otNat64SynthesizeIp6Address(context->instance, &otIpv4Addr, &otIpv6Addr);
    if (error != OT_ERROR_NONE)
    {
        LOG_WRN("OpenThread NAT64 synthesis failed: %d", error);
        bt_nus_printf("OpenThread NAT64 synthesis failed: %d\n", error);
        return false;
    }

    // Copy result back to standard IPv6 format
    memcpy(ipv6_addr, &otIpv6Addr, sizeof(struct in6_addr));

    if (zsock_inet_ntop(AF_INET, ipv4_addr, ipv4_str, sizeof(ipv4_str)) &&
        zsock_inet_ntop(AF_INET6, ipv6_addr, ipv6_str, sizeof(ipv6_str)))
    {
        LOG_INF("OpenThread NAT64 success: %s -> %s", ipv4_str, ipv6_str);
        bt_nus_printf("OpenThread NAT64 success: %s -> %s\n", ipv4_str, ipv6_str);
    }

    return true;
}

// DNS result work handler - processes OpenThread DNS results safely in work queue context
static void dns_result_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    const char *hostname = dns_result.hostname;
    otError error = dns_result.error;

    if (error == OT_ERROR_NONE)
    {
        char addr_str_ipv4[INET_ADDRSTRLEN];
        char addr_str_ipv6[INET6_ADDRSTRLEN];

        // Convert OpenThread IPv4 address to string for logging
        if (zsock_inet_ntop(AF_INET6, &dns_result.ipv6_address, addr_str_ipv6, sizeof(addr_str_ipv6)))
        {
            LOG_INF("OpenThread DNS resolved %s to IPv6: %s (TTL: %u)", hostname, addr_str_ipv6, dns_result.ttl);
            bt_nus_printf("OpenThread DNS resolved %s to IPv6: %s (TTL: %u)\n", hostname, addr_str_ipv6, dns_result.ttl);
        }

        memset(&resolved_addr, 0, sizeof(resolved_addr));
        resolved_addr.sin6_family = AF_INET6;
        resolved_addr.sin6_port = htons(CONFIG_COAP_SAMPLE_SERVER_PORT);
        memcpy(&resolved_addr.sin6_addr, &dns_result.ipv6_address, sizeof(struct in6_addr));
        address_resolved = true; // Mark address as resolved

        // // Convert IPv4 to synthetic IPv6 using OpenThread NAT64
        // struct in6_addr ipv6_addr;

        // if (convert_ipv4_to_ipv6_nat64((uint8_t *)&dns_result.ipv6_address, &ipv6_addr))
        // {
        //     // Set up the IPv6 sockaddr structure
        //     memset(&resolved_addr, 0, sizeof(resolved_addr));
        //     resolved_addr.sin6_family = AF_INET6;
        //     resolved_addr.sin6_port = htons(CONFIG_COAP_SAMPLE_SERVER_PORT);
        //     memcpy(&resolved_addr.sin6_addr, &ipv6_addr, sizeof(struct in6_addr));

        //     if (zsock_inet_ntop(AF_INET6, &ipv6_addr, addr_str_ipv6, sizeof(addr_str_ipv6)))
        //     {
        //         LOG_INF("OpenThread NAT64 synthesis: IPv4 %s -> IPv6 %s", addr_str_ipv4, addr_str_ipv6);
        //         bt_nus_printf("OpenThread NAT64 synthesis: IPv4 %s -> IPv6 %s\n", addr_str_ipv4, addr_str_ipv6);
        //         address_resolved = true; // Mark address as resolved
        //     }
        // }
        // else
        // {
        //     LOG_ERR("Failed to convert IPv4 to IPv6 using OpenThread NAT64");
        //     bt_nus_printf("Failed to convert IPv4 to IPv6 using OpenThread NAT64\n");
        //     address_resolved = false; // Mark resolution as failed
        // }
    }
    else
    {
        LOG_ERR("OpenThread DNS resolution failed for %s: error %d", hostname, error);
        bt_nus_printf("OpenThread DNS resolution failed for %s: error %d\n", hostname, error);
        address_resolved = false; // Mark resolution as failed
    }
}

// OpenThread DNS callback - minimal implementation
static void openthread_dns_callback(otError aError, const otDnsAddressResponse *aResponse, void *aContext)
{
    const char *hostname = (const char *)aContext;

    // Store result data for processing in work queue
    dns_result.error = aError;
    strncpy(dns_result.hostname, hostname, sizeof(dns_result.hostname) - 1);
    dns_result.hostname[sizeof(dns_result.hostname) - 1] = '\0';

    if (aError == OT_ERROR_NONE && aResponse)
    {

        // Get the first IPv4 address from the response
        otIp6Address ipv6Address;
        uint32_t ttl;

        if (otDnsAddressResponseGetAddress(aResponse, 0, &ipv6Address, &ttl) == OT_ERROR_NONE)
        {
            memcpy(&dns_result.ipv6_address, &ipv6Address, sizeof(otIp6Address));
            dns_result.ttl = ttl;
        }
    }

    // Submit work to process the result safely
    k_work_submit(&dns_result_work);
}

/**
 * DNS resolution work handler using OpenThread DNS client
 * This runs in a work queue context to avoid blocking
 */
static void dns_resolve_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    LOG_INF("Starting OpenThread DNS resolution for: %s", target_hostname);

    // Validate hostname
    if (strlen(target_hostname) == 0)
    {
        LOG_ERR("Empty hostname");
        bt_nus_printf("Empty hostname\n");
        return;
    }

    // Get OpenThread instance
    struct openthread_context *context = openthread_get_default_context();
    if (!context || !context->instance)
    {
        LOG_ERR("OpenThread context or instance not available");
        bt_nus_printf("OpenThread context or instance not available\n");
        return;
    }

    // Check if Thread is attached before attempting DNS resolution
    otDeviceRole role = otThreadGetDeviceRole(context->instance);
    if (role == OT_DEVICE_ROLE_DISABLED || role == OT_DEVICE_ROLE_DETACHED)
    {
        LOG_ERR("OpenThread not attached to network (role: %d), cannot resolve DNS", role);
        bt_nus_printf("OpenThread not attached to network (role: %d), cannot resolve DNS\n", role);
        return;
    }

    //     typedef struct otDnsQueryConfig
    // {
    //     otSockAddr          mServerSockAddr;  ///< Server address (IPv6 addr/port). All zero or zero port for unspecified.
    //     uint32_t            mResponseTimeout; ///< Wait time (in msec) to rx response. Zero indicates unspecified value.
    //     uint8_t             mMaxTxAttempts;   ///< Maximum tx attempts before reporting failure. Zero for unspecified value.
    //     otDnsRecursionFlag  mRecursionFlag;   ///< Indicates whether the server can resolve the query recursively or not.
    //     otDnsNat64Mode      mNat64Mode;       ///< Allow/Disallow NAT64 address translation during address resolution.
    //     otDnsServiceMode    mServiceMode;     ///< Determines which records to query during service resolution.
    //     otDnsTransportProto mTransportProto;  ///< Select default transport protocol.
    // } otDnsQueryConfig;

    const otDnsQueryConfig *config = otDnsClientGetDefaultConfig(context->instance);
    // otDnsQueryConfig config = {
    //     .mServerSockAddr = {0}, // Use default server address
    //     .mResponseTimeout = 5000, // 5 seconds timeout
    //     .mMaxTxAttempts = 3, // 3 attempts
    //     .mRecursionFlag = OT_DNS_REC_FLAG_UNSET, // Default recursion flag
    //     .mNat64Mode = OT_DNS_NAT64_UNSPECIFIED, // Use default NAT64 mode
    //     .mServiceMode = OT_DNS_SERVICE_MODE_UNSPECIFIED, // Use default service mode
    //     .mTransportProto = OT_DNS_TRANSPORT_UNSPECIFIED // Use default transport protocol

    // }

    // Use OpenThread DNS client for IPv4 resolution
    otError error = otDnsClientResolveIp4Address(context->instance,
                                                 target_hostname,
                                                 openthread_dns_callback,
                                                 (void *)target_hostname,
                                                 config); // Use default DNS config

    if (error != OT_ERROR_NONE)
    {
        LOG_ERR("Cannot start OpenThread DNS resolution for %s (error: %d)", target_hostname, error);
        bt_nus_printf("Cannot start OpenThread DNS resolution for %s (error: %d)\n", target_hostname, error);

        // Print more detailed error info based on OpenThread error codes
        switch (error)
        {
        case OT_ERROR_INVALID_ARGS:
            bt_nus_printf("Invalid DNS parameters - check hostname format\n");
            break;
        case OT_ERROR_NO_BUFS:
            bt_nus_printf("Out of memory for DNS resolution\n");
            break;
        case OT_ERROR_BUSY:
            bt_nus_printf("DNS resolver busy, try again later\n");
            break;
        case OT_ERROR_INVALID_STATE:
            bt_nus_printf("OpenThread not in correct state for DNS resolution\n");
            break;
        default:
            bt_nus_printf("OpenThread DNS error code: %d\n", error);
            break;
        }

        return;
    }

    LOG_INF("OpenThread DNS resolution started for %s", target_hostname);
    bt_nus_printf("OpenThread DNS resolution started for %s\n", target_hostname);
}

/**
 * Function to resolve hostname and print the result
 * This is a utility function to be called from other parts of the code
 */
void coap_client_resolve_hostname(const char *hostname)
{

    if (!hostname || strlen(hostname) >= sizeof(target_hostname))
    {
        LOG_ERR("Invalid hostname or too long: %s", hostname);
        bt_nus_printf("Invalid hostname or too long: %s\n", hostname);
        return; // Invalid hostname or too long
    }

    // Copy hostname and set callback
    strncpy(target_hostname, hostname, sizeof(target_hostname) - 1);
    target_hostname[sizeof(target_hostname) - 1] = '\0';

    // Reset the resolved address flag before starting new resolution
    address_resolved = false;

    // Submit work to resolve DNS
    k_work_submit(&dns_resolve_work);
}

/**
 * Get the resolved address if available
 * @param addr Pointer to store the resolved IPv6 address
 * @return 0 on success (address available), negative error code on failure
 */
int coap_client_get_resolved_address(struct sockaddr_in6 *addr)
{
    if (!addr)
    {
        LOG_ERR("Invalid address pointer");
        bt_nus_printf("Invalid address pointer\n");
        return -EINVAL;
    }

    if (!address_resolved)
    {
        LOG_WRN("No resolved address available");
        bt_nus_printf("No resolved address available\n");
        return -ENOENT; // No such entry
    }

    // Copy the resolved address
    memcpy(addr, &resolved_addr, sizeof(struct sockaddr_in6));

    LOG_INF("Returning resolved address");
    bt_nus_printf("Returning resolved address\n");

    return 0;
}

/**
 * Check if an address has been resolved
 * @return true if address is available, false otherwise
 */
bool coap_client_is_address_resolved(void)
{
    return address_resolved;
}

/**
 * Clear the resolved address (useful for starting fresh)
 */
void coap_client_clear_resolved_address(void)
{
    address_resolved = false;
    memset(&resolved_addr, 0, sizeof(resolved_addr));
    LOG_INF("Cleared resolved address");
    bt_nus_printf("Cleared resolved address\n");
}
/**
 * Initialize DNS utilities with OpenThread DNS client
 */
void dns_utils_init(void)
{
    // Initialize work items
    k_work_init(&dns_resolve_work, dns_resolve_work_handler);
    k_work_init(&dns_result_work, dns_result_work_handler);

    LOG_INF("DNS utilities initialized with OpenThread DNS client");
    bt_nus_printf("DNS utilities initialized with OpenThread DNS client\n");
}