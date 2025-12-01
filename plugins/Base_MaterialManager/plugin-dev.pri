TARGET = Base_MaterialManager
TEMPLATE = lib
QT += widgets core sql

CONFIG(debug, debug|release) {
    APP_PLUGIN_OUTPUT_DIR = $$clean_path($$PWD/../../build/debug/plugins)
    LIBS += -L$$PWD/../../build/debug -lcore
}
else
{
    APP_PLUGIN_OUTPUT_DIR = $$clean_path($$PWD/../../build/release/plugins)
    LIBS += -L$$PWD/../../build/release -lcore
}

DESTDIR = $$APP_PLUGIN_OUTPUT_DIR

INCLUDEPATH += $$PWD/../../core


HEADERS += \
    $$PWD/MaterialManagerActivator.h

SOURCES += \
    $$PWD/MaterialManagerActivator.cpp
