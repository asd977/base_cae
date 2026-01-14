#include "TemperaturePage.h"

TemperaturePage::TemperaturePage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("变温"),
                       QStringLiteral("变温（NFW）：节点号 + ALFA + DT"),
                       parent)
{
}
