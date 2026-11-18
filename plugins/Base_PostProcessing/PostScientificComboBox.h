#pragma once

#include "PostComboBoxPerformance.h"
#include "PostScientificText.h"

#include <QComboBox>
#include <QPaintEvent>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStylePainter>

// QComboBox (including Qt 6.7) draws its closed label as plain text; a popup
// item delegate alone does not change it.  Paint just the mathematical text
// ourselves while keeping QComboBox's native frame, arrow and interactions.
// No Q_OBJECT needed: the inherited signals and slots are unchanged.
class PostScientificComboBox : public QComboBox
{
public:
    explicit PostScientificComboBox(QWidget* parent = nullptr)
        : QComboBox(parent)
    {
        setItemDelegate(new PostComboBoxPerformance::FixedHeightDelegate(this));
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        const QString shown = currentText();
        if (!PostScientificText::hasSubscript(shown))
        {
            QComboBox::paintEvent(event);
            return;
        }

        QStylePainter painter(this);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        option.currentText.clear();
        painter.drawComplexControl(QStyle::CC_ComboBox, option);
        painter.drawControl(QStyle::CE_ComboBoxLabel, option);

        const QRect labelRect = style()->subControlRect(
            QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, this);
        const QColor textColor = palette().color(
            isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
        PostScientificText::paint(&painter, labelRect.adjusted(2, 0, -2, 0),
                                  shown, font(), textColor);
    }
};
