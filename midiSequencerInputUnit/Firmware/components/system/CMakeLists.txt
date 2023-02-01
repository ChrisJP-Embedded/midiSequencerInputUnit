idf_component_register(SRCS "systemEvents.c" "sequencerGrid.c" "system.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos nvs_flash ipsDisplay rotaryEncoders 
                    guiMenu switchMatrix ledDrivers 
                    fileSys bleCentralClient midi)

