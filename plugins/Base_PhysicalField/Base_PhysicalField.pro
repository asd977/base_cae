include($$PWD/plugin-dev.pri)

QT += widgets core charts

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    BeamLoadPage.h \
    BodyForcePage.h \
    DisplacementConstraintWidget.h \
    # DisplacementConstraintSelectionManager.h \
    FaceLoadPage.h \
    GeoStressFitFacePage.h \
    GeoStressPointPage.h \
    LinearForcePage.h \
    LoadSpecificationWidget.h \
    PhySicalField.h \
    PhysicalFieldSettingWidget.h \
    PointForcePage.h \
    SeismicAndAddedMassPage.h \
    TemperatureLoadPage.h

SOURCES += \
    BeamLoadPage.cpp \
    BodyForcePage.cpp \
    DisplacementConstraintWidget.cpp \
    # DisplacementConstraintSelectionManager.cpp \
    FaceLoadPage.cpp \
    GeoStressFitFacePage.cpp \
    GeoStressPointPage.cpp \
    LinearForcePage.cpp \
    LoadSpecificationWidget.cpp \
    PhySicalField.cpp \
    PhysicalFieldSettingWidget.cpp \
    PointForcePage.cpp \
    SeismicAndAddedMassPage.cpp \
    TemperatureLoadPage.cpp


FORMS += \
    BeamLoadPage.ui \
    BodyForcePage.ui \
    DisplacementConstraintWidget.ui \
    FaceLoadPage.ui \
    GeoStressFitFacePage.ui \
    GeoStressPointPage.ui \
    LinearForcePage.ui \
    LoadSpecificationWidget.ui \
    PhysicalFieldSettingWidget.ui \
    PointForcePage.ui \
    SeismicAndAddedMassPage.ui \
    TemperatureLoadPage.ui


RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
