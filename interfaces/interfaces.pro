TEMPLATE = aux


CONFIG(debug, debug|release) {
    APP_PLUGIN_OUTPUT_DIR = $$clean_path($$PWD/../build/debug/plugins)
}
else
{
    APP_PLUGIN_OUTPUT_DIR = $$clean_path($$PWD/../build/release/plugins)
}

DESTDIR = $$APP_PLUGIN_OUTPUT_DIR

HEADERS += \
    services/GlobalParameterService.h \
    services/MaterialManagerService.h \
    services/OutputService.h \
    services/PhysicalFieldService.h \
    services/PostProcessingService.h \
    services/PreProcessingService.h \
    services/ProjectManagerService.h \
    services/SolverService.h \
    services/SurrogateModelService.h \
    services/UiFrameService.h \
    services/VisualWidgetService.h \
    struct/Mesh.h \
    services/MeshModuleControllerService.h \
    PublicConstants.h

CONFIG += qt \
    qt \
    qt


