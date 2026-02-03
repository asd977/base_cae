include($$PWD/../../libs/vtk-9.2/vtk.pri)

TARGET = Base_PostProcessing
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
DEPENDPATH += $$PWD/../../core

HEADERS += \
    $$PWD/PostProcessingActivator.h

SOURCES += \
    $$PWD/PostProcessingActivator.cpp
#SOURCES += \
#    C:/project/build-CAE_BASE-Desktop_Qt_5_14_2_MSVC2017_64bit-Release/core/moc/moc_BasePage.cpp
