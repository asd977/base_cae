include($$PWD/plugin-dev.pri)

QT += widgets core charts

INCLUDEPATH += $$PWD/../../interfaces

HEADERS += \
    DNNModel.h \
    DatasetEvaluation.h \
    LatinHypercubeSampling.h \
    OrthogonalSampling.h \
    SurrogateModel.h

SOURCES += \
    DNNModel.cpp \
    DatasetEvaluation.cpp \
    LatinHypercubeSampling.cpp \
    OrthogonalSampling.cpp \
    SurrogateModel.cpp


FORMS += \
    DNNModel.ui \
    DatasetEvaluation.ui \
    LatinHypercubeSampling.ui \
    OrthogonalSampling.ui


RESOURCES += \
    ../../res.qrc


msvc
{
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
