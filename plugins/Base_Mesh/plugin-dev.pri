include($$PWD/../../libs/vtk/vtk.pri)
include($$PWD/../../libs/gmsh-4.8.4/gmsh.pri)
include($$PWD/../../libs/OCC/occ.pri)

TARGET = Base_Mesh
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
    $$PWD/MeshActivator.h

SOURCES += \
    $$PWD/MeshActivator.cpp
