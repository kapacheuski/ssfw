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
#include <zephyr/net/openthread.h> // Add this for openthread_get_default_context

#define CONFIG_COAP_SAMPLE_SERVER_PORT 5683

LOG_MODULE_REGISTER(dns_utils, CONFIG_LOG_DEFAULT_LEVEL);

// DNS resolution work structure
static struct k_work dns_resolve_work;
static struct k_work dns_result_work; // New work for handling DNS results
static char target_hostname[64];
static struct sockaddr_in6 resolved_addr;

extern bool thread_is_connected;

// DNS result data structure
struct dns_result_data
{
    enum dns_resolve_status status;
    int ai_family;
    union
    {
        struct sockaddr_in ipv4_addr;
        struct sockaddr_in6 ipv6_addr;
    } addr;
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

// DNS result work handler - processes DNS results safely in work queue context
static void dns_result_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    const char *hostname = dns_result.hostname;
    enum dns_resolve_status status = dns_result.status;

    if (status == DNS_EAI_INPROGRESS)
    {
        if (dns_result.ai_family == AF_INET6)
        {
            char addr_str[INET6_ADDRSTRLEN];

            // Copy the resolved address
            memcpy(&resolved_addr, &dns_result.addr.ipv6_addr, sizeof(struct sockaddr_in6));
            resolved_addr.sin6_port = htons(CONFIG_COAP_SAMPLE_SERVER_PORT);

            if (zsock_inet_ntop(AF_INET6, &resolved_addr.sin6_addr, addr_str, sizeof(addr_str)))
            {
                LOG_INF("DNS resolved %s to IPv6: %s", hostname, addr_str);
                bt_nus_printf("DNS resolved %s to IPv6: %s\n", hostname, addr_str);
                address_resolved = true; // Mark address as resolved
            }
        }
        else if (dns_result.ai_family == AF_INET)
        {
            char addr_str_ipv4[INET_ADDRSTRLEN];
            char addr_str_ipv6[INET6_ADDRSTRLEN];

            // Convert IPv4 address to string for logging
            if (zsock_inet_ntop(AF_INET, &dns_result.addr.ipv4_addr.sin_addr, addr_str_ipv4, sizeof(addr_str_ipv4)))
            {
                LOG_INF("DNS resolved %s to IPv4: %s", hostname, addr_str_ipv4);
                bt_nus_printf("DNS resolved %s to IPv4: %s\n", hostname, addr_str_ipv4);
            }

            // Convert IPv4 to synthetic IPv6 using OpenThread NAT64
            struct in6_addr ipv6_addr;

            if (convert_ipv4_to_ipv6_nat64((uint8_t *)&dns_result.addr.ipv4_addr.sin_addr, &ipv6_addr))
            {
                // Set up the IPv6 sockaddr structure
                memset(&resolved_addr, 0, sizeof(resolved_addr));
                resolved_addr.sin6_family = AF_INET6;
                resolved_addr.sin6_port = htons(CONFIG_COAP_SAMPLE_SERVER_PORT);
                memcpy(&resolved_addr.sin6_addr, &ipv6_addr, sizeof(struct in6_addr));

                if (zsock_inet_ntop(AF_INET6, &ipv6_addr, addr_str_ipv6, sizeof(addr_str_ipv6)))
                {
                    LOG_INF("OpenThread NAT64 synthesis: IPv4 %s -> IPv6 %s", addr_str_ipv4, addr_str_ipv6);
                    bt_nus_printf("OpenThread NAT64 synthesis: IPv4 %s -> IPv6 %s\n", addr_str_ipv4, addr_str_ipv6);
                    address_resolved = true; // Mark address as resolved
                }
            }
            else
            {
                LOG_ERR("Failed to convert IPv4 to IPv6 using OpenThread NAT64");
                bt_nus_printf("Failed to convert IPv4 to IPv6 using OpenThread NAT64\n");
                address_resolved = false; // Mark resolution as failed
            }
        }
    }
    else if (status == DNS_EAI_ALLDONE)
    {
        LOG_INF("DNS resolution complete for %s", hostname);
        bt_nus_printf("DNS resolution complete for %s\n", hostname);
        // Note: address_resolved flag should already be set if successful
    }
    else if (status == DNS_EAI_FAIL)
    {
        LOG_ERR("DNS resolution failed for %s", hostname);
        bt_nus_printf("DNS resolution failed for %s\n", hostname);
        address_resolved = false; // Mark resolution as failed
    }
    else if (status == DNS_EAI_CANCELED)
    {
        LOG_WRN("DNS resolution cancelled or timed out for %s", hostname);
        bt_nus_printf("DNS resolution cancelled or timed out for %s\n", hostname);
        address_resolved = false; // Mark resolution as failed
    }
}

