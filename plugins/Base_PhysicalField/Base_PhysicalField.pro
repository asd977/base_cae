include($$PWD/plugin-dev.pri)

QT += widgets core charts

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    ElectroPhoresisSettingWidget.h \
    ElectroPlatingSettingWidget.h \
    ElectrodeSettingWidget.h \
    ElectrolysisSettingWidget.h \
    ElectrolyteSettingWidget.h \
    PhySicalField.h \
    PhysicalFieldSettingWidget.h \
    PrimaryCurrentDistributionWidget.h

SOURCES += \
    ElectroPhoresisSettingWidget.cpp \
    ElectroPlatingSettingWidget.cpp \
    ElectrodeSettingWidget.cpp \
    ElectrolysisSettingWidget.cpp \
    ElectrolyteSettingWidget.cpp \
    PhySicalField.cpp \
    PhysicalFieldSettingWidget.cpp \
    PrimaryCurrentDistributionWidget.cpp


FORMS += \
    ElectroPhoresisSettingWidget.ui \
    ElectroPlatingSettingWidget.ui \
    ElectrodeSettingWidget.ui \
    ElectrolysisSettingWidget.ui \
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
