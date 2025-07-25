/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <coap_server_client_interface.h>
#include <net/coap_utils.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/openthread.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/net_ip.h>
#include <openthread/thread.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/bluetooth/services/nus.h>
#include <stdio.h>
#include "coap_client_utils.h"
int bt_nus_printf(const char *fmt, ...);

LOG_MODULE_REGISTER(coap_client_utils, CONFIG_COAP_CLIENT_UTILS_LOG_LEVEL);

#define RESPONSE_POLL_PERIOD 100

static uint32_t poll_period;

bool thread_is_connected;

#define COAP_CLIENT_WORKQ_STACK_SIZE 2048
#define COAP_CLIENT_WORKQ_PRIORITY 5

K_THREAD_STACK_DEFINE(coap_client_workq_stack_area, COAP_CLIENT_WORKQ_STACK_SIZE);
static struct k_work_q coap_client_workq;

static struct k_work unicast_light_work;
static struct k_work multicast_light_work;
static struct k_work toggle_MTD_SED_work;
static struct k_work set_med_mode_work;
static struct k_work provisioning_work;
static struct k_work coap_get_time_work;
static struct k_work on_connect_work;
static struct k_work on_disconnect_work;
static struct k_work coap_get_time_from_address_work;

// Static variable to store target address for time request
static struct sockaddr_in6 target_time_server_addr;

mtd_mode_toggle_cb_t on_mtd_mode_toggle;

/* Options supported by the server */
static const char *const light_option[] = {LIGHT_URI_PATH, NULL};
static const char *const provisioning_option[] = {PROVISIONING_URI_PATH,
												  NULL};

