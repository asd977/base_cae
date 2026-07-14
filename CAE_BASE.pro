TEMPLATE = subdirs
CONFIG += console

SUBDIRS += \
    launcher \
    plugins \
    core

include($$PWD/interfaces/interfaces.pri)
launcher.depends += plugins
launcher.depends += core
plugins.depends += core

win32-msvc* {
    QMAKE_CXXFLAGS += /MP
}



