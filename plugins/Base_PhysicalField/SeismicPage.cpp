#include "SeismicPage.h"

SeismicPage::SeismicPage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("地震"),
                       QStringLiteral("地震加速度（NTBX/NTBY/NTBZ）：三向时间序列"),
                       parent)
{
}