/* Thread multicast mesh local address */
static struct sockaddr_in6 multicast_local_addr = {
	.sin6_family = AF_INET6,
	.sin6_port = htons(COAP_PORT),
	.sin6_addr.s6_addr = {0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
	.sin6_scope_id = 0U};
// Send request to fd15:7938:9154:b17:0:ff:fe00:fc10
static struct sockaddr_in6 coap_server_addr = {
	.sin6_family = AF_INET6,
	.sin6_port = htons(COAP_PORT),
	.sin6_addr.s6_addr = {
		0xfd, 0x15, 0x79, 0x38, 0x91, 0x54, 0x0b, 0x17,
		0x00, 0x00, 0x00, 0xff, 0xfe, 0x00, 0xfc, 0x10},
	.sin6_scope_id = 0U};
/* Variable for storing server address acquiring in provisioning handshake */
static char unique_local_addr_str[INET6_ADDRSTRLEN];
static struct sockaddr_in6 unique_local_addr = {
	.sin6_family = AF_INET6,
	.sin6_port = htons(COAP_PORT),
	.sin6_addr.s6_addr = {
		0,
	},
	.sin6_scope_id = 0U};

typedef struct
{
	uint64_t timestamp;
	int16_t acc[3];
	int16_t gyr[3];
	double temperature;
	double voltage; // Port number
} _ss_data_payload;

static _ss_data_payload ss_data_payload = {
	.timestamp = 0,
	.acc = {0, 0, 0},
	.gyr = {0, 0, 0},
	.temperature = 0.0f,
	.voltage = 0.0f};

static bool is_mtd_in_med_mode(otInstance *instance)
{
	return otThreadGetLinkMode(instance).mRxOnWhenIdle;
}
static char str[256] = "";
static void poll_period_response_set(void)
{
	otError error;

	otInstance *instance = openthread_get_default_instance();

	if (is_mtd_in_med_mode(instance))
	{
		return;
	}

	if (!poll_period)
	{
		poll_period = otLinkGetPollPeriod(instance);

		error = otLinkSetPollPeriod(instance, RESPONSE_POLL_PERIOD);
		__ASSERT(error == OT_ERROR_NONE, "Failed to set pool period");

		LOG_INF("Poll Period: %dms set", RESPONSE_POLL_PERIOD);
	}
}

static void poll_period_restore(void)
{
	otError error;
	otInstance *instance = openthread_get_default_instance();

	if (is_mtd_in_med_mode(instance))
	{
		return;
	}

	if (poll_period)
	{
		error = otLinkSetPollPeriod(instance, poll_period);
		__ASSERT_NO_MSG(error == OT_ERROR_NONE);

		LOG_INF("Poll Period: %dms restored", poll_period);
		poll_period = 0;
	}
}

static int on_provisioning_reply(const struct coap_packet *response,
								 struct coap_reply *reply,
								 const struct sockaddr *from)
{
	int ret = 0;
	const uint8_t *payload;
	uint16_t payload_size = 0u;

	ARG_UNUSED(reply);
	ARG_UNUSED(from);

	payload = coap_packet_get_payload(response, &payload_size);

	if (payload == NULL ||
		payload_size != sizeof(unique_local_addr.sin6_addr))
	{
		LOG_ERR("Received data is invalid");
		ret = -EINVAL;
		goto exit;
	}

	memcpy(&unique_local_addr.sin6_addr, payload, payload_size);

	if (!zsock_inet_ntop(AF_INET6, payload, unique_local_addr_str,
						 INET6_ADDRSTRLEN))
	{
		LOG_ERR("Received data is not IPv6 address: %d", errno);
		ret = -errno;
		goto exit;
	}

	LOG_INF("Received peer address: %s", unique_local_addr_str);

exit:
	if (IS_ENABLED(CONFIG_OPENTHREAD_MTD_SED))
	{
		poll_period_restore();
	}

	return ret;
}
static int on_time_reply(const struct coap_packet *response,
						 struct coap_reply *reply,
						 const struct sockaddr *from)
{
	int ret = 0;
	const uint8_t *payload;

	uint16_t payload_size = 0u;

	ARG_UNUSED(reply);
	ARG_UNUSED(from);

	payload = coap_packet_get_payload(response, &payload_size);
	// copy payload to str
	if (payload == NULL || (payload_size + 1) >= sizeof(str))
	{
		LOG_ERR("Received data is invalid");
		bt_nus_printf("Received data is invalid");
		ret = -EINVAL;
		goto exit;
	}
	memcpy(str, payload, payload_size);
	str[payload_size] = '\0'; // Null-terminate the string
							  //	bt_nus_printf("Payload:%s", str);

	LOG_INF("Received peer address: %s", unique_local_addr_str);
	bt_nus_printf("Received peer address: %s\nPayload:%s\n", unique_local_addr_str, str);

exit:
	if (IS_ENABLED(CONFIG_OPENTHREAD_MTD_SED))
	{
		poll_period_restore();
	}

	return ret;
}
static void toggle_one_light(struct k_work *item)
{
	uint8_t payload = (uint8_t)THREAD_COAP_UTILS_LIGHT_CMD_TOGGLE;

	ARG_UNUSED(item);

	if (unique_local_addr.sin6_addr.s6_addr16[0] == 0)
	{
		LOG_WRN("Peer address not set. Activate 'provisioning' option "
				"on the server side");
		return;
	}

	LOG_INF("Send 'light' request to: %s", unique_local_addr_str);
	coap_send_request(COAP_METHOD_PUT,
					  (const struct sockaddr *)&unique_local_addr,
					  light_option, &payload, sizeof(payload), NULL);
}

static void toggle_mesh_lights(struct k_work *item)
{
	static uint8_t command = (uint8_t)THREAD_COAP_UTILS_LIGHT_CMD_OFF;

	ARG_UNUSED(item);

	command = ((command == THREAD_COAP_UTILS_LIGHT_CMD_OFF) ? THREAD_COAP_UTILS_LIGHT_CMD_ON : THREAD_COAP_UTILS_LIGHT_CMD_OFF);

	LOG_INF("Send multicast mesh 'light' request");
	coap_send_request(COAP_METHOD_PUT,
					  (const struct sockaddr *)&multicast_local_addr,
					  light_option, &command, sizeof(command), NULL);
}

static void send_provisioning_request(struct k_work *item)
{
	ARG_UNUSED(item);

	if (IS_ENABLED(CONFIG_OPENTHREAD_MTD_SED))
	{
		/* decrease the polling period for higher responsiveness */
		poll_period_response_set();
	}

	LOG_INF("Send 'provisioning' request");
	coap_send_request(COAP_METHOD_GET,
					  (const struct sockaddr *)&multicast_local_addr,
					  provisioning_option, NULL, 0u, on_provisioning_reply);
}

static void coap_get_time(struct k_work *item)
{
	char payload[256];
	// Serialize ss_data_payload to a json string
	int ret = snprintf(payload, sizeof(payload),
					   "{\"timestamp\":%llu,\"acc\":[%d,%d,%d],"
					   "\"gyr\":[%d,%d,%d],\"temperature\":%.2f,\"voltage\":%.2f}",
					   ss_data_payload.timestamp,
					   ss_data_payload.acc[0], ss_data_payload.acc[1], ss_data_payload.acc[2],
					   ss_data_payload.gyr[0], ss_data_payload.gyr[1], ss_data_payload.gyr[2],
					   ss_data_payload.temperature, ss_data_payload.voltage);

	// const char *server_ip = "178.172.137.254"; // Example IPv4 address
	//  send the payload to the server
	if (ret < 0 || ret >= sizeof(payload))
	{
		LOG_ERR("Failed to format payload");
		return;
	}
	// add path "measurements" to server request
	const char *const path[] = {"time", NULL};

	ret = coap_send_request(COAP_METHOD_GET,
							(const struct sockaddr *)&coap_server_addr,
							path, NULL, 0, &on_time_reply);
	if (ret < 0)
	{
		bt_nus_printf("Failed to send CoAP request: %d", ret);
		return;
	};
	bt_nus_printf("Request sended !\n");
}

static void coap_get_time_from_address(struct k_work *item)
{
	ARG_UNUSED(item);

	char payload[256];

	// Serialize ss_data_payload to a json string
	int ret = snprintf(payload, sizeof(payload),
					   "{\"timestamp\":%llu,\"acc\":[%d,%d,%d],"
					   "\"gyr\":[%d,%d,%d],\"temperature\":%.2f,\"voltage\":%.2f}",
					   ss_data_payload.timestamp,
					   ss_data_payload.acc[0], ss_data_payload.acc[1], ss_data_payload.acc[2],
					   ss_data_payload.gyr[0], ss_data_payload.gyr[1], ss_data_payload.gyr[2],
					   ss_data_payload.temperature, ss_data_payload.voltage);

	if (ret < 0 || ret >= sizeof(payload))
	{
		LOG_ERR("Failed to format payload");
		bt_nus_printf("Failed to format payload\n");
		return;
	}

	// Add path "time" to server request
	const char *const path[] = {"time", NULL};

	ret = coap_send_request(COAP_METHOD_GET,
							(const struct sockaddr *)&target_time_server_addr,
							path, NULL, 0, &on_time_reply);
	if (ret < 0)
	{
		bt_nus_printf("Failed to send CoAP request to resolved address: %d\n", ret);
		return;
	}

	char addr_str[INET6_ADDRSTRLEN];
	if (zsock_inet_ntop(AF_INET6, &target_time_server_addr.sin6_addr, addr_str, sizeof(addr_str)))
	{
		bt_nus_printf("Time request sent to resolved address: %s\n", addr_str);
	}
	else
	{
		bt_nus_printf("Time request sent to resolved address\n");
	}
}

static void toggle_minimal_sleepy_end_device(struct k_work *item)
{
	otError error;
	otLinkModeConfig mode;
	struct openthread_context *context = openthread_get_default_context();

	__ASSERT_NO_MSG(context != NULL);

	openthread_api_mutex_lock(context);
	mode = otThreadGetLinkMode(context->instance);

	LOG_INF("Current mode before toggle: %s", mode.mRxOnWhenIdle ? "MED" : "SED");
	bt_nus_printf("Current mode before toggle: %s\n", mode.mRxOnWhenIdle ? "MED" : "SED");

	mode.mRxOnWhenIdle = !mode.mRxOnWhenIdle;
	error = otThreadSetLinkMode(context->instance, mode);
	openthread_api_mutex_unlock(context);

	if (error != OT_ERROR_NONE)
	{
		LOG_ERR("Failed to set MLE link mode configuration");
	}
	else
	{
		LOG_INF("Mode toggled to: %s", mode.mRxOnWhenIdle ? "MED" : "SED");
		bt_nus_printf("Mode toggled to: %s\n", mode.mRxOnWhenIdle ? "MED" : "SED");
		on_mtd_mode_toggle(mode.mRxOnWhenIdle);
	}
}

static void update_device_state(void)
{
	struct otInstance *instance = openthread_get_default_instance();
	otLinkModeConfig mode = otThreadGetLinkMode(instance);
	on_mtd_mode_toggle(mode.mRxOnWhenIdle);
}

static void set_device_to_med_mode(void)
{
	otError error;
	otLinkModeConfig mode;
	struct openthread_context *context = openthread_get_default_context();

	if (context == NULL)
	{
		LOG_ERR("OpenThread context not available");
		return;
	}

	openthread_api_mutex_lock(context);
	mode = otThreadGetLinkMode(context->instance);

	// Set to MED mode (Minimal End Device) - RxOnWhenIdle = true
	mode.mRxOnWhenIdle = true;

	error = otThreadSetLinkMode(context->instance, mode);
	openthread_api_mutex_unlock(context);

	if (error != OT_ERROR_NONE)
	{
		LOG_ERR("Failed to set device to MED mode: %d", error);
	}
	else
	{
		LOG_INF("Device initialized in MED (Minimal End Device) mode");
		bt_nus_printf("Device initialized in MED (Minimal End Device) mode\n");
		on_mtd_mode_toggle(mode.mRxOnWhenIdle);
	}
}

static void set_med_mode_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	// Add a small delay to ensure OpenThread is fully initialized
	k_sleep(K_MSEC(1000));
	set_device_to_med_mode();
}

