include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    TraceCurveWidget.h \
    VisualizationWidget.h \
    qcustomplot.h

SOURCES += \
    TraceCurveWidget.cpp \
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

