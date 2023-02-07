idf_component_register(SRCS "sequencerGrid.c" "systemEvents.c" "system.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos nvs_flash ipsDisplay rotaryEncoders 
                    guiMenu switchMatrix ledDrivers 
                    fileSys bleCentralClient midiHelper genericMacros
                    genericDLL)