static void on_thread_state_changed(otChangedFlags flags, struct openthread_context *ot_context,
									void *user_data)
{
	if (flags & OT_CHANGED_THREAD_ROLE)
	{
		switch (otThreadGetDeviceRole(ot_context->instance))
		{
		case OT_DEVICE_ROLE_CHILD:
		case OT_DEVICE_ROLE_ROUTER:
		case OT_DEVICE_ROLE_LEADER:
			k_work_submit_to_queue(&coap_client_workq, &on_connect_work);
			thread_is_connected = true;
			break;

		case OT_DEVICE_ROLE_DISABLED:
		case OT_DEVICE_ROLE_DETACHED:
		default:
			k_work_submit_to_queue(&coap_client_workq, &on_disconnect_work);
			thread_is_connected = false;
			break;
		}
	}
}
static struct openthread_state_changed_cb ot_state_chaged_cb = {
	.state_changed_cb = on_thread_state_changed};

static void submit_work_if_connected(struct k_work *work)
{
	if (thread_is_connected)
	{
		k_work_submit_to_queue(&coap_client_workq, work);
	}
	else
	{
		LOG_INF("Connection is broken");
	}
}

void coap_client_utils_init(ot_connection_cb_t on_connect,
							ot_disconnection_cb_t on_disconnect,
							mtd_mode_toggle_cb_t on_toggle)
{
	on_mtd_mode_toggle = on_toggle;

	coap_init(AF_INET6, NULL);

	k_work_queue_init(&coap_client_workq);

	k_work_queue_start(&coap_client_workq, coap_client_workq_stack_area,
					   K_THREAD_STACK_SIZEOF(coap_client_workq_stack_area),
					   COAP_CLIENT_WORKQ_PRIORITY, NULL);

	k_work_init(&on_connect_work, on_connect);
	k_work_init(&on_disconnect_work, on_disconnect);
	k_work_init(&unicast_light_work, toggle_one_light);
	k_work_init(&multicast_light_work, toggle_mesh_lights);
	k_work_init(&provisioning_work, send_provisioning_request);
	k_work_init(&coap_get_time_work, coap_get_time);
	k_work_init(&coap_get_time_from_address_work, coap_get_time_from_address);
	k_work_init(&set_med_mode_work, set_med_mode_work_handler);

	openthread_state_changed_cb_register(openthread_get_default_context(), &ot_state_chaged_cb);
	openthread_start(openthread_get_default_context());

	if (IS_ENABLED(CONFIG_OPENTHREAD_MTD_SED))
	{
		k_work_init(&toggle_MTD_SED_work,
					toggle_minimal_sleepy_end_device);
		// Initialize device in MED mode instead of default SED mode
		k_work_submit(&set_med_mode_work);
	}
}

void coap_client_toggle_one_light(void)
{
	submit_work_if_connected(&unicast_light_work);
}

void coap_client_toggle_mesh_lights(void)
{
	submit_work_if_connected(&multicast_light_work);
}

void coap_client_send_provisioning_request(void)
{
	submit_work_if_connected(&provisioning_work);
}
void coap_client_get_time()
{
	submit_work_if_connected(&coap_get_time_work);
}

void coap_client_get_time_from_address(const struct sockaddr_in6 *server_addr)
{
	if (!server_addr)
	{
		LOG_ERR("Invalid server address");
		bt_nus_printf("Invalid server address\n");
		return;
	}

	// Copy the target address to our static variable
	memcpy(&target_time_server_addr, server_addr, sizeof(struct sockaddr_in6));

	// Submit the work item
	submit_work_if_connected(&coap_get_time_from_address_work);
}

void coap_client_toggle_minimal_sleepy_end_device(void)
{
	if (IS_ENABLED(CONFIG_OPENTHREAD_MTD_SED))
	{
		k_work_submit_to_queue(&coap_client_workq, &toggle_MTD_SED_work);
	}
}
