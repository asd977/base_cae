include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    SeepageBoundaryPage.h \
    SeepageWidget.h \
    Solver.h \
    StressBoundaryPage.h \
    StressStrainWidget.h

SOURCES += \
    SeepageBoundaryPage.cpp \
    SeepageWidget.cpp \
    Solver.cpp \
    StressBoundaryPage.cpp \
    StressStrainWidget.cpp

FORMS += \
    SeepageBoundaryPage.ui \
    SeepageWidget.ui \
    StressBoundaryPage.ui \
    StressStrainWidget.ui

RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
