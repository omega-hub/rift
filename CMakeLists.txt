# Set the module name here
set(MODULE_NAME rift)

set(RIFT_EMULATE false CACHE BOOL "When set to true, compiles the rift module without using the OVR library")
if(RIFT_EMULATE)
	add_definitions(-DRIFT_EMULATE)
endif()

if(NOT RIFT_EMULATE)
	# Oculus SDK include and library
	set(OVR_INCLUDE_DIR CACHE PATH "Oculus SDK include directory")
	set(OVR_LIB CACHE PATH "Oculus SDK library")
	include_directories(${OVR_INCLUDE_DIR})
endif()

if(APPLE)
	find_library(APP_SERVICES_LIBRARY ApplicationServices)
	find_library(CORE_FOUNDATION_LIBRARY CoreFoundation)
	find_library(IOKIT_LIBRARY IOKit)
	mark_as_advanced(APP_SERVICES_LIBRARY CORE_FOUNDATION_LIBRARY IOKIT_LIBRARY)
	set(OVR_EXTRA_LIBS ${APP_SERVICES_LIBRARY} ${CORE_FOUNDATION_LIBRARY} ${IOKIT_LIBRARY} ${COCOA_LIBRARY})
endif (APPLE)
	   
	# Set module name and source files here
add_library(${MODULE_NAME} MODULE 
	rift.cpp)

# Set the module library dependencies here
target_link_libraries(${MODULE_NAME}
	omega)

if(NOT RIFT_EMULATE)
	target_link_libraries(${MODULE_NAME}
		${OVR_LIB} ${OVR_EXTRA_LIBS})
		
	if(WIN32)
		# Needed for time functions (timeGetTime etc.)
		target_link_libraries(${MODULE_NAME} Winmm.lib)
	endif()
	
	# link with libraries needed on a linux install.
	if( CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" )
	  set_target_properties(${MODULE_NAME} PROPERTIES LINK_FLAGS "-ludev -lXinerama")
	endif( CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" )
endif()

install(FILES ./rift.cfg DESTINATION omegalib/rift)

#------------------------------------------------------------------------------
# DO NOT MODIFY ANYTHING BELOW AFTER THIS LINE
set_target_properties(${MODULE_NAME} PROPERTIES PREFIX "")
if(WIN32)
	set_target_properties(${MODULE_NAME} PROPERTIES FOLDER modules SUFFIX ".pyd")
endif()
