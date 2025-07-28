/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <ram_pwrdn.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/net/coap.h>
#include <nrfx.h> // For NRF_FICR access
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include "coap_client_utils.h"
#include "dns_utils.h"
#include "net_utils.h"

#if CONFIG_BT_NUS
#include "ble_utils.h"
#endif

LOG_MODULE_REGISTER(coap_client, CONFIG_COAP_CLIENT_LOG_LEVEL);

#if CONFIG_BT_NUS

#define COMMAND_REQUEST_UNICAST 'u'
#define COMMAND_REQUEST_MULTICAST 'm'
#define COMMAND_REQUEST_PROVISIONING 'p'
#define COMMAND_REQUEST_TIME 't'
#define COMMAND_REQUEST_DNS 'd'
#define COMMAND_REQUEST_NETDATA 'i'			   // Add this line
#define COMMAND_REQUEST_TIME_FROM_RESOLVED 'r' // Request time from resolved address
#define COMMAND_REQUEST_CPU_ID 'c'			   // Print unique CPU ID and MAC address
#define COMMAND_REQUEST_TOGGLE_MODE 's'		   // Toggle SED/MED mode
#define COMMAND_REQUEST_DATASET 'o'			   // Display operational dataset

#define CONFIG_COAP_SAMPLE_SERVER_HOSTNAME "srv-ss.vibromatika.by"

static void on_nus_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
	LOG_INF("Received data: %c", data[0]);

	switch (*data)
	{
	case COMMAND_REQUEST_UNICAST:
		coap_client_toggle_one_light();
		break;

	case COMMAND_REQUEST_MULTICAST:
		coap_client_toggle_mesh_lights();
		break;

	case COMMAND_REQUEST_PROVISIONING:
		coap_client_send_provisioning_request();
		break;

	case COMMAND_REQUEST_TIME:
	{
		coap_client_get_time();
	}
	break;

	case COMMAND_REQUEST_DNS:
	{
		// Example hostname, replace with actual server hostname
		const char *hostname = CONFIG_COAP_SAMPLE_SERVER_HOSTNAME;
		LOG_INF("Starting DNS resolution for: %s", hostname);
		bt_nus_printf("Starting DNS resolution for: %s\n", hostname);
		coap_client_resolve_hostname(hostname);
	}
	break;

	case COMMAND_REQUEST_NETDATA: // Add this case
	{
		LOG_INF("Displaying OpenThread Network Data");
		cmd_show_netdata();
	}
	break;

	case COMMAND_REQUEST_TIME_FROM_RESOLVED: // Request time from resolved address
	{
		LOG_INF("Requesting time from resolved address");
		bt_nus_printf("Requesting time from resolved address\n");

		// Check if we have a resolved address
		if (coap_client_is_address_resolved())
		{
			struct sockaddr_in6 server_addr;
			int result = coap_client_get_resolved_address(&server_addr);

			if (result == 0)
			{
				// We have a valid resolved address, request time from it
				LOG_INF("Using resolved address for time request");
				bt_nus_printf("Using resolved address for time request\n");

				// Call the time request function with the resolved address
				coap_client_get_time_from_address(&server_addr);
			}
			else
			{
				LOG_ERR("Failed to get resolved address: %d", result);
				bt_nus_printf("Failed to get resolved address: %d\n", result);
			}
		}
		else
		{
			LOG_WRN("No resolved address available. Use 'd' command to resolve DNS first");
			bt_nus_printf("No resolved address available. Use 'd' command to resolve DNS first\n");
		}
	}
	break;

	case COMMAND_REQUEST_CPU_ID: // Print unique CPU ID
	{
		LOG_INF("Printing unique CPU ID and MAC address");
		bt_nus_printf("Printing unique CPU ID and MAC address\n");

		// Get the unique device ID from hardware
		uint32_t cpu_id[2]; // Nordic chips typically have 64-bit unique ID

// Read the unique device ID registers
// For nRF52/nRF53 series, the unique ID is in FICR (Factory Information Configuration Registers)
#if defined(NRF_FICR_S) // nRF53 series
		cpu_id[0] = NRF_FICR_S->DEVICEID[0];
		cpu_id[1] = NRF_FICR_S->DEVICEID[1];
#elif defined(NRF_FICR) // nRF52 series and others
		cpu_id[0] = NRF_FICR->DEVICEID[0];
		cpu_id[1] = NRF_FICR->DEVICEID[1];
#else
		// Fallback - try to read from memory mapped addresses
		cpu_id[0] = *(volatile uint32_t *)0x10000060; // DEVICEID[0]
		cpu_id[1] = *(volatile uint32_t *)0x10000064; // DEVICEID[1]
#endif

		LOG_INF("CPU ID: 0x%08X%08X", cpu_id[1], cpu_id[0]);
		bt_nus_printf("=== Device Information ===\n");
		bt_nus_printf("CPU ID: 0x%08X%08X\n", cpu_id[1], cpu_id[0]);

		// Also print in separate parts for easier reading
		bt_nus_printf("CPU ID (High): 0x%08X\n", cpu_id[1]);
		bt_nus_printf("CPU ID (Low):  0x%08X\n", cpu_id[0]);

		// Get MAC address information
		struct net_if *iface = net_if_get_default();
		if (iface != NULL)
		{
			struct net_linkaddr *link_addr = net_if_get_link_addr(iface);
			if (link_addr && link_addr->addr && link_addr->len >= 6)
			{
				bt_nus_printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
							  link_addr->addr[0], link_addr->addr[1], link_addr->addr[2],
							  link_addr->addr[3], link_addr->addr[4], link_addr->addr[5]);
				LOG_INF("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
						link_addr->addr[0], link_addr->addr[1], link_addr->addr[2],
						link_addr->addr[3], link_addr->addr[4], link_addr->addr[5]);
			}
			else
			{
				bt_nus_printf("MAC Address: Not available or invalid length\n");
				LOG_WRN("MAC Address: Not available or invalid length");
			}

			// Print interface information
			bt_nus_printf("Interface: %s\n", iface->if_dev->dev->name);
			bt_nus_printf("Interface Index: %d\n", net_if_get_by_iface(iface));
		}
		else
		{
			bt_nus_printf("MAC Address: Network interface not available\n");
			LOG_WRN("Network interface not available");
		}

// Print additional chip information if available
#if defined(NRF_FICR_S)
		bt_nus_printf("Chip: nRF53 series (FICR_S registers)\n");
#elif defined(NRF_FICR)
		bt_nus_printf("Chip: nRF52 series (FICR registers)\n");
		// Print part and variant info if available
		if (NRF_FICR->INFO.PART != 0xFFFFFFFF)
		{
			bt_nus_printf("Part: 0x%08X, Variant: 0x%08X\n",
						  NRF_FICR->INFO.PART, NRF_FICR->INFO.VARIANT);
		}
#else
		bt_nus_printf("Chip: Unknown (memory-mapped fallback)\n");
#endif
		bt_nus_printf("========================\n");
	}
	break;

	case COMMAND_REQUEST_TOGGLE_MODE: // Toggle SED/MED mode
	{
		LOG_INF("Toggling SED/MED mode");
		bt_nus_printf("Toggling SED/MED mode\n");
		coap_client_toggle_minimal_sleepy_end_device();
	}
	break;

	case COMMAND_REQUEST_DATASET: // Display operational dataset
	{
		LOG_INF("Displaying operational dataset");
		bt_nus_printf("Displaying operational dataset\n");
		display_operational_dataset();
	}
	break;

	default:
		LOG_WRN("Received invalid data from NUS");
	}
}

