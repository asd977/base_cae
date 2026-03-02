include($$PWD/plugin-dev.pri)

QT += widgets core xml


INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    AnimationAviWidget.h \
    BlockSettingsWidget.h \
    DerivedValueSettingWidget.h \
    IsosurfaceSegmentationWidget.h \
    IsosurfaceSettingsWidget.h \
    NephogramSettingWidget.h \
    PostProcessing.h \
    PostVisualUtils.h \
    ProbeSettingsWidget.h \
    SliceSettingsWidget.h \
    StreamlineVectorSettingWidget.h \
    VectorSettingWidget.h

SOURCES += \
    AnimationAviWidget.cpp \
    BlockSettingsWidget.cpp \
    DerivedValueSettingWidget.cpp \
    IsosurfaceSegmentationWidget.cpp \
    IsosurfaceSettingsWidget.cpp \
    NephogramSettingWidget.cpp \
    PostProcessing.cpp \
    ProbeSettingsWidget.cpp \
    SliceSettingsWidget.cpp \
    StreamlineVectorSettingWidget.cpp \
    VectorSettingWidget.cpp

FORMS += \
    AnimationAviWidget.ui \
    BlockSettingsWidget.ui \
    DerivedValueSettingWidget.ui \
    IsosurfaceSegmentationWidget.ui \
    IsosurfaceSettingsWidget.ui \
    NephogramSettingWidget.ui \
    ProbeSettingsWidget.ui \
    SliceSettingsWidget.ui \
    StreamlineVectorSettingWidget.ui \
    VectorSettingWidget.ui



RESOURCES += \
    ../../res.qrc

msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
