include($$PWD/plugin-dev.pri)

QT += widgets core xml

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    Output.h \
    OutputWidget.h


SOURCES += \
    Output.cpp \
    OutputWidget.cpp

FORMS += \
    OutputWidget.ui \


RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
