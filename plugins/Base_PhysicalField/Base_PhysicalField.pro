include($$PWD/plugin-dev.pri)

QT += widgets core charts

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    DisplacementConstraintWidget.h \
    DisplacementConstraintSelectionManager.h \
    LoadSpecificationWidget.h \
    LoadSpecPageBase.h \
    GeoMeasurePage.h \
    GeoFitPage.h \
    BodyForcePage.h \
    PointForcePage.h \
    LineForcePage.h \
    SurfaceForcePage.h \
    TemperaturePage.h \
    BeamLoadPage.h \
    SeismicPage.h \
    AddedMassPage.h \
    PhySicalField.h \
    PhysicalFieldSettingWidget.h

SOURCES += \
    DisplacementConstraintWidget.cpp \
    DisplacementConstraintSelectionManager.cpp \
    LoadSpecificationWidget.cpp \
    LoadSpecPageBase.cpp \
    GeoMeasurePage.cpp \
    GeoFitPage.cpp \
    BodyForcePage.cpp \
    PointForcePage.cpp \
    LineForcePage.cpp \
    SurfaceForcePage.cpp \
    TemperaturePage.cpp \
    BeamLoadPage.cpp \
    SeismicPage.cpp \
    AddedMassPage.cpp \
    PhySicalField.cpp \
    PhysicalFieldSettingWidget.cpp


FORMS += \
    DisplacementConstraintWidget.ui \
    LoadSpecificationWidget.ui \
    PhysicalFieldSettingWidget.ui


RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
