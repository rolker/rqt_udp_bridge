cmake_minimum_required(VERSION 3.14.4)
project(rqt_udp_bridge)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()


find_package(ament_cmake REQUIRED)
find_package(qt_gui_cpp REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rqt_gui_cpp REQUIRED)
find_package(udp_bridge_interfaces REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)


find_package(Qt5 COMPONENTS Widgets REQUIRED)

set(rqt_udp_bridge_SRCS
  src/udp_bridge_plugin.cpp
  src/bridge_node.cpp
)

set(rqt_udp_bridge_HDRS
  include/rqt_udp_bridge/udp_bridge_plugin.h
  include/rqt_udp_bridge/bridge_node.h
)

set(rqt_udp_bridge_UIS
  src/udp_bridge_plugin.ui
  src/add_remote_dialog.ui
  src/subscribe_dialog.ui
)


qt5_wrap_cpp(rqt_udp_bridge_MOCS ${rqt_udp_bridge_HDRS})

qt5_wrap_ui(rqt_udp_bridge_UIS_H ${rqt_udp_bridge_UIS})

add_library(${PROJECT_NAME} SHARED
    ${rqt_udp_bridge_SRCS}
    ${rqt_udp_bridge_MOCS}
    ${rqt_udp_bridge_UIS_H}
)

target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include/${PROJECT_NAME}>")

target_link_libraries(${PROJECT_NAME} PUBLIC
  ${rclcpp_TARGETS}
  ${qt_gui_cpp_TARGETS}
  ${rqt_gui_cpp_TARGETS}
  Qt5::Widgets
  ${udp_bridge_interfaces_TARGETS}
)

install(
  TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}
  ARCHIVE DESTINATION lib/${PROJECT_NAME}
  LIBRARY DESTINATION lib/${PROJECT_NAME}
  RUNTIME DESTINATION bin/${PROJECT_NAME}
)

install(FILES plugin.xml
  DESTINATION share/${PROJECT_NAME}
)

install(
  DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

pluginlib_export_plugin_description_file(rqt_gui "plugin.xml")

ament_export_include_directories("include/${PROJECT_NAME}")
ament_export_libraries(${PROJECT_NAME})
ament_export_targets(${PROJECT_NAME})
ament_package()
