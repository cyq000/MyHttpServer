cmake_minimum_required(VERSION 3.0)
project(httpserver)


#配置编译选项
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g)

#配置最终的可执行文件输出的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

#配置头文件的搜索路径
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/include/http)
include_directories(${PROJECT_SOURCE_DIR}/include/lock)
include_directories(${PROJECT_SOURCE_DIR}/include/log)
include_directories(${PROJECT_SOURCE_DIR}/include/mysql)
include_directories(${PROJECT_SOURCE_DIR}/include/threadpool)
include_directories(${PROJECT_SOURCE_DIR}/include/time)

#加载子目录
add_subdirectory(src)