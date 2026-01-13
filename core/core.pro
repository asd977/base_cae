include($$PWD/../libs/vtk/vtk.pri)

TEMPLATE = lib
CONFIG += shared
QT += core widgets
TARGET = core

DEFINES += CORE_LIB

CONFIG(debug, debug|release) {
    APP_PLUGIN_OUTPUT_DIR = $$clean_path($$PWD/../build/debug)
}
else
{
    APP_PLUGIN_OUTPUT_DIR = $$clean_path($$PWD/../build/release)
}

DESTDIR = $$APP_PLUGIN_OUTPUT_DIR

HEADERS += \
    ContextMenuHub.h \
    EventManager.h \
    KeyValueLineEdit.h \
    ProjectNode.h \
    ServiceRegistry.h \
    core_global.h \
    PublicVariables.h \
    CustomComboBox.h \
        BasePage.h \
        SMThreading.h \
        SMProcess.h \
    StyleManager.h \
    EventManager.h

SOURCES += \
    ContextMenuHub.cpp \
    KeyValueLineEdit.cpp \
    ProjectNode.cpp \
    ServiceRegistry.cpp \
    PublicVariables.cpp \
        SMThreading.cpp \
		BasePage.cpp \
        SMProcess.cpp \
    StyleManager.cpp \
    EventManager.cpp



EXPORTS += \
    BasePage \
msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

MOC_DIR = $$PWD
OBJECTS_DIR = $$OUT_PWD/obj

## 确保 MOC 文件被自动生成
#SOURCES += $$MOC_DIR/moc_BasePage.cpp
