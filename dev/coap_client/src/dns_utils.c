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
#include <errno.h>
#include <string.h>
#include <stdio.h>

// Include your BLE utilities for bt_nus_printf
#include "ble_utils.h"

#define CONFIG_COAP_SAMPLE_SERVER_PORT 5683

LOG_MODULE_REGISTER(dns_utils, CONFIG_LOG_DEFAULT_LEVEL);

// DNS resolution work structure
static struct k_work dns_resolve_work;
static char target_hostname[64];
static struct sockaddr_in6 resolved_addr;

// DNS resolution result callback
typedef void (*dns_resolve_callback_t)(int result, struct sockaddr_in6 *addr);

/**
 * Convert IPv4 address to IPv6 using configurable NAT64 prefix
 * @param ipv4_addr Pointer to IPv4 address (4 bytes)
 * @param ipv6_addr Pointer to store the resulting IPv6 address
 * @param nat64_prefix Pointer to NAT64 prefix (12 bytes for /96 prefix)
 * @return true on success, false on failure
 */
bool COMM_getIpv6Address_NAT64(uint8_t *ipv4_addr, struct in6_addr *ipv6_addr,
                               uint8_t *nat64_prefix)
{
    if (!ipv4_addr || !ipv6_addr)
    {
        LOG_ERR("Invalid parameters for NAT64 synthesis");
        return false;
    }

    // Clear the IPv6 address structure
    memset(ipv6_addr, 0, sizeof(struct in6_addr));

    if (nat64_prefix)
    {
        // Use custom NAT64 prefix (first 12 bytes = /96 prefix)
        memcpy(ipv6_addr->s6_addr, nat64_prefix, 12);
    }
    else
    {
        // Use RFC 6052 Well-Known Prefix: 64:ff9b::/96
        ipv6_addr->s6_addr[0] = 0x00;
        ipv6_addr->s6_addr[1] = 0x64;
        ipv6_addr->s6_addr[2] = 0xff;
        ipv6_addr->s6_addr[3] = 0x9b;
        // Bytes 4-11 remain zero
    }

    // Append IPv4 address to the last 4 bytes
    memcpy(&ipv6_addr->s6_addr[12], ipv4_addr, 4);

    return true;
}

// DNS callback for Zephyr DNS resolver
static void zephyr_dns_callback(enum dns_resolve_status status,
                                struct dns_addrinfo *info, void *user_data)
{
    const char *hostname = (const char *)user_data;

    if (status == DNS_EAI_INPROGRESS && info)
    {
        if (info->ai_family == AF_INET6)
        {
            char addr_str[INET6_ADDRSTRLEN];

            // Copy the resolved address directly - no intermediate pointer needed
            struct sockaddr_ *addr_ptr = (struct sockaddr_ *)(&(info->ai_addr));
            memcpy(&resolved_addr, addr_ptr, sizeof(struct sockaddr));
            resolved_addr.sin6_port = htons(CONFIG_COAP_SAMPLE_SERVER_PORT);

            if (zsock_inet_ntop(AF_INET6, &resolved_addr.sin6_addr, addr_str, sizeof(addr_str)))
            {
                LOG_INF("DNS resolved %s to IPv6: %s", hostname, addr_str);
                bt_nus_printf("DNS resolved %s to IPv6: %s \n", hostname, addr_str);
            }
        }
        else if (info->ai_family == AF_INET)
        {
            char addr_str_ipv4[INET_ADDRSTRLEN];
            char addr_str_ipv6[INET6_ADDRSTRLEN];

            // First, properly extract the IPv4 address
            struct sockaddr_in *ipv4_addr_ptr = (struct sockaddr_in *)(&(info->ai_addr));

            // Convert IPv4 address to string for logging
            if (zsock_inet_ntop(AF_INET, &ipv4_addr_ptr->sin_addr, addr_str_ipv4, sizeof(addr_str_ipv4)))
            {
                LOG_INF("DNS resolved %s to IPv4: %s", hostname, addr_str_ipv4);
                bt_nus_printf("DNS resolved %s to IPv4: %s\n", hostname, addr_str_ipv4);
            }

            // get nat64 prefix from dns response
            uint8_t nat_prefix[12] = {0x00, 0x64, 0xff, 0x9b, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00}; // Default NAT64 prefix

            // Convert IPv4 to synthetic IPv6 using NAT64 prefix
            struct in6_addr ipv6_addr;

            // Use the standard NAT64 Well-Known Prefix
            if (COMM_getIpv6Address_NAT64((uint8_t *)&ipv4_addr_ptr->sin_addr, &ipv6_addr,
                                          nat_prefix))
            {
                // Set up the IPv6 sockaddr structure
                memset(&resolved_addr, 0, sizeof(resolved_addr));
                resolved_addr.sin6_family = AF_INET6;
                resolved_addr.sin6_port = htons(CONFIG_COAP_SAMPLE_SERVER_PORT);
                memcpy(&resolved_addr.sin6_addr, &ipv6_addr, sizeof(struct in6_addr));

                if (zsock_inet_ntop(AF_INET6, &ipv6_addr, addr_str_ipv6, sizeof(addr_str_ipv6)))
                {
                    LOG_INF("NAT64 synthesis: IPv4 %s -> IPv6 %s", addr_str_ipv4, addr_str_ipv6);
                    bt_nus_printf("NAT64 synthesis: IPv4 %s -> IPv6 %s \n", addr_str_ipv4, addr_str_ipv6);
                }
            }
            else
            {
                LOG_ERR("Failed to convert IPv4 to IPv6");
                bt_nus_printf("Failed to convert IPv4 to IPv6 \n");
            }
        }
        else if (status == DNS_EAI_ALLDONE)
        {
            LOG_INF("DNS resolution complete for %s", hostname);
            bt_nus_printf("DNS resolution complete for %s\n", hostname);
        }
        else if (status == DNS_EAI_FAIL)
        {
            LOG_ERR("DNS resolution failed for %s", hostname);
            bt_nus_printf("DNS resolution failed for %s\n", hostname);
        }
        else if (status == DNS_EAI_CANCELED)
        {
            LOG_WRN("DNS resolution cancelled or timed out for %s", hostname);
            bt_nus_printf("DNS resolution cancelled or timed out for %s\n", hostname);
        }
    }
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

    // Submit work to resolve DNS
    k_work_submit(&dns_resolve_work);
}
/**
 * Initialize DNS utilities and context
 */
void dns_utils_init(void)
{
    // init dns with ciustom servers

    k_work_init(&dns_resolve_work, dns_resolve_work_handler);

    LOG_INF("DNS utilities initialized with context and servers");
    bt_nus_printf("DNS utilities initialized with context and servers\n");
}