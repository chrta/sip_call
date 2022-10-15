idf_component_register(INCLUDE_DIRS "include"
		       REQUIRES esp_http_server json app_update
		       PRIV_REQUIRES sip_client
		       EMBED_FILES index.html
                      )

