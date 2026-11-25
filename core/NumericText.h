#pragma once

#include <QLocale>
#include <QString>

namespace NumericText
{
// Presentation only: the caller already chose the numeric precision. Work only
// on a fixed-point fraction, never on the integer digits or exponent.
inline QString trimFixedZeros(QString text,const QLocale& locale)
{
    const QString point=locale.decimalPoint();
    const QString zero=locale.zeroDigit();
    const qsizetype fraction=text.lastIndexOf(point);
    if(point.isEmpty() || zero.isEmpty() || fraction<0)return text;
    while(text.size()>fraction+point.size() && text.endsWith(zero))text.chop(zero.size());
    if(text.endsWith(point))text.chop(point.size());
    return text;
}
}
