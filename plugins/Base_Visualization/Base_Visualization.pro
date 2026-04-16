include($$PWD/plugin-dev.pri)

QT += widgets core printsupport

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    CurvePlotWidget.h \
    VisualizationWidget.h \
    qcustomplot.h

SOURCES += \
    CurvePlotWidget.cpp \
    VisualizationWidget.cpp \
    qcustomplot.cpp

FORMS += \

RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

