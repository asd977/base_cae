#pragma once

#include "PostScientificText.h"

#include <QApplication>
#include <QComboBox>
#include <QListView>
#include <QPointer>
#include <QSize>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>

#include <algorithm>

namespace PostComboBoxPerformance
{
class FixedHeightDelegate final : public QStyledItemDelegate
{
public:
    explicit FixedHeightDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        const QString field = index.data(Qt::DisplayRole).toString();
        if (!PostScientificText::hasSubscript(field))
        {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem styled = option;
        initStyleOption(&styled, index);
        const QString shown = styled.text;
        styled.text.clear();
        const QStyle* itemStyle = styled.widget ? styled.widget->style() : QApplication::style();
        itemStyle->drawControl(QStyle::CE_ItemViewItem, &styled, painter, styled.widget);
        const QRect textRect = itemStyle->subElementRect(
            QStyle::SE_ItemViewItemText, &styled, styled.widget).adjusted(2, 0, -2, 0);
        const bool selected = styled.state & QStyle::State_Selected;
        const QColor color = styled.palette.color(
            selected ? QPalette::HighlightedText : QPalette::Text);
        PostScientificText::paint(painter, textRect, shown, styled.font, color);
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(26, option.fontMetrics.height() + 10));
        return size;
    }
};

class FastListView final : public QListView
{
public:
    explicit FastListView(QComboBox* combo)
        : QListView(combo), m_combo(combo)
    {
        setUniformItemSizes(true);
        setLayoutMode(QListView::SinglePass);
        setResizeMode(QListView::Fixed);
        setMovement(QListView::Static);
        setWrapping(false);
        setWordWrap(false);
        setTextElideMode(Qt::ElideRight);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        setItemDelegate(new FixedHeightDelegate(this));
    }

    // QComboBox 在弹出列表时通常会调用这个函数计算内容宽度。
    // 默认实现会遍历所有结果名称；这里直接按控件宽度返回，避免每次展开都扫描模型。
    int sizeHintForColumn(int column) const override
    {
        Q_UNUSED(column);
        return m_combo ? std::max(180, m_combo->width()) : 240;
    }

private:
    QPointer<QComboBox> m_combo;
};
} // namespace PostComboBoxPerformance

inline void configurePostResultCombo(QComboBox* combo)
{
    if (!combo)
        return;

    // Windows/Qt 的下拉动画本身会带来明显的“慢半拍”感。
    // 这里只关闭组合框动画，不影响窗口、菜单等其他动画效果。
    static const bool comboAnimationDisabled = []() {
        QApplication::setEffectEnabled(Qt::UI_AnimateCombo, false);
        return true;
    }();
    Q_UNUSED(comboAnimationDisabled);

    combo->setEditable(false);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(16);
    combo->setMaxVisibleItems(12);

    if (!combo->property("postResultFastView").toBool())
    {
        combo->setView(new PostComboBoxPerformance::FastListView(combo));
        combo->setProperty("postResultFastView", true);
    }
}
