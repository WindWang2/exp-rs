add_executable(sicnu_geo_rs
    main.cpp
)

target_include_directories(sicnu_geo_rs PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_geo_rs PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Xml
    Qt6::Svg
    Qt6::Sql
    qgis_core
    qgis_gui
    qgis_native
    sicnu_core
    sicnu_gui
    CURL::libcurl
    ${PCRE2_16}
    ${PCRE2_8}
)

set_target_properties(sicnu_geo_rs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    OUTPUT_NAME "sicnu_geo_rs"
)

install(TARGETS sicnu_geo_rs
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)
