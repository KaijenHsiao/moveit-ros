set(MOVEIT_LIB_NAME moveit_occupancy_map_monitor)

find_package(PkgConfig REQUIRED)
pkg_check_modules(OCTOMAP REQUIRED octomap)
include_directories(${OCTOMAP_INCLUDE_DIRS})
link_directories(${OCTOMAP_LIBRARY_DIRS})

add_library(${MOVEIT_LIB_NAME}
  src/occupancy_map_monitor.cpp
  src/point_cloud_occupancy_map_updater.cpp
  src/octomap_markers.cpp
  )
target_link_libraries(${MOVEIT_LIB_NAME} ${OCTOMAP_LIBRARIES} ${catkin_LIBRARIES} ${Boost_LIBRARIES} moveit_robot_self_filter)

install(TARGETS ${MOVEIT_LIB_NAME} LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

add_executable(moveit_occupancy_map_server src/occupancy_map_server.cpp)
target_link_libraries(moveit_occupancy_map_server ${MOVEIT_LIB_NAME}
  ${OCTOMAP_LIBRARIES} ${Boost_LIBRARIES} moveit_robot_self_filter)
