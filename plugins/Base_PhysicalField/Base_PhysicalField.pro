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
    CurrentDistributionWidgetBase.h \
    PrimaryCurrentDistributionWidget.h \
    SecondaryCurrentDistributionWidget.h \
    TertiaryCurrentDistributionWidget.h

SOURCES += \
    ElectroPhoresisSettingWidget.cpp \
    ElectroPlatingSettingWidget.cpp \
    ElectrodeSettingWidget.cpp \
    ElectrolysisSettingWidget.cpp \
    ElectrolyteSettingWidget.cpp \
    PhySicalField.cpp \
    PhysicalFieldSettingWidget.cpp \
    CurrentDistributionWidgetBase.cpp \
    PrimaryCurrentDistributionWidget.cpp \
    SecondaryCurrentDistributionWidget.cpp \
    TertiaryCurrentDistributionWidget.cpp


FORMS += \
    ElectroPhoresisSettingWidget.ui \
    ElectroPlatingSettingWidget.ui \
    ElectrodeSettingWidget.ui \
    ElectrolysisSettingWidget.ui \
    ElectrolyteSettingWidget.ui \
    PhysicalFieldSettingWidget.ui \
    PrimaryCurrentDistributionWidget.ui \
    SecondaryCurrentDistributionWidget.ui \
    TertiaryCurrentDistributionWidget.ui


RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
