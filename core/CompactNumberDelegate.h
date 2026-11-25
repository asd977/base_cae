#pragma once

#include <QStyledItemDelegate>
#include "NumericText.h"

// Opt in only on numeric parameter tables. Keep the model's original text for
// editing, saving and solver export; compact only the displayed fixed decimals.
class CompactNumberDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QString displayText(const QVariant& value,const QLocale& locale) const override
    {
        const QString text=QStyledItemDelegate::displayText(value,locale);
        if(value.metaType().id()!=QMetaType::QString || text.contains(QLatin1Char('e'),Qt::CaseInsensitive))return text;
        bool numeric=false;QLocale::c().toDouble(text,&numeric);
        return numeric?NumericText::trimFixedZeros(text,QLocale::c()):text;
    }
};
