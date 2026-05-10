idf_component_register(
    SRC_DIRS 
        "."
        "APP"
    INCLUDE_DIRS 
        "."
        "APP")
target_compile_definitions(${COMPONENT_LIB} PRIVATE LCD_1_47INCHL)