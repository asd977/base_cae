#pragma once

#include <QAbstractItemModel>
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
#include <QRegularExpression>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace SolverUiLayoutHelper
{
inline QString semanticEnumText(const QString& text)
{
    static const QRegularExpression codePrefix(
        QStringLiteral(R"(^\s*-?\d+(?:\.\d+)?\s*[-:：]\s*)"));
    QString result = text;
    result.remove(codePrefix);
    return result;
}

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

constexpr int kPageMinimumWidth = 280;
constexpr int kFieldMinimumWidth = 96;
constexpr int kPushButtonMinimumWidth = 96;
constexpr int kTableSectionMinimumWidth = 48;

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

inline void updateComboPopupWidth(QComboBox* combo)
{
    if (!combo || !combo->view())
        return;

    // 弹出列表可以比控件宽，但控件本身仍服从页面布局，不再被长选项撑宽。
    combo->view()->setMinimumWidth(qMax(combo->width(), comboPopupWidth(combo)));
}

inline void bindComboPopupWidth(QComboBox* combo)
{
    if (!combo)
        return;

    static constexpr char kBoundProperty[] = "_solverPopupWidthBound";
    if (!combo->property(kBoundProperty).toBool())
    {
        combo->setProperty(kBoundProperty, true);
        auto refresh = [combo]() { updateComboPopupWidth(combo); };
        QObject::connect(combo->model(), &QAbstractItemModel::rowsInserted,
                         combo, refresh);
        QObject::connect(combo->model(), &QAbstractItemModel::rowsRemoved,
                         combo, refresh);
        QObject::connect(combo->model(), &QAbstractItemModel::modelReset,
                         combo, refresh);
        QObject::connect(combo->model(), &QAbstractItemModel::dataChanged,
                         combo, refresh);
        QObject::connect(combo->model(), &QAbstractItemModel::layoutChanged,
                         combo, refresh);
    }
    updateComboPopupWidth(combo);
}

inline void setHorizontalSizing(QWidget* widget,
                                QSizePolicy::Policy policy,
                                int minimumWidth = 0)
{
    if (!widget)
        return;

    widget->setMinimumWidth(minimumWidth);
    QSizePolicy sizePolicy = widget->sizePolicy();
    sizePolicy.setHorizontalPolicy(policy);
    widget->setSizePolicy(sizePolicy);
}

inline void configureComboSizing(QComboBox* combo)
{
    if (!combo)
        return;

    setHorizontalSizing(combo, QSizePolicy::Expanding, kFieldMinimumWidth);
    QSizePolicy sizePolicy = combo->sizePolicy();
    sizePolicy.setVerticalPolicy(QSizePolicy::Fixed);
    combo->setSizePolicy(sizePolicy);
    combo->setMinimumHeight(28);
    combo->setMinimumContentsLength(0);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMaxVisibleItems(12);
    if (combo->view())
        combo->view()->setTextElideMode(Qt::ElideNone);
    bindComboPopupWidth(combo);
}

inline void polishTable(QTableView* table, int minimumHeight = 120, int maximumHeight = 240)
{
    if (!table)
        return;

    setHorizontalSizing(table, QSizePolicy::Expanding);
    QSizePolicy sizePolicy = table->sizePolicy();
    sizePolicy.setVerticalPolicy(QSizePolicy::Preferred);
    table->setSizePolicy(sizePolicy);
    table->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
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
        // 表格本身可缩窄，列宽不足时使用横向滚动，不让表头反向撑大设置页。
        table->horizontalHeader()->setMinimumSectionSize(kTableSectionMinimumWidth);
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
    const int compactRows = qMax(1, qMin(visibleRows, minimumVisibleRows));
    const int rowHeight = qMax(26, table->verticalHeader()->defaultSectionSize());
    const int headerHeight = table->horizontalHeader()->isVisible()
                                 ? table->horizontalHeader()->height()
                                 : 0;
    const int frame = table->frameWidth() * 2;
    const int compactHeight = headerHeight + compactRows * rowHeight + frame + 4;
    const int preferredHeight = headerHeight + visibleRows * rowHeight + frame + 4;
    table->setMinimumHeight(compactHeight);
    table->setMaximumHeight(preferredHeight);
}

inline void polishPage(QWidget* page)
{
    if (!page)
        return;

    // 页面可以压缩，但必须保留基本可操作宽度。低于该宽度时应由外层 Dock/
    // ScrollArea 阻止继续缩小或显示滚动条，而不是把输入控件压成几像素。
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setMinimumWidth(kPageMinimumWidth);
    page->setMinimumHeight(0);
    page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    if (QLabel* header = page->findChild<QLabel*>(QStringLiteral("pageHeaderLabel")))
        header->hide();

    if (page->layout())
        page->layout()->setSizeConstraint(QLayout::SetDefaultConstraint);

    const auto scrollAreas = page->findChildren<QAbstractScrollArea*>();
    for (QAbstractScrollArea* area : scrollAreas)
    {
        setHorizontalSizing(area, QSizePolicy::Expanding);
        area->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    }

    const auto formLayouts = page->findChildren<QFormLayout*>();
    for (QFormLayout* form : formLayouts)
    {
        form->setContentsMargins(10, 8, 10, 10);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(8);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        // 控件有稳定的最小宽度后，WrapLongRows 只会在页面确实过窄时换行，
        // 不会再因为切换下拉选项而在“一行/两行”之间跳动。
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
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
        setHorizontalSizing(group, QSizePolicy::Expanding);
        QSizePolicy sizePolicy = group->sizePolicy();
        sizePolicy.setVerticalPolicy(QSizePolicy::Maximum);
        group->setSizePolicy(sizePolicy);
        if (group->layout())
        {
            group->layout()->setContentsMargins(10, 10, 10, 10);
            group->layout()->setSpacing(8);
        }
    }

    const auto labels = page->findChildren<QLabel*>();
    for (QLabel* label : labels)
    {
        setHorizontalSizing(label, QSizePolicy::Preferred);
        label->setWordWrap(true);
    }

    const auto combos = page->findChildren<QComboBox*>();
    for (QComboBox* combo : combos)
    {
        for (int i = 0; i < combo->count(); ++i)
            combo->setItemText(i, semanticEnumText(combo->itemText(i)));
        configureComboSizing(combo);
    }

    const auto edits = page->findChildren<QLineEdit*>();
    for (QLineEdit* edit : edits)
    {
        setHorizontalSizing(edit, QSizePolicy::Expanding, kFieldMinimumWidth);
        QSizePolicy sizePolicy = edit->sizePolicy();
        sizePolicy.setVerticalPolicy(QSizePolicy::Fixed);
        edit->setSizePolicy(sizePolicy);
        edit->setMinimumHeight(28);
    }

    const auto spins = page->findChildren<QAbstractSpinBox*>();
    for (QAbstractSpinBox* spin : spins)
    {
        setHorizontalSizing(spin, QSizePolicy::Expanding, kFieldMinimumWidth);
        spin->setMinimumHeight(28);
    }

    // 只给真正的操作按钮设置可点击下限。复选框、单选框不能统一强制 30px 高，
    // 否则参数表单会被无意义地拉松。
    const auto buttons = page->findChildren<QPushButton*>();
    for (QPushButton* button : buttons)
    {
        setHorizontalSizing(button, QSizePolicy::Expanding, kPushButtonMinimumWidth);
        button->setMinimumHeight(30);
    }

    const auto tables = page->findChildren<QTableView*>();
    for (QTableView* table : tables)
        polishTable(table);

    if (QTableView* table = page->findChild<QTableView*>(QStringLiteral("tableExcavationDays")))
        polishTable(table, 150, 260);
    if (QTableView* table = page->findChild<QTableView*>(QStringLiteral("strengthReserveTable")))
        polishTable(table, 150, 224);
    if (QTableView* table = page->findChild<QTableView*>(QStringLiteral("measureTable")))
        polishTable(table, 170, 280);
    if (QTableView* table = page->findChild<QTableView*>(QStringLiteral("tableNodes")))
        polishTable(table, 160, 260);
}

} // namespace SolverUiLayoutHelper
