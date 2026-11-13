QT += widgets network
CONFIG += c++17
CONFIG += link_pkgconfig
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0
TEMPLATE = app
TARGET = vehicle_terminal

SOURCES += main.cpp \
           vehicle_data_client.cpp \
           audio_player.cpp \
           media_page.cpp \
           camera_capture.cpp \
           camera_page.cpp \
           map_page.cpp \
           map_search_client.cpp \
           system_page.cpp \
           connection_page.cpp

HEADERS += vehicle_data_client.h \
           audio_player.h \
           media_page.h \
           camera_capture.h \
           camera_color.h \
           camera_page.h \
           map_page.h \
           map_search_client.h \
           system_page.h \
           connection_page.h
