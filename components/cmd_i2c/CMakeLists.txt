idf_component_register(SRCS "cmd_i2c.c"
                    INCLUDE_DIRS .
		    PRIV_REQUIRES esp_driver_i2c console)
