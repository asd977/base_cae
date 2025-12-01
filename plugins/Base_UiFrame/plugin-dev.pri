include($$PWD/../../libs/Qt6-AdvancedDock/DockingWidget.pri)
include($$PWD/../../libs/SARibbon-qt6/SARibbon.pri)
TARGET = Base_UiFrame
TEMPLATE = lib
QT += widgets core

CONFIG(debug, debug|release) {
    APP_PLUGIN_OUTPUT_DIR = $$clean_path($$PWD/../../build/debug/plugins)
    LIBS += -L$$PWD/../../build/debug -lcore
} else {
    APP_PLUGIN_OUTPUT_DIR = $$clean_path($$PWD/../../build/release/plugins)
    LIBS += -L$$PWD/../../build/release -lcore
}

DESTDIR = $$APP_PLUGIN_OUTPUT_DIR

INCLUDEPATH += $$PWD/../../core

HEADERS += \
    $$PWD/UiFrameActivator.h

SOURCES += \
    $$PWD/UiFrameActivator.cpp
