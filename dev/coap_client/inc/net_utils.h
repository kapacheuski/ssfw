#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>

/**
 * Display OpenThread Network Data
 */
void display_openthread_netdata(void);

/**
 * Display network data in raw format
 */
void display_raw_netdata(void);

/**
 * Display Thread network topology information
 */
void display_thread_topology(void);

/**
 * Command to display all network information
 */
void cmd_show_netdata(void);

#endif /* NET_UTILS_H */