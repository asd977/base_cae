TEMPLATE = subdirs
CONFIG += console

SUBDIRS += \
    launcher \
    plugins \
    core \
    interfaces


launcher.depends += plugins
launcher.depends += core
plugins.depends += core



