cmake_minimum_required(VERSION 3.16)

# ESP-DashboardPlus is wired in via components/esp_dashboard_plus/CMakeLists.txt,
# which delegates to the sibling library directory.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_dashboard_test)
