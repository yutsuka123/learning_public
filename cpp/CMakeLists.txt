cmake_minimum_required(VERSION 3.10)
project(CppLearning)

# C++11標準を使用
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# コンパイラに文字コードを指定
if(MSVC)
    add_compile_options(/utf-8)
endif()

# hello_worldの実行ファイルを作成
# OpenCV を検出
find_package(OpenCV REQUIRED)

# 実行ファイルを生成 (OpenCV サンプルも含める)
add_executable(hello_world
    hello_world.cpp
    opencv_sample.cpp
)

# インクルードディレクトリとライブラリをリンク
target_include_directories(hello_world PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(hello_world PRIVATE ${OpenCV_LIBS})
