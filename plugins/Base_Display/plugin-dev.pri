include($$PWD/../../libs/vtk-9.2/vtk.pri)

TARGET = Base_Display
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
    $$PWD/DisplayActivator.h \
    $$PWD/DisplayModule.h \
    $$PWD/SolidMechanicsDisplayPage.h

SOURCES += \
    $$PWD/DisplayActivator.cpp \
    $$PWD/DisplayModule.cpp \
    $$PWD/SolidMechanicsDisplayPage.cpp
