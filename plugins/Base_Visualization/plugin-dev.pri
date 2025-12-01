include($$PWD/../../libs/vtk/vtk.pri)

TARGET = Base_Visualization
TEMPLATE = lib

CONFIG(debug, debug|release) {
    DESTDIR = $$PWD/../../build/debug/plugins
    LIBS += -L$$PWD/../../build/debug -lcore
} else {
    DESTDIR = $$PWD/../../build/release/plugins
    LIBS += -L$$PWD/../../build/release -lcore
}



INCLUDEPATH += $$PWD/../../core


HEADERS += \
    $$PWD/VisualizationActivator.h

SOURCES += \
    $$PWD/VisualizationActivator.cpp
