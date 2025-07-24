
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(coap_client)

# NORDIC SDK APP START
target_sources(app PRIVATE src/coap_client.c
			   src/coap_client_utils.c
			   src/dns_utils.c
			   src/net_utils.c
			   src/ble_utils.c)

target_include_directories(app PUBLIC coap_server/interface
			   inc
			   src)


			   # NORDIC SDK APP END
