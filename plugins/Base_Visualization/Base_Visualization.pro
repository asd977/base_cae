include($$PWD/plugin-dev.pri)

QT += widgets core xml charts

INCLUDEPATH += $$PWD/../../interfaces


HEADERS += \
    CurveWidget.h \
    PVCameraToolbar.h \
    PostProcessingWidget.h \
    PreProcessingWidget.h \
    Visualization.h \
    VisualWidget.h \
    VTKScalar.h \
    vtkWidget.h


SOURCES += \
    CurveWidget.cpp \
    PVCameraToolbar.cpp \
    PostProcessingWidget.cpp \
    PreProcessingWidget.cpp \
    Visualization.cpp \
    VisualWidget.cpp \
    VTKScalar.cpp \
    vtkWidget.cpp

FORMS += \
    CurveWidget.ui \
    PVCameraToolbar.ui \
    PostProcessingWidget.ui \
    PreProcessingWidget.ui \
    VisualWidget.ui \
    vtkWidget.ui

RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

