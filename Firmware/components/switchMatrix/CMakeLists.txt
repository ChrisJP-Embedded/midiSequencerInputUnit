idf_component_register(SRCS "switchMatrix.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos
                    PRIV_REQUIRES ledDrivers driver)
