include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    CaveExcavationWidget.h \
    GeoStressMeasurePointPage.h \
    GeoStressOuterForcePage.h \
    GeoStressRegressionWidget.h \
    GeoStressSideFacePage.h \
    FillStorageCyclePage.h \
    SeepageBoundaryPage.h \
    SeepageWidget.h \
    SlopeStabilityWidget.h \
    Solver.h \
    SolverSettingWidget.h \
    StressBoundaryPage.h \
    DamAnalysisWidget.h \
    CaveSupportComputationPage.h

SOURCES += \
    CaveExcavationWidget.cpp \
    GeoStressMeasurePointPage.cpp \
    GeoStressOuterForcePage.cpp \
    GeoStressRegressionWidget.cpp \
    GeoStressSideFacePage.cpp \
    FillStorageCyclePage.cpp \
    SeepageBoundaryPage.cpp \
    SeepageWidget.cpp \
    SlopeStabilityWidget.cpp \
    Solver.cpp \
    SolverSettingWidget.cpp \
    StressBoundaryPage.cpp \
    DamAnalysisWidget.cpp \
    CaveSupportComputationPage.cpp

FORMS += \
    CaveExcavationWidget.ui \
    GeoStressRegressionWidget.ui \
    FillStorageCyclePage.ui \
    SeepageBoundaryPage.ui \
    SeepageWidget.ui \
    SolverSettingWidget.ui \
    StressBoundaryPage.ui \
    DamAnalysisWidget.ui \
    SlopeStabilityWidget.ui

RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
