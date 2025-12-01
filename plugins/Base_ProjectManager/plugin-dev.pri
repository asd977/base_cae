# include($$PWD/../../libs/QuaZIP/QuaZip.pri)
TARGET = Base_ProjectManager
TEMPLATE = lib
QT += widgets core

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
    $$PWD/ProjectManagerActivator.h

SOURCES += \
    $$PWD/ProjectManagerActivator.cpp


