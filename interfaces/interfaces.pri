
QT += core

CONFIG(debug, debug|release) {
    APP_PLUGIN_OUTPUT_DIR = $$clean_path($$PWD/../build/debug/plugins)
}
else
{
    APP_PLUGIN_OUTPUT_DIR = $$clean_path($$PWD/../build/release/plugins)
}

DESTDIR = $$APP_PLUGIN_OUTPUT_DIR

HEADERS += \
    $$PWD/services/GlobalParameterService.h \
    $$PWD/services/MaterialManagerService.h \
    $$PWD/services/OutputService.h \
    $$PWD/services/PhysicalFieldService.h \
    $$PWD/services/PostProcessingService.h \
    $$PWD/services/PreProcessingService.h \
    $$PWD/services/ProjectManagerService.h \
    $$PWD/services/SolverService.h \
    $$PWD/services/SurrogateModelService.h \
    $$PWD/services/UiFrameService.h \
    $$PWD/services/VisualWidgetService.h \
    $$PWD/services/MeshModuleControllerService.h \
    $$PWD/PublicConstants.h \
    $$PWD/struct/MeshMap.h


