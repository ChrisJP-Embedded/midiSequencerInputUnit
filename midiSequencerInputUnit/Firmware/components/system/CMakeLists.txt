idf_component_register(SRCS "systemEvents.c" "sequencerGrid.c" "system.c"
                    INCLUDE_DIRS "include"
<<<<<<< HEAD
                    REQUIRES freertos nvs_flash ipsDisplay rotaryEncoders 
                    guiMenu switchMatrix ledDrivers 
                    fileSys bleCentralClient midi)
=======
                    REQUIRES freertos nvs_flash IPSDisplayDriver RotaryEncoders 
                    systemMenu switchMatrix IPSDisplayDriver LP5862LedDriver 
                    fileSys bleCentralClient)
>>>>>>> 2e307e5 (Started to plumb in bt central (gattServer) using Ning NimBLE stack - ongoing)
