cmake_minimum_required(VERSION 3.11)
project(monitorob)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# FTXUIをGitHubから自動取得する設定
include(FetchContent)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

# サーバーのビルド
add_executable(monitorob_server monitorob_server.cpp)
target_link_libraries(monitorob_server PRIVATE pthread)

# TUIクライアントのビルド
add_executable(monitorob_tui monitorob_tui.cpp)
target_link_libraries(monitorob_tui PRIVATE ftxui::screen ftxui::dom ftxui::component pthread)
