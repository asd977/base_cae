include($$PWD/plugin-dev.pri)
include($$PWD/../../libs/OCC/occ.pri)

QT += widgets core xml


INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    AnimationAviWidget.h \
    BlockSettingsWidget.h \
    CurveSettingsWidget.h \
    DerivedValueSettingWidget.h \
    IsosurfaceSegmentationWidget.h \
    IsosurfaceSettingsWidget.h \
    NephogramSettingWidget.h \
    PostContourReaderWidget.h \
    PostProcessing.h \
    PostVisualUtils.h \
    ProbeSettingsWidget.h \
    ResultArrangeWidget.h \
    SliceSettingsWidget.h \
    VectorSettingWidget.h \
    ResultRuleConfigDialog.h \
    ResultRuleManager.h

SOURCES += \
    AnimationAviWidget.cpp \
    BlockSettingsWidget.cpp \
    CurveSettingsWidget.cpp \
    DerivedValueSettingWidget.cpp \
    IsosurfaceSegmentationWidget.cpp \
    IsosurfaceSettingsWidget.cpp \
    NephogramSettingWidget.cpp \
    PostContourReaderWidget.cpp \
    PostProcessing.cpp \
    ProbeSettingsWidget.cpp \
    ResultArrangeWidget.cpp \
    SliceSettingsWidget.cpp \
    VectorSettingWidget.cpp \
    ResultRuleConfigDialog.cpp \
    ResultRuleManager.cpp

FORMS += \
    AnimationAviWidget.ui \
    BlockSettingsWidget.ui \
    CurveSettingsWidget.ui \
    DerivedValueSettingWidget.ui \
    IsosurfaceSegmentationWidget.ui \
    IsosurfaceSettingsWidget.ui \
    NephogramSettingWidget.ui \
    PostContourReaderWidget.ui \
    ProbeSettingsWidget.ui \
    ResultArrangeWidget.ui \
    SliceSettingsWidget.ui \
    VectorSettingWidget.ui



RESOURCES += \
    ../../res.qrc

msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