// DNS callback for Zephyr DNS resolver - minimal implementation
static void zephyr_dns_callback(enum dns_resolve_status status,
                                struct dns_addrinfo *info, void *user_data)
{
    const char *hostname = (const char *)user_data;

    // Simple acknowledgment that we're in the callback
    // Don't use bt_nus_printf here as it might cause issues in callback context

    // Store result data for processing in work queue
    dns_result.status = status;
    strncpy(dns_result.hostname, hostname, sizeof(dns_result.hostname) - 1);
    dns_result.hostname[sizeof(dns_result.hostname) - 1] = '\0';

    if (status == DNS_EAI_INPROGRESS && info)
    {
        dns_result.ai_family = info->ai_family;

        if (info->ai_family == AF_INET6)
        {
            // Copy IPv6 address
            struct sockaddr_in6 *addr_ptr = (struct sockaddr_in6 *)(&(info->ai_addr));
            memcpy(&dns_result.addr.ipv6_addr, addr_ptr, sizeof(struct sockaddr_in6));
        }
        else if (info->ai_family == AF_INET)
        {
            // Copy IPv4 address
            struct sockaddr_in *ipv4_addr_ptr = (struct sockaddr_in *)(&info->ai_addr);
            memcpy(&dns_result.addr.ipv4_addr, ipv4_addr_ptr, sizeof(struct sockaddr_in));
        }
    }

    // Submit work to process the result safely
    k_work_submit(&dns_result_work);
}

/**
 * DNS resolution work handler using Zephyr DNS resolver
 * This runs in a work queue context to avoid blocking
 */
static void dns_resolve_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    uint16_t dns_id;
    int ret;

    LOG_INF("Starting DNS resolution for: %s", target_hostname);

    // Validate hostname
    if (strlen(target_hostname) == 0)
    {
        LOG_ERR("Empty hostname");
        bt_nus_printf("Empty hostname\n");
        return;
    }

    // Use Zephyr DNS resolver with proper timeout
    ret = dns_get_addr_info(target_hostname,
                            DNS_QUERY_TYPE_A, // IPv4 query
                            &dns_id,
                            zephyr_dns_callback,
                            (void *)target_hostname,
                            10000); // Use K_SECONDS macro

    if (ret < 0)
    {
        LOG_ERR("Cannot start DNS resolution for %s (%d)", target_hostname, ret);
        bt_nus_printf("Cannot start DNS resolution for %s (%d)\n", target_hostname, ret);

        // Print more detailed error info
        switch (ret)
        {
        case -EINVAL:
            bt_nus_printf("Invalid DNS parameters - check hostname format\n");
            break;
        case -ENOTSUP:
            bt_nus_printf("DNS resolution not supported\n");
            break;
        case -ENOMEM:
            bt_nus_printf("Out of memory for DNS resolution\n");
            break;
        case -EAGAIN:
            bt_nus_printf("DNS resolver busy, try again later\n");
            break;
        case -ENODEV:
            bt_nus_printf("DNS context not active - check network connection\n");
            break;
        default:
            bt_nus_printf("DNS error code: %d", ret);
            break;
        }

        return;
    }

    LOG_DBG("DNS id %u", dns_id);
    bt_nus_printf("DNS resolution started for %s with ID: %d \n", target_hostname, dns_id);
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
 * Initialize DNS utilities and context
 */
void dns_utils_init(void)
{
    // Initialize work items
    k_work_init(&dns_resolve_work, dns_resolve_work_handler);
    k_work_init(&dns_result_work, dns_result_work_handler);

    LOG_INF("DNS utilities initialized with context and servers");
    bt_nus_printf("DNS utilities initialized with context and servers\n");
}