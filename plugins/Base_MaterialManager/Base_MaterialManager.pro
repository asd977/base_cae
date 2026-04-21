include($$PWD/plugin-dev.pri)
include($$PWD/../../libs/vtk-9.2/vtk.pri)
INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    MaterialManager.h \
    MaterialManagerWidget.h \
    MaterialLibraryData.h \
    MaterialEntryWidget.h \
    MaterialModelWidget.h \
    MaterialTransitionPage.h


SOURCES += \
    MaterialManager.cpp \
    MaterialManagerWidget.cpp \
    MaterialLibraryData.cpp \
    MaterialEntryWidget.cpp \
    MaterialModelWidget.cpp \
    MaterialTransitionPage.cpp


FORMS += \
    MaterialManagerWidget.ui


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
