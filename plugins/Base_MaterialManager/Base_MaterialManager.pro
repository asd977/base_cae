include($$PWD/plugin-dev.pri)

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    ElectrolyteSettingWidget.h \
    MaterialManager.h \
    MaterialManagerWidget.h \
    MetalSettingWidget.h \
    ThinFilmSettingWidget.h


SOURCES += \
    ElectrolyteSettingWidget.cpp \
    MaterialManager.cpp \
    MaterialManagerWidget.cpp \
    MetalSettingWidget.cpp \
    ThinFilmSettingWidget.cpp

FORMS += \
    ElectrolyteSettingWidget.ui \
    MaterialManagerWidget.ui \
    MetalSettingWidget.ui \
    ThinFilmSettingWidget.ui

