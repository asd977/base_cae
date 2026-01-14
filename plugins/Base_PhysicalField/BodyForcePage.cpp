#include "BodyForcePage.h"

BodyForcePage::BodyForcePage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("体积力"),
                       QStringLiteral("体积力（NFB）：节点编号 + Fbx/Fby/Fbz"),
                       parent)
{
}
