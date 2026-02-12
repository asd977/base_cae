include($$PWD/plugin-dev.pri)

QT += widgets core xml gui

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    MeshGlobalMeshingWidget.h \
    MeshGroupingWidget.h \
    MeshModuleController.h \
    MeshProcessor.h \
    MeshGridIO.h \
    GridEditingWidget.h

SOURCES += \
    MeshGlobalMeshingWidget.cpp \
    MeshGroupingWidget.cpp \
    MeshModuleController.cpp \
    MeshProcessor.cpp \
    MeshGridIO.cpp \
    GridEditingWidget.cpp

FORMS += \
    MeshGlobalMeshingWidget.ui \
    GridEditingWidget.ui \
    MeshGroupingWidget.ui



RESOURCES += \
    ../../res.qrc

msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
