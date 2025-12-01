include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    CurveWidget.h \
    Solver.h

SOURCES += \
    CurveWidget.cpp \
    Solver.cpp

FORMS += \
    CurveWidget.ui

RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
