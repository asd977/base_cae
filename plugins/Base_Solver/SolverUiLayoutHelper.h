#pragma once

#include <BasePage.h>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QBoxLayout>
#include <QComboBox>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
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

inline bool replaceWidgetInLayout(QLayout* layout,
                                  QWidget* oldWidget,
                                  QWidget* replacement)
{
    if (!layout || !oldWidget || !replacement)
        return false;

    for (int index = 0; index < layout->count(); ++index)
    {
        QLayoutItem* item = layout->itemAt(index);
        if (!item)
            continue;

        if (item->widget() == oldWidget)
        {
            if (auto* box = qobject_cast<QBoxLayout*>(layout))
            {
                box->removeWidget(oldWidget);
                box->insertWidget(index, replacement);
            }
            else if (auto* grid = qobject_cast<QGridLayout*>(layout))
            {
                int row = 0;
                int column = 0;
                int rowSpan = 1;
                int columnSpan = 1;
                grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
                grid->removeWidget(oldWidget);
                grid->addWidget(replacement, row, column, rowSpan, columnSpan);
            }
            else if (auto* form = qobject_cast<QFormLayout*>(layout))
            {
                int row = -1;
                QFormLayout::ItemRole role = QFormLayout::SpanningRole;
                form->getWidgetPosition(oldWidget, &row, &role);
                if (row < 0)
                    return false;
                form->removeWidget(oldWidget);
                form->setWidget(row, role, replacement);
            }
            else
            {
                layout->removeWidget(oldWidget);
                layout->addWidget(replacement);
            }
            return true;
        }

        if (item->layout() && replaceWidgetInLayout(item->layout(), oldWidget, replacement))
            return true;
    }

    return false;
}

inline QString computeDeviceSettingsKey(QWidget* page)
{
    QString pageId;
    if (auto* basePage = qobject_cast<BasePage*>(page))
        pageId = basePage->getUuid();
    if (pageId.trimmed().isEmpty() && page)
        pageId = QString::fromLatin1(page->metaObject()->className());
    return QStringLiteral("Solver/ComputeDevice/%1").arg(pageId);
}

class ComputeControlsBinder final : public QObject
{
  public:
    ComputeControlsBinder(QWidget* page,
                          QPushButton* numericalButton,
                          QPushButton* standardButton,
                          QComboBox* deviceCombo)
        : QObject(page),
          m_page(page),
          m_numericalButton(numericalButton),
          m_standardButton(standardButton),
          m_deviceCombo(deviceCombo)
    {
        if (m_page)
            m_page->installEventFilter(this);
        if (m_numericalButton)
            m_numericalButton->installEventFilter(this);

        if (m_deviceCombo)
        {
            QObject::connect(m_deviceCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                             this, [this](int) { persistDevice(); });
        }

        QTimer::singleShot(0, this, [this]() {
            restoreDevice();
            syncComputingState();
        });
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_numericalButton &&
            event->type() == QEvent::DynamicPropertyChange)
        {
            auto* propertyEvent = static_cast<QDynamicPropertyChangeEvent*>(event);
            if (propertyEvent->propertyName() == QByteArrayLiteral("computing"))
                QTimer::singleShot(0, this, [this]() { syncComputingState(); });
        }
        else if (watched == m_page && event->type() == QEvent::Show)
        {
            restoreDevice();
            QTimer::singleShot(0, this, [this]() { syncComputingState(); });
        }

