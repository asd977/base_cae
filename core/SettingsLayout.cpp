#include "SettingsLayout.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QRadioButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStyleOptionSpinBox>
#include <QTimer>
#include <algorithm>

namespace CaeSettingsLayout
{
namespace
{
int fieldWidth(const QWidget* widget)
{
    const QFontMetrics fm(widget->font(),widget);
    const int frame=widget->style()->pixelMetric(QStyle::PM_DefaultFrameWidth,nullptr,widget);
    return fm.horizontalAdvance(QStringLiteral("-000000.000000"))+fm.height()+2*frame;
}

void sizeEditor(QWidget* widget)
{
    if(widget->property("settingsPreserveSize").toBool())return;
    const QFontMetrics fm(widget->font(),widget);
    int width=fieldWidth(widget);
    if(auto* spin=qobject_cast<QAbstractSpinBox*>(widget)) {
        QStyleOptionSpinBox option;option.initFrom(spin);option.rect=spin->rect();
        const int buttons=spin->style()->subControlRect(QStyle::CC_SpinBox,&option,QStyle::SC_SpinBoxUp,spin).width();
        QString sample=QStringLiteral("-000000");
        if(auto* number=qobject_cast<QDoubleSpinBox*>(spin)) {
            const QString fraction=number->decimals()>0?QStringLiteral(".")+QString(std::min(3,number->decimals()),QLatin1Char('0')):QString();
            sample=number->prefix()+QStringLiteral("-000000")+fraction+number->suffix();
        }
        else if(auto* number=qobject_cast<QSpinBox*>(spin))
            sample=number->prefix()+sample+number->suffix();
        width=std::max(fm.horizontalAdvance(sample),fm.horizontalAdvance(spin->text()))+fm.height()+buttons;
    } else if(auto* combo=qobject_cast<QComboBox*>(widget)) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(0);
        QStyleOptionComboBox option;option.initFrom(combo);option.rect=combo->rect();
        const int arrow=combo->style()->subControlRect(QStyle::CC_ComboBox,&option,QStyle::SC_ComboBoxArrow,combo).width();
        width=fm.horizontalAdvance(QStringLiteral("参数选项 / 0000"))+fm.height()+arrow;
    } else if(auto* editor=qobject_cast<QLineEdit*>(widget);editor && qobject_cast<const QDoubleValidator*>(editor->validator())) {
        const int frame=widget->style()->pixelMetric(QStyle::PM_DefaultFrameWidth,nullptr,widget);
        width=std::max(fm.horizontalAdvance(QStringLiteral("-0.000000")),fm.horizontalAdvance(editor->text()))+fm.height()+2*frame;
    }
    // Ignore range/item-derived hints, but enforce enough room for readable text.
    auto policy=widget->sizePolicy();policy.setHorizontalPolicy(QSizePolicy::Expanding);
    widget->setSizePolicy(policy);
    if(widget->minimumWidth()!=width)widget->setMinimumWidth(width);
    widget->setMaximumWidth(QWIDGETSIZE_MAX);
}

void sizeLabel(QLabel* label)
{
    // Fixed is a size policy, not a pixel width: Qt measures the current text,
    // font, rich text and style. The field column receives the remaining space.
    label->setMinimumWidth(0);
    label->setMaximumWidth(QWIDGETSIZE_MAX);
    label->setWordWrap(false);
    label->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Preferred);
    label->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
}

void sizeGrid(QGridLayout* grid,QWidget* page)
{
    QSet<int> fieldColumns,labelColumns;
    for(int i=0;i<grid->count();++i) {
        int row,column,rowSpan,columnSpan;
        grid->getItemPosition(i,&row,&column,&rowSpan,&columnSpan);
        if(columnSpan!=1)continue;
        auto* widget=grid->itemAt(i)->widget();
        if(widget && !widget->isVisibleTo(page))continue;
        if(qobject_cast<QLabel*>(widget) || qobject_cast<QCheckBox*>(widget) || qobject_cast<QRadioButton*>(widget))
            labelColumns.insert(column);
        else if(widget || grid->itemAt(i)->layout())fieldColumns.insert(column);
    }
    // Read-only name/value grids use QLabel for their value cells as well.
    if(fieldColumns.isEmpty() && labelColumns.contains(0) && labelColumns.contains(1)) {
        fieldColumns.insert(1);labelColumns.remove(1);
    }
    if(fieldColumns.isEmpty())return;
    for(int column=0;column<grid->columnCount();++column)
        grid->setColumnStretch(column,fieldColumns.contains(column)?1:0);
    for(int i=0;i<grid->count();++i) {
        int row,column,rowSpan,columnSpan;
        grid->getItemPosition(i,&row,&column,&rowSpan,&columnSpan);
        if(columnSpan!=1 || fieldColumns.contains(column) || !labelColumns.contains(column))continue;
        auto* widget=grid->itemAt(i)->widget();if(!widget)continue;
        grid->setColumnMinimumWidth(column,0);
        if(auto* label=qobject_cast<QLabel*>(widget))sizeLabel(label);
        else {
            auto policy=widget->sizePolicy();policy.setHorizontalPolicy(QSizePolicy::Fixed);widget->setSizePolicy(policy);
        }
    }
}

