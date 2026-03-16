#include "DisplayActivator.h"

#include "DisplayModule.h"

DisplayActivator::DisplayActivator()
{
}

void DisplayActivator::start()
{
    m_displayModule = new DisplayModule;
}

void DisplayActivator::stop()
{
}
