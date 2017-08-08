project(xpsupport)
cmake_minimum_required(VERSION 2.8)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
  # x64
  SET( HDE_SRC "minhook/src/HDE/hde32.c" )
else( CMAKE_SIZEOF_VOID_P EQUAL 8 ) 
  # x86
  SET( HDE_SRC "minhook/src/HDE/hde32.c" )
endif( CMAKE_SIZEOF_VOID_P EQUAL 8 )

add_library(xpsupport SHARED main.c sync_v3.c ${HDE_SRC}
  minhook/src/buffer.c
  minhook/src/hook.c
  minhook/src/trampoline.c
  )
set_target_properties(xpsupport PROPERTIES PREFIX "") # 去掉mingw默认的lib前辍