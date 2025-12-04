include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    CurveWidget.h \
    Solver.h \
    SolverSettingWidget.h

SOURCES += \
    CurveWidget.cpp \
    Solver.cpp \
    SolverSettingWidget.cpp

FORMS += \
    CurveWidget.ui \
    SolverSettingWidget.ui

RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
