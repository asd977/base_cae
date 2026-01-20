include($$PWD/plugin-dev.pri)

QT += widgets core xml gui openglwidgets

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    BaseGeometryDialog\Boxdialog.h \
    BaseGeometryDialog\TorusDialog.h\
    BaseGeometryDialog\SphereDialog.h\
    BaseGeometryDialog\ConeDialog.h\
    BaseGeometryDialog\CylinderDialog.h\
    BaseGeometryDialog\CircleDialog.h\
    BaseGeometryDialog\EllipseDialog.h\
    BaseGeometryDialog\PolygonDialog.h\
    BaseGeometryDialog\RectangleDialog.h\
    OccBooleanBaseWidget.h \
    OccBooleanFuseWidget.h \
    OccBooleanCutWidget.h \
    OccBooleanCommonWidget.h \
    OccShapeToVtk.h \
    PreProcessing.h \
    GeometryCleanup.h \
    GeomManagerInstance.h\
    GeomShapeObject.h\
    GeometryImportWidget.h

SOURCES += \
    BaseGeometryDialog\Boxdialog.cpp\
    BaseGeometryDialog\TorusDialog.cpp\
    BaseGeometryDialog\SphereDialog.cpp\
    BaseGeometryDialog\ConeDialog.cpp\
    BaseGeometryDialog\CylinderDialog.cpp\
    BaseGeometryDialog\CircleDialog.cpp\
    BaseGeometryDialog\EllipseDialog.cpp\
    BaseGeometryDialog\PolygonDialog.cpp\
    BaseGeometryDialog\RectangleDialog.cpp\
    OccBooleanBaseWidget.cpp \
    OccBooleanFuseWidget.cpp \
    OccBooleanCutWidget.cpp \
    OccBooleanCommonWidget.cpp \
    OccShapeToVtk.cpp \
    PreProcessing.cpp \
    GeomManagerInstance.cpp\
    GeomShapeObject.cpp\
    GeometryImportWidget.cpp
	
	
	

FORMS += \
    OccBooleanBaseWidget.ui


RESOURCES += \
    ../../res.qrc

msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
