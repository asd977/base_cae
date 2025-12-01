include($$PWD/plugin-dev.pri)

QT += widgets core xml charts

INCLUDEPATH += $$PWD/../../interfaces


HEADERS += \
    GlobalParameter.h \
    GlobalParameterSettingWidget.h



SOURCES += \
    GlobalParameter.cpp \
    GlobalParameterSettingWidget.cpp


FORMS += \
    GlobalParameterSettingWidget.ui



msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

