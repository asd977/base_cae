include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    CaveBlockPlanePage.h \
    CaveBlockStabilityWidget.h \
    CaveExcavationBodyPage.h \
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
    CaveBlockPlanePage.cpp \
    CaveBlockStabilityWidget.cpp \
    CaveExcavationBodyPage.cpp \
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
    CaveBlockPlanePage.ui \
    CaveBlockStabilityWidget.ui \
    CaveExcavationWidget.ui \
    CaveSupportComputationPage.ui \
    GeoStressRegressionWidget.ui \
    GeoStressSideFacePage.ui \
    GeoStressOuterForcePage.ui \
    GeoStressMeasurePointPage.ui \
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