        return QObject::eventFilter(watched, event);
    }

  private:
    void restoreDevice()
    {
        if (!m_page || !m_deviceCombo)
            return;

        QSettings settings;
        const QString device = settings.value(computeDeviceSettingsKey(m_page),
                                              QStringLiteral("cpu"))
                                   .toString()
                                   .toLower();
        int index = m_deviceCombo->findData(device);
        if (index < 0)
            index = m_deviceCombo->findData(QStringLiteral("cpu"));

        m_restoring = true;
        if (index >= 0)
            m_deviceCombo->setCurrentIndex(index);
        m_restoring = false;

        m_page->setProperty("solverComputeDevice",
                            m_deviceCombo->currentData().toString());
    }

    void persistDevice()
    {
        if (m_restoring || !m_page || !m_deviceCombo)
            return;

        const QString device = m_deviceCombo->currentData().toString();
        m_page->setProperty("solverComputeDevice", device);

        QSettings settings;
        settings.setValue(computeDeviceSettingsKey(m_page), device);
    }

    void syncComputingState()
    {
        if (!m_numericalButton)
            return;

        const bool computing = m_numericalButton->property("computing").toBool();
        m_numericalButton->setText(computing ? QStringLiteral("停止计算")
                                             : QStringLiteral("数值法计算"));
        if (m_standardButton)
            m_standardButton->setEnabled(!computing);
        if (m_deviceCombo)
            m_deviceCombo->setEnabled(!computing);
    }

    QPointer<QWidget> m_page;
    QPointer<QPushButton> m_numericalButton;
    QPointer<QPushButton> m_standardButton;
    QPointer<QComboBox> m_deviceCombo;
    bool m_restoring = false;
};

inline QString computeDevice(const QWidget* page)
{
    if (!page)
        return QStringLiteral("cpu");

    const QVariant value = page->property("solverComputeDevice");
    if (value.isValid() && !value.toString().trimmed().isEmpty())
        return value.toString().toLower();

    if (auto* combo = page->findChild<QComboBox*>(QStringLiteral("comboComputeDevice")))
        return combo->currentData().toString().toLower();

    return QStringLiteral("cpu");
}

inline void setupComputeControls(QWidget* page)
{
    if (!page || page->property("_solverComputeControlsReady").toBool())
        return;

    QPushButton* numericalButton =
        page->findChild<QPushButton*>(QStringLiteral("btnCompute"));
    if (!numericalButton)
        numericalButton = page->findChild<QPushButton*>(QStringLiteral("computeBtn"));
    if (!numericalButton || !page->layout())
        return;

    auto* panel = new QWidget(numericalButton->parentWidget());
    panel->setObjectName(QStringLiteral("solverComputeControls"));
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(8);

    auto* deviceRow = new QHBoxLayout();
    deviceRow->setContentsMargins(0, 0, 0, 0);
    deviceRow->setSpacing(10);

    auto* deviceLabel = new QLabel(QStringLiteral("计算设备"), panel);
    auto* deviceCombo = new QComboBox(panel);
    deviceCombo->setObjectName(QStringLiteral("comboComputeDevice"));
    deviceCombo->addItem(QStringLiteral("CPU"), QStringLiteral("cpu"));
    deviceCombo->addItem(QStringLiteral("GPU（接口预留）"), QStringLiteral("gpu"));
    deviceCombo->setToolTip(
        QStringLiteral("CPU/GPU 仅作为计算设备接口预留，当前数值法仍使用现有求解流程。"));

    deviceRow->addWidget(deviceLabel);
    deviceRow->addWidget(deviceCombo, 1);
    panelLayout->addLayout(deviceRow);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(10);

    auto* standardButton = new QPushButton(QStringLiteral("规范法计算"), panel);
    standardButton->setObjectName(QStringLiteral("btnStandardCompute"));
    standardButton->setProperty("computeMethod", QStringLiteral("standard"));
    standardButton->setToolTip(QStringLiteral("规范法计算接口预留，当前尚未接入实际计算。"));

    numericalButton->setProperty("computeMethod", QStringLiteral("numerical"));
    numericalButton->setText(QStringLiteral("数值法计算"));

    if (!replaceWidgetInLayout(page->layout(), numericalButton, panel))
    {
        delete panel;
        return;
    }

    buttonRow->addWidget(numericalButton, 1);
    buttonRow->addWidget(standardButton, 1);
    panelLayout->addLayout(buttonRow);

    QObject::connect(standardButton, &QPushButton::clicked, page, [page]() {
        QMessageBox::information(
            page,
            QStringLiteral("规范法计算"),
            QStringLiteral("规范法计算接口已预留，当前尚未接入实际计算。"));
    });

    new ComputeControlsBinder(page, numericalButton, standardButton, deviceCombo);
    page->setProperty("_solverComputeControlsReady", true);
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
