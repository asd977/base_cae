include($$PWD/plugin-dev.pri)

QT += widgets core xml gui

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    ParametricDam/DamParametricTypes.h \
    ParametricDam/MsgJzDamIO.h \
    ParametricDam/DamParametricCalculator.h \
    ParametricDam/DamParametricRunner.h \
    MeshGlobalMeshingWidget.h \
    MeshSingleMeshingWidget.h \
    MeshVolumeControlWidget.h \
    MeshFaceControlWidget.h \
    MeshEdgeControlWidget.h \
    MeshModuleController.h \
    MeshProcessor.h \
    MeshGridIO.h \
    GridEditingWidget.h \
    MeshGroupingWidget.h

SOURCES += \
    ParametricDam/DamParametricTypes.cpp \
    ParametricDam/MsgJzDamIO.cpp \
    ParametricDam/DamParametricCalculator.cpp \
    ParametricDam/DamParametricRunner.cpp \
    MeshGlobalMeshingWidget.cpp \
    MeshSingleMeshingWidget.cpp \
    MeshVolumeControlWidget.cpp \
    MeshFaceControlWidget.cpp \
    MeshEdgeControlWidget.cpp \
    MeshModuleController.cpp \
    MeshProcessor.cpp \
    MeshGridIO.cpp \
    GridEditingWidget.cpp \
    MeshGroupingWidget.cpp

FORMS += \
    MeshGlobalMeshingWidget.ui \
    MeshSingleMeshingWidget.ui \
    MeshVolumeControlWidget.ui \
    MeshFaceControlWidget.ui \
    MeshEdgeControlWidget.ui \
    GridEditingWidget.ui \
    MeshGroupingWidget.ui


RESOURCES += \
    ../../res.qrc

msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
