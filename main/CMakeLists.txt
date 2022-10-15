idf_component_register(SRCS ./main.cpp
                       REQUIRES driver esp_netif esp_wifi mbedtls
		                nvs_flash sip_client web_server)

#INCLUDE_DIRS include                       # Edit following two lines to set component requirements (see docs)
#                       REQUIRES )
#                       PRIV_REQUIRES )

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)
