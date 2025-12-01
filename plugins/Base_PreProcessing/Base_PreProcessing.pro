include($$PWD/plugin-dev.pri)

QT += widgets core xml gui  openglwidgets

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    OccBooleanToolWidget.h \
    OccShapeToVtk.h \
    PreProcessing.h \
    PreProcessingWidget.h\
	GemoPickerType.h\
	GemoPickerInteractorStyle.h\
	GeomManagerInstance.h\
	GeomShapeObject.h\
	MeshProcessor.h\
	GemoRWUtilInstance.h\
	GeometryCreator.h\
	Boxdialog.h \
	TorusDialog.h\
	SphereDialog.h\
	ConeDialog.h\
	CylinderDialog.h\
	HelixDialog.h\
	CircleDialog.h\
	EllipseDialog.h\
	PolygonDialog.h\
	PointDialog.h\
	RectangleDialog.h\
	SquareDialog.h\
	LineDialog.h\
	ArcDialog.h\
	HyperbolaDialog.h\
	ParabolaDialog.h\
	Bezier2Dialog.h\
	Bezier3Dialog.h\
	InterpolationDialog.h\
	ParametricCurveDialog.h\
	PointPickingDialog.h\
	CentroidPickingDialog.h\
	DistancePickingDialog.h\
	MeshRefinementWidget.h\
	MeshRefinement3DWidget.h\
	GeometryImportWidget.h\
	GridImportWidget.h



SOURCES += \
    OccBooleanToolWidget.cpp \
    OccShapeToVtk.cpp \
    PreProcessing.cpp \
    PreProcessingWidget.cpp\
	GemoPickerInteractorStyle.cpp\
	GeomManagerInstance.cpp\
	GeomShapeObject.cpp\
	MeshProcessor.cpp\
	GemoRWUtilInstance.cpp\
	GeometryCreator.cpp\
	Boxdialog.cpp\
	TorusDialog.cpp\
	SphereDialog.cpp\
	ConeDialog.cpp\
	CylinderDialog.cpp\
	HelixDialog.cpp\
	CircleDialog.cpp\
	EllipseDialog.cpp\
	PolygonDialog.cpp\
	PointDialog.cpp\
	RectangleDialog.cpp\
	SquareDialog.cpp\
	LineDialog.cpp\
	ArcDialog.cpp\
	HyperbolaDialog.cpp\
	ParabolaDialog.cpp\
	Bezier2Dialog.cpp\
	Bezier3Dialog.cpp\
	InterpolationDialog.cpp\
	ParametricCurveDialog.cpp\
	PointPickingDialog.cpp\
	CentroidPickingDialog.cpp\
	DistancePickingDialog.cpp\
	MeshRefinementWidget.cpp\
	MeshRefinement3DWidget.cpp\
	GeometryImportWidget.cpp\
	GridImportWidget.cpp
	
	
	

FORMS += \
    OccBooleanToolWidget.ui \
    PreProcessingWidget.ui


RESOURCES += \
    ../../res.qrc

msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
