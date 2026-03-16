include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

FORMS += \
    $$PWD/SolidMechanicsSettingPage.ui \
    $$PWD/SolidBoundaryConditionPage.ui \
    $$PWD/SolidResultOutputPage.ui \
    $$PWD/AcousticParameterPage.ui \
    $$PWD/AcousticBoundaryConditionPage.ui \
    $$PWD/AcousticResultOutputPage.ui \
    $$PWD/SolverFrequencyDefinitionPage.ui \
    $$PWD/SolverTimeDefinitionPage.ui \
    $$PWD/SolverVariableDefinitionPage.ui \
    $$PWD/SolverMeshSettingPage.ui \
    $$PWD/SolverResultFocusPage.ui \
    $$PWD/TopologyOptimizationInputPage.ui \
    $$PWD/TopologyObjectiveFunctionPage.ui \
    $$PWD/TopologyConstraintPage.ui \
    $$PWD/TopologyResultOutputPage.ui \
    $$PWD/ParameterOptimizationInputPage.ui \
    $$PWD/ParameterOptimizationProcessPage.ui \
    $$PWD/ParameterOptimizationResultPage.ui \
    $$PWD/FatigueInputSettingPage.ui \
    $$PWD/FatigueResultOutputPage.ui \
    $$PWD/VisualizationManageViewPage.ui \
    $$PWD/VisualizationInteractiveAnalysisPage.ui \
    $$PWD/VisualizationExpressionPage.ui \
    $$PWD/VisualizationStyleAssistPage.ui

RESOURCES += \
    ../../res.qrc

msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
