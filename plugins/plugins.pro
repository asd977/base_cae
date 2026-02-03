TEMPLATE = subdirs
CONFIG += console
SUBDIRS += \
    Base_UiFrame \
    Base_Output \
    Base_ProjectManager \
    Base_Visualization \
    Base_Gemo \
    Base_Mesh \
    Base_MaterialManager \
    Base_PostProcessing \
#    Base_Slover \
    Base_GlobalParameter \
    Base_SurrogateModel \
    Base_PhysicalField

msvc {
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
