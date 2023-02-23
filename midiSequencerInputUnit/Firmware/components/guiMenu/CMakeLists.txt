idf_component_register(SRCS "menu.c" "private/menuData.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos nvs_flash ipsDisplay rotaryEncoders fileSys)
