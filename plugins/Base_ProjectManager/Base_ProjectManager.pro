
include($$PWD/plugin-dev.pri)

QT += widgets core xml

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    ProjectManager.h \
    ProjectTreeWidget.h \
	ProjectSetupDialog.h\
    SettingWidget.h \
    ConcretePageA.h


SOURCES += \
    ProjectManager.cpp \
    ProjectTreeWidget.cpp \
	ProjectSetupDialog.cpp\
    SettingWidget.cpp


FORMS += \
    ProjectTreeWidget.ui \
    SettingWidget.ui


RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
