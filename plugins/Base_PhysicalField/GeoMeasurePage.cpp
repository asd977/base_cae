#include "GeoMeasurePage.h"

GeoMeasurePage::GeoMeasurePage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("地应力测点"),
                       QStringLiteral("现场地应力测点（NUM_GEO）：坐标 + Sx/Sy/Txy"),
                       parent)
{
}
