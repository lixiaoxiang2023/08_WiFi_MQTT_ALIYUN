set(src_dirs
            IIC
            LCD
            LED
            SPI
            FIRMWARE_STORAGE
            JSONPRO
            SDIO
            OBS
            WEB
            XL9555
            LVGL)

set(include_dirs
            IIC
            FIRMWARE_STORAGE    
            FIRMWARE_STORAGE/include
            LCD
            LED
            SPI
            JSONPRO
            SDIO   
            OBS
            WEB
            XL9555
            LVGL)

set(requires
            driver
            json
            fatfs
            spiffs
            nvs_flash
            mbedtls
            vfs            
            app_update
            esp_https_ota
            esp_http_client
            esp_http_server
            esp_system
            esp_wifi             
            esp_event
            lvgl
            esp_lvgl_port)          

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires} )

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)