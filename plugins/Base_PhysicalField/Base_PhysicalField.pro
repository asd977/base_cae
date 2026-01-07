include($$PWD/plugin-dev.pri)

QT += widgets core charts

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    ConstraintSettingWidget.h \
    LoadSettingWidget.h \
    PhySicalField.h \
    PhysicalFieldSettingWidget.h

SOURCES += \
    ConstraintSettingWidget.cpp \
    LoadSettingWidget.cpp \
    PhySicalField.cpp \
    PhysicalFieldSettingWidget.cpp


FORMS += \
    ConstraintSettingWidget.ui \
    LoadSettingWidget.ui \
    PhysicalFieldSettingWidget.ui


RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