static void on_ble_connect(struct k_work *item)
{
	ARG_UNUSED(item);
}

static void on_ble_disconnect(struct k_work *item)
{
	ARG_UNUSED(item);
}

#endif /* CONFIG_BT_NUS */

static void on_ot_connect(struct k_work *item)
{
	ARG_UNUSED(item);

	bt_nus_printf("OpenThread disconnected\n");
}

static void on_ot_disconnect(struct k_work *item)
{
	ARG_UNUSED(item);

	bt_nus_printf("OpenThread disconnected\n");
}

static void on_mtd_mode_toggle(uint32_t med)
{
	// #if IS_ENABLED(CONFIG_PM_DEVICE) && DT_HAS_CHOSEN(zephyr_console)
	// 	const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	// 	if (!device_is_ready(cons))
	// 	{
	// 		return;
	// 	}

	// 	if (med)
	// 	{
	// 		pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);
	// 	}
	// 	else
	// 	{
	// 		pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
	// 	}
	// #else
	// 	ARG_UNUSED(med);
	// 	// Console device not available or PM not enabled
	// #endif
}

// DNS resolution callback

int main(void)
{
	int ret;

	LOG_INF("Start CoAP-client sample");

	if (IS_ENABLED(CONFIG_RAM_POWER_DOWN_LIBRARY))
	{
		power_down_unused_ram();
	}

#if CONFIG_BT_NUS
	struct bt_nus_cb nus_clbs = {
		.received = on_nus_received,
		.sent = NULL,
	};

	ret = ble_utils_init(&nus_clbs, on_ble_connect, on_ble_disconnect);
	if (ret)
	{
		LOG_ERR("Cannot init BLE utilities");
		return 0;
	}

#endif /* CONFIG_BT_NUS */

	coap_client_utils_init(on_ot_connect, on_ot_disconnect, on_mtd_mode_toggle);

	// Initialize DNS utilities
	dns_utils_init();

	LOG_INF("Available BLE commands:");
	LOG_INF("  'u' - Toggle unicast light");
	LOG_INF("  'm' - Toggle multicast lights");
	LOG_INF("  'p' - Send provisioning request");
	LOG_INF("  't' - Get time from default server");
	LOG_INF("  'd' - Resolve DNS for hostname");
	LOG_INF("  'r' - Request time from resolved address");
	LOG_INF("  'i' - Show network data");
	LOG_INF("  'c' - Print unique CPU ID and MAC address");
	LOG_INF("  's' - Toggle SED/MED mode");
	LOG_INF("  'o' - Display operational dataset");

	bt_nus_printf("CoAP Client started. Available commands:\n");
	bt_nus_printf("  'u' - Toggle unicast light\n");
	bt_nus_printf("  'm' - Toggle multicast lights\n");
	bt_nus_printf("  'p' - Send provisioning request\n");
	bt_nus_printf("  't' - Get time from default server\n");
	bt_nus_printf("  'd' - Resolve DNS for hostname\n");
	bt_nus_printf("  'r' - Request time from resolved address\n");
	bt_nus_printf("  'i' - Show network data\n");
	bt_nus_printf("  'c' - Print unique CPU ID and MAC address\n");
	bt_nus_printf("  's' - Toggle SED/MED mode\n");
	bt_nus_printf("  'o' - Display operational dataset\n");

	return 0;
}

// Function to resolve hostname
