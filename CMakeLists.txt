zephyr_library()
zephyr_include_directories(include)

zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_MODULE src/input_module.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_MODULE src/events/input_module_state.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_MODULE_SENSOR_PROXY
	src/input_module_sensor_proxy.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_MODULE_KSCAN_PROXY
	src/input_module_kscan_proxy.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_MODULE_INPUT_PROXY
	src/input_module_input_proxy.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_MODULE_SELECT_BEHAVIOR
	src/behavior_input_module_select.c)

if(CONFIG_ZMK_INPUT_MODULE_STUDIO_RPC)
	target_sources(app PRIVATE src/studio/input_module_handler.c)

	list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
	include(nanopb)
	set(NANOPB_GENERATE_CPP_APPEND_PATH TRUE)
	set(NANOPB_GENERATE_CPP_STANDALONE OFF)

	set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/proto/dya/input_module/input_module.proto)

	nanopb_generate_cpp(proto_srcs proto_hdrs RELPATH ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_FILES})
	target_include_directories(${ZEPHYR_CURRENT_LIBRARY} PUBLIC
		${CMAKE_CURRENT_BINARY_DIR}
		${NANOPB_INCLUDE_DIRS}
	)
	target_sources(${ZEPHYR_CURRENT_LIBRARY} PRIVATE ${proto_srcs} ${proto_hdrs})

	target_include_directories(app PUBLIC
		${CMAKE_CURRENT_BINARY_DIR}/proto
		${NANOPB_INCLUDE_DIRS}
	)
	add_dependencies(app ${ZEPHYR_CURRENT_LIBRARY})
endif()
