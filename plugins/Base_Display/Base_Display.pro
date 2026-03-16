include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \

SOURCES += \


FORMS += \
    SolidMechanicsDisplay.ui \

RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
