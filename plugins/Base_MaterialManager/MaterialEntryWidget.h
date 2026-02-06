#pragma once

#include <BasePage.h>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QMap>

class MaterialManager;
class MaterialModel;
class MaterialEntry;
class CustomComboBox;

class MaterialEntryWidget : public BasePage
{
    Q_OBJECT
public:
    explicit MaterialEntryWidget(MaterialManager *manager, QWidget *parent = nullptr);

    void setEntry(const QString &modelKey, const QString &entryId);

    void refreshAppliedMatypes(const QSet<int> &appliedMatypes, int selectedMatype);

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onNameEdited(const QString &text);
    void onValueEdited(const QString &text);

    void onMatypeChanged(int index);
    void onMatypeActivated(int index);
    void onApplyMatypeClicked();

    void onAppliedMatypeSelected(const QString &text);
    void onRemoveMatypeClicked();

    void onMatypeHovered(int index);
    void onDropdownClosed();

private:
    void rebuildForm(MaterialModel *model, MaterialEntry *entry);
    void clearForm();
    MaterialEntry *lookupEntry(MaterialModel **model = nullptr) const;

    void updateAppliedListColors(); // ★ list 里“预删除/选中”两色

private:
    MaterialManager *m_materialManager;

    QString m_modelKey;
    QString m_entryId;

    bool m_isUpdating;

    QFormLayout *m_formLayout;

    QLineEdit *m_nameEdit;
    QMap<QString, QLineEdit*> m_valueEdits;

    CustomComboBox *m_matypeCombo = nullptr;
    QPushButton *m_applyButton = nullptr;

    QListWidget *m_appliedMatypesList = nullptr;
    QPushButton *m_removeButton = nullptr;

    int m_lastHoveredIndex = -1;

    // ===== 两个临时态（不写入材料数据，只影响高亮）=====
    int m_previewMatype = -1;    // 下拉悬浮
    int m_preDeleteMatype = -1;  // list 选中

    bool m_ignoreNextMatypeChanged = false; // 忽略下拉关闭后可能触发的伪 currentIndexChanged
};
