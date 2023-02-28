idf_component_register(SRCS "bleCentral.c" "peer.c"
                    INCLUDE_DIRS "include"
                    REQUIRES bt freertos nvs_flash)

