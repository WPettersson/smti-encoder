INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
SET(SOURCES
  main.cpp
  smti_grp.cpp
  smti_preprocess.cpp
  smti_pbo.cpp
  smti_ip.cpp
  smti_basic.cpp
  matchings.cpp
  )

ADD_EXECUTABLE(tests ${SOURCES})
TARGET_LINK_LIBRARIES(tests smti)

catch_discover_tests(tests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

