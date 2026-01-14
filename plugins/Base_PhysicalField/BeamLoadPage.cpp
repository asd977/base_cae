#include "BeamLoadPage.h"

BeamLoadPage::BeamLoadPage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("梁单元"),
                       QStringLiteral("梁单元（NFG）：LDTYPE + LOCATION + LOAD + ACTION_PLANE"),
                       parent)
{
}
