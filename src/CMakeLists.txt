
#定义了一个SRC_LIST变量，包含了该目录下所有的源文件
aux_source_directory(. main_LIST)
aux_source_directory(./http http_LIST)
aux_source_directory(./log log_LIST)
aux_source_directory(./mysql mysql_LIST)

#指定生成可执行文件
add_executable(server ${main_LIST} ${http_LIST} ${log_LIST} ${mysql_LIST})
#指定可执行文件链接时需要依赖的库文件
target_link_libraries(server pthread mysqlclient)


#增加C++11
add_definitions(-std=c++11)