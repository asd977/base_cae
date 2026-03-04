include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    FillStorageCyclePage.h \
    SeepageBoundaryPage.h \
    SeepageWidget.h \
    Solver.h \
    StressBoundaryPage.h \
    StressStrainWidget.h

SOURCES += \
    FillStorageCyclePage.cpp \
    SeepageBoundaryPage.cpp \
    SeepageWidget.cpp \
    Solver.cpp \
    StressBoundaryPage.cpp \
    StressStrainWidget.cpp

FORMS += \
    FillStorageCyclePage.ui \
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
