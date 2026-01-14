#include "SurfaceForcePage.h"

SurfaceForcePage::SurfaceForcePage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("面力"),
                       QStringLiteral("面力（NFT）：面节点 + Fx/Fy/Fz"),
                       parent)
{
}
