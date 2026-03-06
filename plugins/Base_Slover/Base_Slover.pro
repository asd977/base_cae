include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    FillStorageCyclePage.h \
    SeepageBoundaryPage.h \
    SeepageWidget.h \
    Solver.h \
    StressBoundaryPage.h \
    DamAnalysisWidget.h

SOURCES += \
    FillStorageCyclePage.cpp \
    SeepageBoundaryPage.cpp \
    SeepageWidget.cpp \
    Solver.cpp \
    StressBoundaryPage.cpp \
    DamAnalysisWidget.cpp

FORMS += \
    FillStorageCyclePage.ui \
    SeepageBoundaryPage.ui \
    SeepageWidget.ui \
    StressBoundaryPage.ui \
    DamAnalysisWidget.ui

RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
