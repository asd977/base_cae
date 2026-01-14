#include "GeoFitPage.h"

GeoFitPage::GeoFitPage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("地应力拟合"),
                       QStringLiteral("拟合边界面（NFA_GEO）：面文件名 + IDIR + HEIGHT + GAMA + PGROUT"),
                       parent)
{
}
