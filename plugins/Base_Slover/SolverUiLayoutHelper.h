#pragma once

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace SolverUiLayoutHelper
{
class StableLayoutUpdate
{
  public:
    explicit StableLayoutUpdate(QWidget* page, QWidget* anchor = nullptr)
        : m_page(page), m_anchor(anchor),
          m_updatesEnabled(page ? page->updatesEnabled() : false)
    {
        if (!m_page)
            return;

        for (QWidget* p = m_page->parentWidget(); p; p = p->parentWidget())
        {
            if (auto* area = qobject_cast<QAbstractScrollArea*>(p))
            {
                m_scrollArea = area;
                break;
            }
        }

        if (m_scrollArea)
        {
            m_verticalValue = m_scrollArea->verticalScrollBar()->value();
            m_horizontalValue = m_scrollArea->horizontalScrollBar()->value();
            if (m_anchor)
                m_anchorViewportY = m_anchor->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
        }
        m_page->setUpdatesEnabled(false);
    }

    ~StableLayoutUpdate()
    {
        if (m_page)
        {
            if (m_page->layout())
            {
                m_page->layout()->invalidate();
                m_page->layout()->activate();
            }
            m_page->setUpdatesEnabled(m_updatesEnabled);
            m_page->updateGeometry();
            m_page->update();
        }

        const QPointer<QAbstractScrollArea> area = m_scrollArea;
        const QPointer<QWidget> anchor = m_anchor;
        const int verticalValue = m_verticalValue;
        const int horizontalValue = m_horizontalValue;
        const int anchorViewportY = m_anchorViewportY;
        if (area)
        {
            QTimer::singleShot(0, area, [area, anchor, verticalValue, horizontalValue, anchorViewportY]() {
                if (!area)
                    return;

                int targetVertical = verticalValue;
                if (anchor)
                {
                    const int newAnchorY = anchor->mapTo(area->viewport(), QPoint(0, 0)).y();
                    targetVertical = area->verticalScrollBar()->value() +
                                     (newAnchorY - anchorViewportY);
                }

                area->verticalScrollBar()->setValue(targetVertical);
                area->horizontalScrollBar()->setValue(horizontalValue);
            });
        }
    }

  private:
    QPointer<QWidget> m_page;
    QPointer<QWidget> m_anchor;
    QPointer<QAbstractScrollArea> m_scrollArea;
    int m_verticalValue = 0;
    int m_anchorViewportY = 0;
    int m_horizontalValue = 0;
    bool m_updatesEnabled = true;
};

inline int comboPopupWidth(const QComboBox* combo)
{
    if (!combo)
        return 0;

    const QFontMetrics fm(combo->font());
    int width = 0;
    for (int i = 0; i < combo->count(); ++i)
        width = qMax(width, fm.horizontalAdvance(combo->itemText(i)));
    return qMin(width + 52, 520);
}

inline void polishTable(QTableView* table, int minimumHeight = 120, int maximumHeight = 240)
{
    if (!table)
        return;

    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setMinimumHeight(minimumHeight);
    table->setMaximumHeight(maximumHeight);

    if (table->verticalHeader())
    {
        table->verticalHeader()->setVisible(false);
        table->verticalHeader()->setDefaultSectionSize(28);
        table->verticalHeader()->setMinimumSectionSize(24);
    }
    if (table->horizontalHeader())
    {
        table->horizontalHeader()->setMinimumSectionSize(64);
        table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
}

inline void fitTableToRows(QTableWidget* table,
                           int rowCount,
                           int minimumVisibleRows = 2,
                           int maximumVisibleRows = 6)
{
    if (!table)
        return;

    const int visibleRows = qBound(minimumVisibleRows, rowCount, maximumVisibleRows);
    const int rowHeight = qMax(26, table->verticalHeader()->defaultSectionSize());
    const int headerHeight = table->horizontalHeader()->isVisible()
                                 ? table->horizontalHeader()->height()
                                 : 0;
    const int frame = table->frameWidth() * 2;
    const int height = headerHeight + visibleRows * rowHeight + frame + 4;
    table->setMinimumHeight(height);
    table->setMaximumHeight(height);
}

inline void polishPage(QWidget* page)
{
    if (!page)
        return;

    page->setMinimumWidth(0);
    page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    if (QLabel* header = page->findChild<QLabel*>(QStringLiteral("pageHeaderLabel")))
        header->hide();

    const auto formLayouts = page->findChildren<QFormLayout*>();
    for (QFormLayout* form : formLayouts)
    {
        form->setContentsMargins(10, 8, 10, 10);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(8);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        // 不能使用 WrapLongRows：下拉框文字变长时，表单会在“一行/换行”之间切换，
        // 这是设置面板出现明显跳动的主要原因。
        form->setRowWrapPolicy(QFormLayout::DontWrapRows);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);
    }

    const auto gridLayouts = page->findChildren<QGridLayout*>();
    for (QGridLayout* grid : gridLayouts)
    {
        if (grid->horizontalSpacing() < 0 || grid->horizontalSpacing() > 16)
            grid->setHorizontalSpacing(10);
        if (grid->verticalSpacing() < 0 || grid->verticalSpacing() > 16)
            grid->setVerticalSpacing(8);
    }

    const auto groups = page->findChildren<QGroupBox*>();
    for (QGroupBox* group : groups)
    {
        group->setMinimumWidth(0);
        group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        if (group->layout())
        {
            group->layout()->setContentsMargins(10, 10, 10, 10);
            group->layout()->setSpacing(8);
        }
    }

    const auto combos = page->findChildren<QComboBox*>();
    for (QComboBox* combo : combos)
    {
        combo->setMinimumWidth(0);
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        combo->setMinimumHeight(28);

        // 控件本身始终由布局分配宽度，不再根据当前选项文字重新计算宽度。
        combo->setMinimumContentsLength(0);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMaxVisibleItems(12);
        if (combo->view())
        {
            combo->view()->setTextElideMode(Qt::ElideNone);
            combo->view()->setMinimumWidth(comboPopupWidth(combo));
        }
    }

    const auto edits = page->findChildren<QLineEdit*>();
    for (QLineEdit* edit : edits)
    {
        edit->setMinimumWidth(0);
        edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        edit->setMinimumHeight(28);
    }

    const auto spins = page->findChildren<QAbstractSpinBox*>();
    for (QAbstractSpinBox* spin : spins)
    {
        spin->setMinimumWidth(0);
        spin->setMinimumHeight(28);
    }

    const auto buttons = page->findChildren<QPushButton*>();
    for (QPushButton* button : buttons)
        button->setMinimumHeight(30);

    const auto tables = page->findChildren<QTableView*>();
    for (QTableView* table : tables)
        polishTable(table);

    if (QTableView* table = page->findChild<QTableView*>(QStringLiteral("tableExcavationDays")))
        polishTable(table, 96, 196);
    if (QTableView* table = page->findChild<QTableView*>(QStringLiteral("strengthReserveTable")))
        polishTable(table, 150, 224);
    if (QTableView* table = page->findChild<QTableView*>(QStringLiteral("measureTable")))
        polishTable(table, 170, 280);
    if (QTableView* table = page->findChild<QTableView*>(QStringLiteral("tableNodes")))
        polishTable(table, 160, 260);
}
} // namespace SolverUiLayoutHelper
