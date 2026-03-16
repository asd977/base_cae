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
    $$PWD/AcousticBoundaryWidget.h \
    $$PWD/AcousticInputWidget.h \
    $$PWD/AcousticOutputWidget.h \
    $$PWD/DisplayActivator.h \
    $$PWD/DisplayManager.h \
    $$PWD/DisplayRootWidget.h \
    $$PWD/FatigueInputWidget.h \
    $$PWD/FatigueOutputWidget.h \
    $$PWD/OptimizationInputWidget.h \
    $$PWD/OptimizationOutputWidget.h \
    $$PWD/OtherSettingsWidget.h \
    $$PWD/SolidBoundaryWidget.h \
    $$PWD/SolidInputWidget.h \
    $$PWD/SolidOutputWidget.h \
    $$PWD/SolverFrequencyWidget.h \
    $$PWD/SolverTimeWidget.h

SOURCES += \
    $$PWD/AcousticBoundaryWidget.cpp \
    $$PWD/AcousticInputWidget.cpp \
    $$PWD/AcousticOutputWidget.cpp \
    $$PWD/DisplayActivator.cpp \
    $$PWD/DisplayManager.cpp \
    $$PWD/DisplayRootWidget.cpp \
    $$PWD/FatigueInputWidget.cpp \
    $$PWD/FatigueOutputWidget.cpp \
    $$PWD/OptimizationInputWidget.cpp \
    $$PWD/OptimizationOutputWidget.cpp \
    $$PWD/OtherSettingsWidget.cpp \
    $$PWD/SolidBoundaryWidget.cpp \
    $$PWD/SolidInputWidget.cpp \
    $$PWD/SolidOutputWidget.cpp \
    $$PWD/SolverFrequencyWidget.cpp \
    $$PWD/SolverTimeWidget.cpp
