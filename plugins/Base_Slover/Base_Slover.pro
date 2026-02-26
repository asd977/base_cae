include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    SeepageWidget.h \
    Solver.h

SOURCES += \
    SeepageWidget.cpp \
    Solver.cpp

FORMS += \ \
    SeepageWidget.ui

RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
