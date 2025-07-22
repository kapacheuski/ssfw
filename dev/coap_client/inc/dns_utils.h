#ifndef DNS_UTILS_H
#define DNS_UTILS_H

#include <zephyr/net/socket.h>

/**
 * DNS resolution result callback
 * @param result 0 on success, negative error code on failure
 * @param addr Resolved IPv6 address (NULL on failure)
 */
typedef void (*dns_resolve_callback_t)(int result, struct sockaddr_in6 *addr);

/**
 * Initialize DNS utilities
 */
void dns_utils_init(void);

/**
 * Start DNS resolution for a hostname (asynchronous)
 * @param hostname The hostname to resolve
 * @param callback Callback function to call when resolution completes
 * @return 0 on success, negative error code on failure
 */
int dns_resolve_async(const char *hostname, dns_resolve_callback_t callback);

/**
 * Start DNS resolution for the default server hostname
 * @param callback Callback function to call when resolution completes
 * @return 0 on success, negative error code on failure
 */
int dns_resolve_default_server(dns_resolve_callback_t callback);

/**
 * Check if DNS resolution is complete
 * @return true if complete, false if still in progress
 */
bool dns_is_resolution_complete(void);

/**
 * Get the resolved address (only valid after successful resolution)
 * @param addr Pointer to store the resolved address
 * @return 0 on success, negative error code if not resolved
 */
int dns_get_resolved_address(struct sockaddr_in6 *addr);

/**
 * Synchronous DNS resolution (blocks until complete)
 * @param hostname The hostname to resolve
 * @param addr Pointer to store the resolved address
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, negative error code on failure
 */
int dns_resolve_sync(const char *hostname, struct sockaddr_in6 *addr, int timeout_ms);

void coap_client_resolve_hostname(const char *hostname);

#endif // DNS_UTILS_H