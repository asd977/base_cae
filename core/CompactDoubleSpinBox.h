#pragma once

#include <QDoubleSpinBox>
#include "NumericText.h"

// Qt still owns validation, decimal precision, stepping, ranges and values.
// Only the painted/editable numeric text omits insignificant trailing zeros.
class CompactDoubleSpinBox : public QDoubleSpinBox
{
public:
    explicit CompactDoubleSpinBox(QWidget* parent=nullptr):QDoubleSpinBox(parent)
    {
        setValue(value());
    }
protected:
    QString textFromValue(double value) const override
    {
        return NumericText::trimFixedZeros(QDoubleSpinBox::textFromValue(value),locale());
    }
};
