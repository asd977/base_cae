#pragma once

#include <BasePage.h>

#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QPushButton>

class MaterialManager;
class MaterialModel;
class MaterialEntry;
class CustomComboBox;
class QComboBox;
class QCheckBox;
class QSpinBox;
struct DomainKey;

class MaterialEntryWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit MaterialEntryWidget(MaterialManager* manager, QWidget* parent = nullptr);
    ~MaterialEntryWidget() override;

    void setEntry(const QString& modelKey, const QString& entryId);

    void refreshAppliedMatypes(const QSet<int>& appliedMatypes, int selectedMatype);

    void beforeLoad() override;
    void beforeUnload() override;

  private slots:
    void onNameEdited(const QString& text);
    void onNameEditingFinished();
    void onMaterialModelActivated(int index);
    void onValueEdited(const QString& text);
    void onOptionValueChanged(int index);
    void onSeepageModelChanged(int index);
    void onCreepModelChanged(int index);
    void onSeepageValueEdited(const QString& text);

    void onMatypeChanged(int index);
    void onMatypeActivated(int index);
    void onApplyMatypeClicked();

    void onAppliedMatypeSelected(const QString& text);
    void onRemoveMatypeClicked();

    void onMatypeHovered(int index);
    void onDropdownClosed();
    void onTransitionModeChanged(int state);
    void onTransitionMatypeChanged(int value);
    void onDomainPicked(const QString& domain, const QString& uuid, const DomainKey& key);

  private:
    void ensurePickInteractionReady();
    void refreshMatypeHighlightSafe();
    void ensureMatypeOptionsLoaded();
    void appendMatypeOption(int matype);
    void rebuildForm(MaterialModel* model, MaterialEntry* entry);
    void rebuildSeepageFields(MaterialEntry* entry);
    void rebuildCreepFields(MaterialEntry* entry);
    void clearForm();
    MaterialEntry* lookupEntry(MaterialModel** model = nullptr) const;

    void updateAppliedListColors(); // ★ list 里“预删除/选中”两色

  private:
    MaterialManager* m_materialManager;

    QString m_modelKey;
    QString m_entryId;

    bool m_isUpdating;
    bool m_transitionChangeInProgress = false;

    QFormLayout* m_formLayout;

    QLineEdit* m_nameEdit;
    QComboBox* m_modelCombo = nullptr;
    QMap<QString, QLineEdit*> m_valueEdits;
    CustomComboBox* m_seepageCombo = nullptr;
    QWidget* m_seepageFieldsWidget = nullptr;
    QFormLayout* m_seepageFieldsLayout = nullptr;
    QMap<QString, QLineEdit*> m_seepageValueEdits;
    CustomComboBox* m_creepCombo = nullptr;
    QWidget* m_creepFieldsWidget = nullptr;
    QFormLayout* m_creepFieldsLayout = nullptr;
    QMap<QString, QLineEdit*> m_creepValueEdits;

    CustomComboBox* m_matypeCombo = nullptr;
    QPushButton* m_applyButton = nullptr;

    QListWidget* m_appliedMatypesList = nullptr;
    QPushButton* m_removeButton = nullptr;
    QCheckBox* m_transitionOnlyCheck = nullptr;
    QSpinBox* m_transitionMatypeSpin = nullptr;

    int m_lastHoveredIndex = -1;

    // ===== 两个临时态（不写入材料数据，只影响高亮）=====
    int m_previewMatype = -1;   // 下拉悬浮
    int m_preDeleteMatype = -1; // list 选中

    bool m_matypeDropdownOpen = false;
    bool m_ignoreNextMatypeChanged = false; // 忽略下拉关闭后可能触发的伪 currentIndexChanged
    bool m_beforeUnloadHandled = false;
    bool m_matypeOptionsLoaded = false;
    bool m_pickInteractionReady = false;
};
