#pragma once

#include <BasePage.h>
#include <struct/MeshMap.h>

#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QPushButton>
#include <QSet>

class MaterialManager;
class MaterialModel;
class MaterialEntry;
class CustomComboBox;
class QCheckBox;
class QSpinBox;

class MaterialEntryWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit MaterialEntryWidget(MaterialManager* manager, QWidget* parent = nullptr);
    ~MaterialEntryWidget() override;

    void setEntry(const QString& modelKey, const QString& entryId);

    void refreshAppliedDomainTags(const QSet<MeshTagKey>& appliedTags,
                                  const MeshTagKey& selectedKey);

    void beforeLoad() override;
    void beforeUnload() override;

    void onTransitionDomainChanged(int value);
  private slots:
    void onNameEdited(const QString& text);
    void onValueEdited(const QString& text);
    void onSeepageModelChanged(int index);
    void onSeepageValueEdited(const QString& text);

    void onDomainChanged(int index);
    void onDomainActivated(int index);
    void onApplyDomainClicked();

    void onAppliedDomainSelected(const QString& text);
    void onRemoveDomainClicked();

    void onDomainHovered(int index);
    void onDropdownClosed();

    void onTransitionModeChanged(int state);

  private:
    void rebuildForm(MaterialModel* model, MaterialEntry* entry);
    void rebuildSeepageFields(MaterialEntry* entry);
    void clearForm();
    MaterialEntry* lookupEntry(MaterialModel** model = nullptr) const;

    void updateAppliedListColors();

  private:
    MaterialManager* m_materialManager = nullptr;

    QString m_modelKey;
    QString m_entryId;

    bool m_isUpdating = false;
    bool m_ignoreNextDomainChanged = false;
    bool m_beforeUnloadHandled = false;

    QFormLayout* m_formLayout = nullptr;

    QLineEdit* m_nameEdit = nullptr;
    QMap<QString, QLineEdit*> m_valueEdits;

    CustomComboBox* m_seepageCombo = nullptr;
    QFormLayout* m_seepageFieldsLayout = nullptr;
    QMap<QString, QLineEdit*> m_seepageValueEdits;

    CustomComboBox* m_domainCombo = nullptr;
    QPushButton* m_applyDomainButton = nullptr;

    QListWidget* m_appliedDomainList = nullptr;
    QPushButton* m_removeDomainButton = nullptr;

    QCheckBox* m_transitionOnlyCheck = nullptr;
    QSpinBox* m_transitionMatypeSpin = nullptr;

    int m_lastHoveredIndex = -1;

    MeshTagKey m_previewDomainTag;
    MeshTagKey m_preDeleteDomainTag;
};
