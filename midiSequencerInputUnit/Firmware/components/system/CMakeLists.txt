idf_component_register(SRCS "system.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos nvs_flash ipsDisplay rotaryEncoders 
                    guiMenu fileSys bleCentralClient midiHelper genericMacros switchMatrix)

