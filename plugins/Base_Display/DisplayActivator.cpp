#include "DisplayActivator.h"

#include <QDebug>

#include "DisplayModule.h"

void DisplayActivator::start()
{
    qDebug() << "start Display";
    static DisplayModule* module = new DisplayModule();
    Q_UNUSED(module);
}

void DisplayActivator::stop()
{
    qDebug() << "stop Display";
}
