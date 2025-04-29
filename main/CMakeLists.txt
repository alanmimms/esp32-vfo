idf_component_register(SRCS
			main.c
			si5351.c
			console_settings.c
			cmd_freq.c

                       PRIV_REQUIRES esp_event nvs_flash esp_wifi
		       		     esp_http_server dns_server
		       		     console nvs_flash fatfs cmd_i2c
		       		     cmd_nvs cmd_system cmd_wifi
		       		     spi_flash driver esp_driver_gpio
		       		     esp_wifi mdns
				     esp_driver_i2c

                       EMBED_FILES root.html
		       INCLUDE_DIRS ".")
