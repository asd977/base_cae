include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    Ribbon.h \
    RibbonButtonGroup.h \
    RibbonTabContent.h \
    UiFrame.h

SOURCES += \
    Ribbon.cpp \
    RibbonButtonGroup.cpp \
    RibbonTabContent.cpp \
    UiFrame.cpp

FORMS += \
    RibbonButtonGroup.ui \
    RibbonTabContent.ui

RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

