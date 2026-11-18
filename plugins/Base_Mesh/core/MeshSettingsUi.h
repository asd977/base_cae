#pragma once

#include <QAbstractButton>
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
inline void simplify(QWidget* page) {
    if (!page) return;
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
        widget->installEventFilter(filter);
        if (widget->objectName() == QStringLiteral("meshControlOwnerLabel") ||
            widget->objectName() == QStringLiteral("labelSmeshCurvatureHint") ||
            widget->objectName() == QStringLiteral("labelCartesianInfo") ||
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
