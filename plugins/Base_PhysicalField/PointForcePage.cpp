#include "PointForcePage.h"

PointForcePage::PointForcePage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("集中力"),
                       QStringLiteral("集中力（NFC）：单元号 + 坐标 + Fcx/Fcy/Fcz"),
                       parent)
{
}