class PagePolicy final : public QObject
{
public:
    explicit PagePolicy(QWidget* page):QObject(page),m_page(page)
    {
        setObjectName(QStringLiteral("settingsLayoutPolicy"));m_update.setSingleShot(true);
        connect(&m_update,&QTimer::timeout,this,[this]{if(m_page && m_page->isVisible())apply();});
    }

    void apply()
    {
        if(!m_page || m_applying)return;
        m_applying=true;
        m_page->setMinimumWidth(0);m_page->setMaximumWidth(QWIDGETSIZE_MAX);
        if(m_page->layout())m_page->layout()->setSizeConstraint(QLayout::SetMinimumSize);
        auto widgets=m_page->findChildren<QWidget*>();widgets.prepend(m_page);
        for(auto* widget:widgets) {
            // Popup sizing belongs to the combo's native view, not the form.
            if(widget->window()->windowType()==Qt::Popup || widget->window()->windowType()==Qt::ToolTip)continue;
            widget->ensurePolished();
            widget->installEventFilter(this);
            if(auto* spin=qobject_cast<QAbstractSpinBox*>(widget)) {
                sizeEditor(spin);
                if(!spin->property("settingsMetricWidthManaged").toBool()) {
                    spin->setProperty("settingsMetricWidthManaged",true);
                    if(auto* number=qobject_cast<QDoubleSpinBox*>(spin))
                        connect(number,QOverload<double>::of(&QDoubleSpinBox::valueChanged),number,[number](double){sizeEditor(number);});
                    else if(auto* number=qobject_cast<QSpinBox*>(spin))
                        connect(number,QOverload<int>::of(&QSpinBox::valueChanged),number,[number](int){sizeEditor(number);});
                }
            } else if(auto* combo=qobject_cast<QComboBox*>(widget)) {
                sizeEditor(combo);
                if(combo->toolTip().isEmpty() && !combo->property("settingsSelectionTip").toBool()) {
                    combo->setProperty("settingsSelectionTip",true);combo->setToolTip(combo->currentText());
                    connect(combo,&QComboBox::currentTextChanged,combo,&QWidget::setToolTip);
                }
            } else if(auto* editor=qobject_cast<QLineEdit*>(widget)) {
                if(!qobject_cast<QAbstractSpinBox*>(editor->parentWidget())) {
                    sizeEditor(editor);
                    if(qobject_cast<const QDoubleValidator*>(editor->validator()) && !editor->property("settingsMetricWidthManaged").toBool()) {
                        editor->setProperty("settingsMetricWidthManaged",true);
                        connect(editor,&QLineEdit::textChanged,editor,[editor]{sizeEditor(editor);});
                    }
                }
            } else if(auto* view=qobject_cast<QAbstractItemView*>(widget)) {
                // Tables/lists provide their own scrolling; they do not set dock width.
                view->setMinimumWidth(0);
                auto policy=view->sizePolicy();policy.setHorizontalPolicy(QSizePolicy::Expanding);view->setSizePolicy(policy);
            } else if(auto* label=qobject_cast<QLabel*>(widget)) {
                if(label->objectName()==QStringLiteral("materialUnitLabel"))continue;
                if(!label->text().isEmpty() && !label->pixmap().isNull())continue;
                if(label->text().contains(QStringLiteral("提示")) || label->objectName().contains(QStringLiteral("Hint")))
                    label->setWordWrap(true);
            }
        }
        const auto forms=m_page->findChildren<QFormLayout*>();
        const QFontMetrics fm(m_page->font(),m_page);
        const auto units=m_page->findChildren<QLabel*>(QStringLiteral("materialUnitLabel"));
        const auto unitScope=[this](QLabel* label) {
            QWidget* parent=label->parentWidget();
            while(parent && parent!=m_page && !qobject_cast<QGroupBox*>(parent))parent=parent->parentWidget();
            return parent;
        };
        QHash<QWidget*,int> unitWidths;
        for(auto* label:units)
            if(label->isVisibleTo(m_page))unitWidths[unitScope(label)]=std::max(unitWidths.value(unitScope(label)),label->sizeHint().width());
        for(auto* label:units) {
            const int unitWidth=unitWidths.value(unitScope(label));
            label->setMinimumWidth(unitWidth);
            label->setMaximumWidth(unitWidth);
        }
        for(auto* form:forms) {
            // Dynamic forms keep their add/remove-row API and the same two-column
            // behavior as a grid. Narrow docks scroll rather than stacking rows.
            form->setRowWrapPolicy(QFormLayout::DontWrapRows);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            form->setLabelAlignment(Qt::AlignLeft|Qt::AlignVCenter);
            form->setFormAlignment(Qt::AlignLeft|Qt::AlignTop);
            form->setHorizontalSpacing(std::max(1,fm.horizontalAdvance(QStringLiteral("  "))));
            form->setVerticalSpacing(std::max(1,fm.height()/3));
            for(int row=0;row<form->rowCount();++row) {
                auto* item=form->itemAt(row,QFormLayout::LabelRole);
                auto* label=item?qobject_cast<QLabel*>(item->widget()):nullptr;
                if(!label)continue;
                sizeLabel(label);
            }
        }
        for(auto* grid:m_page->findChildren<QGridLayout*>())sizeGrid(grid,m_page);
        for(auto* scroll:m_page->findChildren<QScrollArea*>()) {
            scroll->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Expanding);
            scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            if(auto* content=scroll->widget())content->setMinimumWidth(readableWidth(content));
        }
        const auto layouts=m_page->findChildren<QLayout*>();
        for(auto it=layouts.crbegin();it!=layouts.crend();++it)(*it)->invalidate();
        if(m_page->layout())m_page->layout()->activate();
        m_page->updateGeometry();m_applying=false;
    }

