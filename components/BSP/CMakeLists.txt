set(src_dirs
            IIC
            SPIFFS
            LCD
            LED
            SPI
            FIRMWARE_STORAGE
            JSONPRO
            SDIO
            OBS
            WEB
            XL9555)

set(include_dirs
            IIC
            SPIFFS
            FIRMWARE_STORAGE    
            FIRMWARE_STORAGE/include
            LCD
            LED
            SPI
            JSONPRO
            SDIO   
            OBS
            WEB
            XL9555)

set(requires
            driver
            json
            fatfs
            spiffs
            nvs_flash
            mbedtls
            app_update
            esp_https_ota
            esp_http_client
            esp_http_server
            esp_system)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires} )

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)