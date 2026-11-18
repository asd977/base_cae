#pragma once

#include <QDoubleSpinBox>
#include <QLocale>
#include <QString>

// Display a numeric value with up to decimals() digits, but never pad
// insignificant trailing zeros. QDoubleSpinBox still owns the numeric value,
// bounds, editing, rounding and persistence semantics.
class MeshCompactDoubleSpinBox : public QDoubleSpinBox
{
public:
    explicit MeshCompactDoubleSpinBox(QWidget* parent = nullptr)
        : QDoubleSpinBox(parent)
    {
    }

protected:
    QString textFromValue(double value) const override
    {
        QString text = QDoubleSpinBox::textFromValue(value);
        const QString point = locale().decimalPoint(); // Qt 6 returns QString, not QChar.
        if (point.isEmpty())
            return text;
        const qsizetype pointIndex = text.lastIndexOf(point);
        if (pointIndex < 0)
            return text; // Integer, or a number without a fraction.

        // Work only to the right of the decimal point, so '10' never becomes '1'.
        while (text.size() > pointIndex + point.size() && text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(point))
            text.chop(point.size());
        return text;
    }
};
