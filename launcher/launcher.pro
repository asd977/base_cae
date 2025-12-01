TEMPLATE = app
QT += widgets

CONFIG += console c++11

CONFIG(debug, debug|release) {
    APP_OUTPUT_DIR = $$clean_path($$PWD/../build/debug)
    LIBS += -L$$PWD/../build/debug -lcore
} else {
    APP_OUTPUT_DIR = $$clean_path($$PWD/../build/release)
    LIBS += -L$$PWD/../build/release -lcore
}

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    TARGET = CAE_BASEd
} else {
    TARGET = CAE_BASE
}


DESTDIR = $$APP_OUTPUT_DIR

INCLUDEPATH += $$PWD/../core
INCLUDEPATH += $$PWD/../interfaces

SOURCES += \
    launcher.cpp
