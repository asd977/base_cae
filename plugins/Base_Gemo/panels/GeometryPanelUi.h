#pragma once

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QBoxLayout>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QSizePolicy>
#include <QWidget>

namespace GeometryPanelUi
{
inline bool isErrorOrRequiredMessage(const QString& text)
{
    return text.contains(QObject::tr("失败")) ||
           text.contains(QObject::tr("错误")) ||
           text.contains(QObject::tr("无效")) ||
           text.contains(QObject::tr("已删除")) ||
           text.contains(QObject::tr("请选择")) ||
           text.contains(QObject::tr("缺少")) ||
           text.contains(QObject::tr("不能为空"));
}

inline void configureControl(QWidget* widget)
{
    if (!widget)
        return;

    if (auto* combo = qobject_cast<QComboBox*>(widget))
    {
        combo->setMinimumWidth(10);
        combo->setMaxVisibleItems(20);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(0);
        combo->setSizePolicy(QSizePolicy::Expanding, combo->sizePolicy().verticalPolicy());
        return;
    }

    if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget))
    {
        spin->setMinimumWidth(10);
        spin->setKeyboardTracking(false);
        spin->setSizePolicy(QSizePolicy::Expanding, spin->sizePolicy().verticalPolicy());
        return;
    }

    if (auto* edit = qobject_cast<QLineEdit*>(widget))
    {
        edit->setMinimumWidth(10);
        edit->setClearButtonEnabled(false);
        edit->setSizePolicy(QSizePolicy::Expanding, edit->sizePolicy().verticalPolicy());
        return;
    }

    if (auto* view = qobject_cast<QAbstractItemView*>(widget))
    {
        view->setMinimumWidth(10);
        view->setSizePolicy(QSizePolicy::Expanding, view->sizePolicy().verticalPolicy());
        return;
    }

    if (auto* button = qobject_cast<QPushButton*>(widget))
    {
        const QString objectName = button->objectName();
        const QString text = button->text().trimmed();
        if (objectName == QStringLiteral("refreshButton") || text == QObject::tr("刷新对象"))
        {
            button->hide();
            return;
        }

        if (!button->property("preserveActionText").toBool() &&
            (objectName == QStringLiteral("applyButton") ||
            objectName == QStringLiteral("okButton") ||
            objectName == QStringLiteral("generateButton") ||
            text == QObject::tr("生成") || text.startsWith(QObject::tr("生成"))))
        {
            button->setText(QObject::tr("应用"));
        }
        else if (objectName == QStringLiteral("cancelButton"))
        {
            // 几何参数页采用“修改即时保留 + 应用提交”的交互，不再提供容易与
            // “撤销建模”混淆的页面级取消按钮。旧 .ui 中若仍有该控件，统一隐藏。
            button->hide();
            return;
        }
        else if (objectName.startsWith(QStringLiteral("pick"), Qt::CaseInsensitive))
        {
            // “选择”是一次动作，不是一个需要再次点击退出的模式开关。
            // 具体选择期间的提示由 GeometryViewportPickController 临时更新文本。
            button->setCheckable(false);
            if (text.contains(QObject::tr("图形区")))
            {
                if (text.contains(QObject::tr("边")))
                    button->setText(QObject::tr("选择边"));
                else if (text.contains(QObject::tr("面")))
                    button->setText(QObject::tr("选择面"));
                else if (text.contains(QObject::tr("轴")))
                    button->setText(QObject::tr("选择轴"));
                else if (text.contains(QObject::tr("路径")))
                    button->setText(QObject::tr("选择路径"));
                else if (text.contains(QObject::tr("截面")) || text.contains(QObject::tr("轮廓")))
                    button->setText(QObject::tr("选择截面"));
                else
                    button->setText(QObject::tr("选择对象"));
            }
        }

        button->setAutoDefault(false);
        button->setDefault(false);
        if (button->text() == QObject::tr("应用"))
            button->setMinimumWidth(82);
        return;
    }

    if (auto* label = qobject_cast<QLabel*>(widget))
    {
        const QString name = label->objectName().toLower();
        if (name.contains(QStringLiteral("status")) ||
            name.contains(QStringLiteral("hint")) ||
            name.contains(QStringLiteral("info")))
        {
            label->setWordWrap(true);
        }
        if (name.contains(QStringLiteral("status")) && !isErrorOrRequiredMessage(label->text()))
            label->clear();
        return;
    }

    if (qobject_cast<QGroupBox*>(widget))
        widget->setMinimumWidth(0);
}

inline void configureLayout(QLayout* layout)
{
    if (!layout)
        return;

    // 所有几何页面统一紧凑节奏：参数行 8px，区块间 10px。
    layout->setSpacing(8);
    if (auto* form = qobject_cast<QFormLayout*>(layout))
    {
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignTop);
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(8);
    }

    for (int i = 0; i < layout->count(); ++i)
    {
        QLayoutItem* item = layout->itemAt(i);
        if (!item)
            continue;
        if (QWidget* widget = item->widget())
        {
            configureControl(widget);
            configureLayout(widget->layout());
        }
        else if (QLayout* childLayout = item->layout())
        {
            configureLayout(childLayout);
        }
    }
}

inline void apply(QWidget* panel)
{
    if (!panel)
        return;

    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setMinimumWidth(0);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    if (QLayout* root = panel->layout())
    {
        root->setContentsMargins(12, 10, 12, 10);
        root->setSpacing(10);
    }

    const auto pageHeaders = panel->findChildren<QWidget*>(QStringLiteral("pageHeaderLabel"));
    for (QWidget* header : pageHeaders)
        header->hide();

    configureLayout(panel->layout());

    // 部分 .ui 页面中的控件没有直接挂在顶层布局，最后再统一遍历一次。
    const auto controls = panel->findChildren<QWidget*>();
    for (QWidget* control : controls)
        configureControl(control);
}
} // namespace GeometryPanelUi
