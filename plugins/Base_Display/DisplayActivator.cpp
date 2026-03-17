#include "DisplayActivator.h"

#include <QDebug>

#include "DisplayManager.h"

void DisplayActivator::start()
{
    qDebug() << "start Display";
    static DisplayManager* manager = new DisplayManager();
    Q_UNUSED(manager);
}

void DisplayActivator::stop()
{
    qDebug() << "stop Display";
}
