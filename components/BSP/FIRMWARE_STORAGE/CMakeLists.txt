idf_component_register(
    SRCS "firmware_storage.c"
    INCLUDE_DIRS "include"   # 注意：这是相对于组件目录的路径
    REQUIRES spiffs mbedtls
)