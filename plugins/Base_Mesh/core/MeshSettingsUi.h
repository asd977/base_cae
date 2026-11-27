#pragma once

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLayout>
#include <QLineEdit>
#include <QSizePolicy>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QObject>
#include <QWidget>

namespace MeshSettingsUi {
// Settings show controls and validation errors, without instructional prose or
// hover prompts. Filtering also covers tooltips assigned after page creation.
class PromptFilter final : public QObject {
public:
    explicit PromptFilter(QWidget* page) : QObject(page) {}
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ToolTip || event->type() == QEvent::WhatsThis)
            return true;
        if (event->type() == QEvent::Show && watched->property("mesh.explanation").toBool())
            if (auto* widget = qobject_cast<QWidget*>(watched)) widget->hide();
        return false;
    }
};
inline void hideExplanation(QWidget* widget) {
    if (!widget) return;
    widget->setProperty("mesh.explanation", true);
    widget->hide();
}
inline QString plainText(QString value) {
    value.remove(QStringLiteral("（推荐）"));
    value.remove(QStringLiteral("(推荐)"));
    value.remove(QStringLiteral("（建议）"));
    return value;
}
inline void applyCompactLayout(QLayout* layout) {
    if (!layout) return;
    if (auto* form = qobject_cast<QFormLayout*>(layout)) {
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem* item = form->itemAt(row, QFormLayout::LabelRole);
            if (item && item->widget())
                if (auto* label = qobject_cast<QLabel*>(item->widget())) {
                    label->setWordWrap(true);
                    label->setMinimumWidth(0);
                }
        }
    }
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (!item) continue;
        if (auto* child = item->layout()) applyCompactLayout(child);
        else if (auto* widget = item->widget()) applyCompactLayout(widget->layout());
    }
}
inline void simplify(QWidget* page) {
    if (!page) return;
    // Do not mark the entire page QSizePolicy::Ignored: that allows a dock
    // to squeeze form controls below their usable minimum. Individual views
    // handle overflow using scroll bars; forms wrap their long rows.
    page->setSizePolicy(QSizePolicy::Preferred, page->sizePolicy().verticalPolicy());
    applyCompactLayout(page->layout());
    auto* filter = static_cast<PromptFilter*>(page->findChild<QObject*>(QStringLiteral("meshPromptFilter"), Qt::FindDirectChildrenOnly));
    // PromptFilter has no Q_OBJECT; use the owned QObject identity for lookup.
    if (!filter) {
        filter = new PromptFilter(page);
        filter->setObjectName(QStringLiteral("meshPromptFilter"));
    }
    auto widgets = page->findChildren<QWidget*>();
    widgets.prepend(page);
    for (auto* widget : widgets) {
        widget->setToolTip({}); widget->setStatusTip({}); widget->setWhatsThis({});
        if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combo->setMinimumContentsLength(8);
            combo->setSizePolicy(QSizePolicy::Expanding, combo->sizePolicy().verticalPolicy());
        } else if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget)) {
            spin->setSizePolicy(QSizePolicy::Preferred, spin->sizePolicy().verticalPolicy());
        } else if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
            edit->setSizePolicy(QSizePolicy::Expanding, edit->sizePolicy().verticalPolicy());
        } else if (auto* view = qobject_cast<QAbstractItemView*>(widget)) {
            view->setMinimumWidth(0);
            view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            view->setSizePolicy(QSizePolicy::Expanding, view->sizePolicy().verticalPolicy());
        }
        widget->installEventFilter(filter);
        if (widget->objectName() == QStringLiteral("meshControlOwnerLabel") ||
            widget->objectName() == QStringLiteral("labelSmeshCurvatureHint") ||
            widget->objectName() == QStringLiteral("meshFaceEffectiveSizePreview"))
            hideExplanation(widget);
        if (widget->property("mesh.explanation").toBool()) widget->hide();
        if (auto* combo = qobject_cast<QComboBox*>(widget))
            for (int i=0;i<combo->count();++i) {
                combo->setItemText(i, plainText(combo->itemText(i)));
                combo->setItemData(i, {}, Qt::ToolTipRole);
            }
        if (auto* button = qobject_cast<QAbstractButton*>(widget))
            button->setText(plainText(button->text()));
    }
}
}
