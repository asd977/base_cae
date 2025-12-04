include($$PWD/plugin-dev.pri)

QT += widgets core charts

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    ElectrodeSurfaceSettingWidget.h \
    ElectrolysisSettingWidget.h \
    ElectrolyteSettingWidget.h \
    PhySicalField.h \
    PhysicalFieldSettingWidget.h \
    PrimaryCurrentDistributionWidget.h

SOURCES += \
    ElectrodeSurfaceSettingWidget.cpp \
    ElectrolyteSettingWidget.cpp \
    PhySicalField.cpp \
    PhysicalFieldSettingWidget.cpp \
    PrimaryCurrentDistributionWidget.cpp


FORMS += \
    ElectrodeSurfaceSettingWidget.ui \
    ElectrolyteSettingWidget.ui \
    PhysicalFieldSettingWidget.ui \
    PrimaryCurrentDistributionWidget.ui


RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