protected:
    bool eventFilter(QObject*,QEvent* event) override
    {
        if(!m_applying && (event->type()==QEvent::FontChange || event->type()==QEvent::ApplicationFontChange ||
            event->type()==QEvent::StyleChange || event->type()==QEvent::ChildAdded ||
            event->type()==QEvent::ChildRemoved || event->type()==QEvent::Show || event->type()==QEvent::Hide))
            m_update.start(0);
        return false;
    }
private:
    QPointer<QWidget> m_page;
    QTimer m_update;
    bool m_applying{false};
};
}

int readableWidth(const QWidget* widget)
{
    const QFontMetrics fm(widget->font(),widget);
    const int margin=widget->style()->pixelMetric(QStyle::PM_LayoutLeftMargin,nullptr,widget);
    return fieldWidth(widget)+fm.horizontalAdvance(QStringLiteral("参数名称"))+fm.height()+4*std::max(0,margin);
}

void prepare(QWidget* page)
{
    if(!page)return;
    auto* policy=dynamic_cast<PagePolicy*>(page->findChild<QObject*>(QStringLiteral("settingsLayoutPolicy"),Qt::FindDirectChildrenOnly));
    if(!policy)policy=new PagePolicy(page);
    policy->apply();
}

ActivePageStack::ActivePageStack(QWidget* parent):QStackedWidget(parent)
{
    connect(this,&QStackedWidget::currentChanged,this,[this](int){updateGeometry();});
}
QSize ActivePageStack::sizeHint() const
{
    return currentWidget()?currentWidget()->sizeHint().expandedTo(minimumSizeHint()):QSize(0,0);
}
QSize ActivePageStack::minimumSizeHint() const
{
    auto* page=currentWidget();if(!page)return {0,0};
    const auto hint=page->minimumSizeHint();
    const int floor=page->findChildren<QScrollArea*>().isEmpty()?readableWidth(page):0;
    return {std::max({0,floor,hint.width()}),std::max(0,hint.height())};
}
bool ActivePageStack::hasHeightForWidth() const
{
    return currentWidget() && currentWidget()->hasHeightForWidth();
}
int ActivePageStack::heightForWidth(int width) const
{
    auto* page=currentWidget();if(!page)return 0;
    const int height=page->heightForWidth(width);
    return height>=0?height:page->sizeHint().height();
}
}
