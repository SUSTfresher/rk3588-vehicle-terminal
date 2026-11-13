QT += widgets network testlib
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = map_search_test
SOURCES += main.cpp ../01_ui_shell/map_page.cpp ../01_ui_shell/map_search_client.cpp
HEADERS += ../01_ui_shell/map_page.h ../01_ui_shell/map_search_client.h
