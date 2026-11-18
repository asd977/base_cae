#pragma once

#include <QDoubleSpinBox>
#include <QLocale>

// Display "1" instead of "1.000000000000" while keeping the same number
// of decimal places available for editing and the same stored double value.
// No Q_OBJECT required; safe for Qt Designer/uic promotion.
class CompactPostDoubleSpinBox : public QDoubleSpinBox
{
public:
    explicit CompactPostDoubleSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {}

protected:
    QString textFromValue(double value) const override
    {
        QString text = QDoubleSpinBox::textFromValue(value);
        const auto decimalSeparator = locale().decimalPoint();
        if (text.contains(decimalSeparator))
        {
            while (text.endsWith(QLatin1Char('0')))
                text.chop(1);
            if (text.endsWith(decimalSeparator))
                text.chop(1);
        }
        return text;
    }
};
