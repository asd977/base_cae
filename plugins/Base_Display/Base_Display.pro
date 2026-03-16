include($$PWD/plugin-dev.pri)

QT += widgets core

INCLUDEPATH += $$PWD/../../interfaces

FORMS += \
    $$PWD/AcousticBoundaryWidget.ui \
    $$PWD/AcousticInputWidget.ui \
    $$PWD/AcousticOutputWidget.ui \
    $$PWD/DisplayRootWidget.ui \
    $$PWD/FatigueInputWidget.ui \
    $$PWD/FatigueOutputWidget.ui \
    $$PWD/OptimizationInputWidget.ui \
    $$PWD/OptimizationOutputWidget.ui \
    $$PWD/OtherSettingsWidget.ui \
    $$PWD/SolidBoundaryWidget.ui \
    $$PWD/SolidInputWidget.ui \
    $$PWD/SolidOutputWidget.ui \
    $$PWD/SolverFrequencyWidget.ui \
    $$PWD/SolverTimeWidget.ui

RESOURCES += \
    ../../res.qrc

msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
