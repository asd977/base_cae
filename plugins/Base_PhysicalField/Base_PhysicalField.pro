include($$PWD/plugin-dev.pri)

QT += widgets core charts

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    BodyForcePage.h \
    DisplacementConstraintWidget.h \
    # DisplacementConstraintSelectionManager.h \
    FaceLoadPage.h \
    GeoStressFitFacePage.h \
    GeoStressPointPage.h \
    LoadSpecificationWidget.h \
    PhySicalField.h \
    PhysicalFieldSettingWidget.h \
    PickManager.h \
    PointForcePage.h \
    SeismicAndAddedMassPage.h \
    TemperatureLoadPage.h

SOURCES += \
    BodyForcePage.cpp \
    DisplacementConstraintWidget.cpp \
    # DisplacementConstraintSelectionManager.cpp \
    FaceLoadPage.cpp \
    GeoStressFitFacePage.cpp \
    GeoStressPointPage.cpp \
    LoadSpecificationWidget.cpp \
    PhySicalField.cpp \
    PhysicalFieldSettingWidget.cpp \
    PickManager.cpp \
    PointForcePage.cpp \
    SeismicAndAddedMassPage.cpp \
    TemperatureLoadPage.cpp


FORMS += \
    BodyForcePage.ui \
    DisplacementConstraintWidget.ui \
    FaceLoadPage.ui \
    GeoStressFitFacePage.ui \
    GeoStressPointPage.ui \
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
