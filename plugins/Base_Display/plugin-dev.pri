include($$PWD/../../libs/vtk-9.2/vtk.pri)

TARGET = Base_Display
TEMPLATE = lib
QT += widgets core

CONFIG(debug, debug|release) {
    APP_PLUGIN_OUTPUT_DIR = $$clean_path($$PWD/../../build/debug/plugins)
    LIBS += -L$$PWD/../../build/debug -lcore
} else {
    APP_PLUGIN_OUTPUT_DIR = $$clean_path($$PWD/../../build/release/plugins)
    LIBS += -L$$PWD/../../build/release -lcore
}

DESTDIR = $$APP_PLUGIN_OUTPUT_DIR

INCLUDEPATH += $$PWD/../../core

HEADERS += \
    $$PWD/DisplayActivator.h \
    $$PWD/DisplayManager.h \
    $$PWD/SolidMechanicsSettingPage.h \
    $$PWD/SolidBoundaryConditionPage.h \
    $$PWD/SolidResultOutputPage.h \
    $$PWD/AcousticParameterPage.h \
    $$PWD/AcousticBoundaryConditionPage.h \
    $$PWD/AcousticResultOutputPage.h \
    $$PWD/SolverFrequencyDefinitionPage.h \
    $$PWD/SolverTimeDefinitionPage.h \
    $$PWD/SolverVariableDefinitionPage.h \
    $$PWD/SolverMeshSettingPage.h \
    $$PWD/SolverResultFocusPage.h \
    $$PWD/TopologyOptimizationInputPage.h \
    $$PWD/TopologyObjectiveFunctionPage.h \
    $$PWD/TopologyConstraintPage.h \
    $$PWD/TopologyResultOutputPage.h \
    $$PWD/ParameterOptimizationInputPage.h \
    $$PWD/ParameterOptimizationProcessPage.h \
    $$PWD/ParameterOptimizationResultPage.h \
    $$PWD/FatigueInputSettingPage.h \
    $$PWD/FatigueResultOutputPage.h \
    $$PWD/VisualizationManageViewPage.h \
    $$PWD/VisualizationInteractiveAnalysisPage.h \
    $$PWD/VisualizationExpressionPage.h \
    $$PWD/VisualizationStyleAssistPage.h

SOURCES += \
    $$PWD/DisplayActivator.cpp \
    $$PWD/DisplayManager.cpp \
    $$PWD/SolidMechanicsSettingPage.cpp \
    $$PWD/SolidBoundaryConditionPage.cpp \
    $$PWD/SolidResultOutputPage.cpp \
    $$PWD/AcousticParameterPage.cpp \
    $$PWD/AcousticBoundaryConditionPage.cpp \
    $$PWD/AcousticResultOutputPage.cpp \
    $$PWD/SolverFrequencyDefinitionPage.cpp \
    $$PWD/SolverTimeDefinitionPage.cpp \
    $$PWD/SolverVariableDefinitionPage.cpp \
    $$PWD/SolverMeshSettingPage.cpp \
    $$PWD/SolverResultFocusPage.cpp \
    $$PWD/TopologyOptimizationInputPage.cpp \
    $$PWD/TopologyObjectiveFunctionPage.cpp \
    $$PWD/TopologyConstraintPage.cpp \
    $$PWD/TopologyResultOutputPage.cpp \
    $$PWD/ParameterOptimizationInputPage.cpp \
    $$PWD/ParameterOptimizationProcessPage.cpp \
    $$PWD/ParameterOptimizationResultPage.cpp \
    $$PWD/FatigueInputSettingPage.cpp \
    $$PWD/FatigueResultOutputPage.cpp \
    $$PWD/VisualizationManageViewPage.cpp \
    $$PWD/VisualizationInteractiveAnalysisPage.cpp \
    $$PWD/VisualizationExpressionPage.cpp \
    $$PWD/VisualizationStyleAssistPage.cpp
