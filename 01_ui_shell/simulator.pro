QT = core network
CONFIG += c++17 console
CONFIG -= app_bundle

TEMPLATE = app
TARGET = vehicle_simulator

SOURCES += simulator_main.cpp \
           vehicle_simulator.cpp

HEADERS += vehicle_simulator.h
