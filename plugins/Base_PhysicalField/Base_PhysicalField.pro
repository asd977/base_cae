include($$PWD/plugin-dev.pri)

QT += widgets core charts

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    DisplacementConstraintWidget.h \
    LoadSpecificationWidget.h \
    PhySicalField.h \
    PhysicalFieldSettingWidget.h

SOURCES += \
    DisplacementConstraintWidget.cpp \
    LoadSpecificationWidget.cpp \
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
