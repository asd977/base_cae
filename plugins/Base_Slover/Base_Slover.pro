include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    SeepageBoundaryPage.h \
    SeepageWidget.h \
    Solver.h

SOURCES += \
    SeepageBoundaryPage.cpp \
    SeepageWidget.cpp \
    Solver.cpp

FORMS += \
    SeepageBoundaryPage.ui \
    SeepageWidget.ui

RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
