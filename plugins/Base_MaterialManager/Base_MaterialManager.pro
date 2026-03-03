include($$PWD/plugin-dev.pri)

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    MaterialManager.h \
    MaterialManagerWidget.h \
    MaterialLibraryData.h \
    MaterialEntryWidget.h \
    MaterialModelWidget.h \
    MaterialLibraryWidget.h


SOURCES += \
    MaterialManager.cpp \
    MaterialManagerWidget.cpp \
    MaterialLibraryData.cpp \
    MaterialEntryWidget.cpp \
    MaterialModelWidget.cpp \
    MaterialLibraryWidget.cpp


FORMS += \
    MaterialManagerWidget.ui \
    MaterialLibraryWidget.ui


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
