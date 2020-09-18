idf_component_register(SRCS ./main.cpp
                       REQUIRES nvs_flash sip_client)

#INCLUDE_DIRS include                       # Edit following two lines to set component requirements (see docs)
#                       REQUIRES )
#                       PRIV_REQUIRES )

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)
