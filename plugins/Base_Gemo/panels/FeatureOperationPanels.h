#pragma once

#include <BasePage.h>
#include <CadTypes.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonObject>
#include <QLabel>
#include <QSet>
#include <QStringList>

class QListWidget;
class QPushButton;
class QCheckBox;

class RevolveDialog : public BasePage
{
    Q_OBJECT
public:
    explicit RevolveDialog(QWidget* parent = nullptr);
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void setInitialSource(const QString& uuid);

private slots:
    void onApplyClicked();

private:
    void refreshSources();
    void updateStatus(const QString& text, bool ok);
    void updatePreview();
    void clearPreview(bool renderNow = true);
    void updateAxisPreview();
    void clearAxisPreview(bool renderNow = true);
    void startAutomaticSourcePick();
    void startAutomaticAxisPick();
    void syncSourceHighlight();
    bool setSourceFromFace(const QString& uuid, int faceIndex);
    bool setAxisFromEdge(const QString& uuid, int edgeIndex);
    RevolveParameters parameters() const;
    QComboBox* m_sourceCombo = nullptr;
    QComboBox* m_axisCombo = nullptr;
    QDoubleSpinBox* m_angleSpin = nullptr;
    QDoubleSpinBox* m_oxSpin = nullptr;
    QDoubleSpinBox* m_oySpin = nullptr;
    QDoubleSpinBox* m_ozSpin = nullptr;
    QDoubleSpinBox* m_axisDxSpin = nullptr;
    QDoubleSpinBox* m_axisDySpin = nullptr;
    QDoubleSpinBox* m_axisDzSpin = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_applyButton = nullptr;
    QCheckBox* m_previewCheck = nullptr;
    QString m_sourceUuid;
    int m_sourceFaceIndex = 0;
    QJsonObject m_committedState;
};



class SweepDialog : public BasePage
{
    Q_OBJECT
public:
    explicit SweepDialog(QWidget* parent = nullptr);
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void setInitialProfile(const QString& uuid);
    void setInitialPath(const QString& uuid);

private slots:
    void onApplyClicked();
    void onCancelClicked();

private:
    void refreshSources();
    void updateStatus(const QString& text, bool ok);
    void syncHighlight();
    void updatePreview();
    void clearPreview(bool renderNow = true);
    void updateUiState();
    QComboBox* m_profileCombo = nullptr;
    QComboBox* m_pathCombo = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_applyButton = nullptr;
    QString m_profileUuid;
    QString m_pathUuid;
    QJsonObject m_committedState;
};

class LoftDialog : public BasePage
{
    Q_OBJECT
public:
    explicit LoftDialog(QWidget* parent = nullptr);
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void addInitialProfile(const QString& uuid);

private slots:
    void onApplyClicked();
    void onCancelClicked();

private:
    void refreshSources();
    void addCurrentProfile();
    void removeSelectedProfile();
    void clearProfiles();
    void refreshSelectedList();
    void updateStatus(const QString& text, bool ok);
    void syncHighlight();
    void updatePreview();
    void clearPreview(bool renderNow = true);
    QComboBox* m_profileCombo = nullptr;
    QListWidget* m_profileList = nullptr;
    QCheckBox* m_solidCheck = nullptr;
    QCheckBox* m_ruledCheck = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_applyButton = nullptr;
    QStringList m_profileUuids;
    QJsonObject m_committedState;
};

class CutExtrudeDialog : public BasePage
{
    Q_OBJECT
public:
    explicit CutExtrudeDialog(QWidget* parent = nullptr);
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void setInitialProfile(const QString& uuid);
    void setInitialTarget(const QString& uuid);

private slots:
    void onApplyClicked();
    void onCancelClicked();

private:
    void refreshSources();
    void updateStatus(const QString& text, bool ok);
    void syncHighlight();
    void updatePreview();
    void clearPreview(bool renderNow = true);
    QComboBox* m_targetCombo = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QComboBox* m_directionCombo = nullptr;
    QDoubleSpinBox* m_depthSpin = nullptr;
    QCheckBox* m_reverseCheck = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_applyButton = nullptr;
    QString m_targetUuid;
    QString m_profileUuid;
    QJsonObject m_committedState;
};

class TransformDialog : public BasePage
{
    Q_OBJECT
public:
    explicit TransformDialog(QWidget* parent = nullptr);
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void setInitialSource(const QString& uuid);

private slots:
    void onApplyClicked();
    void onCancelClicked();

private:
    void refreshSources();
    void updateStatus(const QString& text, bool ok);
    void updatePreview();
    void clearPreview(bool renderNow = true);
    QComboBox* m_sourceCombo = nullptr;
    QDoubleSpinBox* m_dxSpin = nullptr;
    QDoubleSpinBox* m_dySpin = nullptr;
    QDoubleSpinBox* m_dzSpin = nullptr;
    QDoubleSpinBox* m_rxSpin = nullptr;
    QDoubleSpinBox* m_rySpin = nullptr;
    QDoubleSpinBox* m_rzSpin = nullptr;
    QComboBox* m_pivotModeCombo = nullptr;
    QDoubleSpinBox* m_pivotXSpin = nullptr;
    QDoubleSpinBox* m_pivotYSpin = nullptr;
    QDoubleSpinBox* m_pivotZSpin = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_applyButton = nullptr;
    QCheckBox* m_previewCheck = nullptr;
    QString m_sourceUuid;
    QJsonObject m_committedState;
    int m_orderVersion = 2;
    bool m_loadingState = false;
};

class MirrorDialog : public BasePage
{
    Q_OBJECT
public:
    explicit MirrorDialog(QWidget* parent = nullptr);
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void setInitialSource(const QString& uuid);

private slots:
    void onApplyClicked();
    void onCancelClicked();

private:
    void refreshSources();
    void updateStatus(const QString& text, bool ok);
    void updatePreview();
    void clearPreview(bool renderNow = true);
    QComboBox* m_sourceCombo = nullptr;
    QComboBox* m_planeCombo = nullptr;
    QDoubleSpinBox* m_offsetSpin = nullptr;
    QPushButton* m_pickPlaneFaceButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_applyButton = nullptr;
    QCheckBox* m_previewCheck = nullptr;
    QString m_sourceUuid;
    QJsonObject m_committedState;
};

class FilletDialog : public BasePage
{
    Q_OBJECT
public:
    explicit FilletDialog(QWidget* parent = nullptr);
    ~FilletDialog() override;
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void setInitialSource(const QString& uuid);

private slots:
    void onApplyClicked();
    void onCancelClicked();

private:
    void refreshSources();
    void updateStatus(const QString& text, bool ok);
    QString selectedEdges() const;
    void reloadStoredEdgeSelection();
    void removeSelectedEdges();
    void updateSelectedEdgesUi();
    void syncEdgeHighlight();
    void updatePreview();
    void clearPreview(bool renderNow = true);
    void setViewportPickingEnabled(bool enabled);
    void stopViewportPicking();
    void togglePickedEdge(int edgeIndex);

    QComboBox* m_sourceCombo = nullptr;
    QDoubleSpinBox* m_radiusSpin = nullptr;
    QListWidget* m_selectedEdgesList = nullptr;
    QPushButton* m_pickEdgeButton = nullptr;
    QPushButton* m_removeEdgeButton = nullptr;
    QLabel* m_selectedEdgesLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_applyButton = nullptr;

    QString m_sourceUuid;
    QString m_selectionSourceUuid;
    QString m_edgeSelection;
    bool m_edgeSelectionDirty = false;
    QSet<int> m_selectedEdgeIndices;
    QJsonObject m_committedState;

};

class ChamferDialog : public BasePage
{
    Q_OBJECT
public:
    explicit ChamferDialog(QWidget* parent = nullptr);
    ~ChamferDialog() override;
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void setInitialSource(const QString& uuid);

private slots:
    void onApplyClicked();
    void onCancelClicked();

private:
    void refreshSources();
    void updateStatus(const QString& text, bool ok);
    QString selectedEdges() const;
    void reloadStoredEdgeSelection();
    void removeSelectedEdges();
    void updateSelectedEdgesUi();
    void syncEdgeHighlight();
    void updatePreview();
    void clearPreview(bool renderNow = true);
    void setViewportPickingEnabled(bool enabled);
    void stopViewportPicking();
    void togglePickedEdge(int edgeIndex);

    QComboBox* m_sourceCombo = nullptr;
    QDoubleSpinBox* m_distanceSpin = nullptr;
    QListWidget* m_selectedEdgesList = nullptr;
    QPushButton* m_pickEdgeButton = nullptr;
    QPushButton* m_removeEdgeButton = nullptr;
    QLabel* m_selectedEdgesLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_applyButton = nullptr;

    QString m_sourceUuid;
    QString m_selectionSourceUuid;
    QString m_edgeSelection;
    bool m_edgeSelectionDirty = false;
    QSet<int> m_selectedEdgeIndices;
    QJsonObject m_committedState;

};

class SplitDialog : public BasePage
{
    Q_OBJECT
public:
    explicit SplitDialog(QWidget* parent = nullptr);
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void setInitialSource(const QString& uuid);
    void setInitialTool(const QString& uuid);
    void setPartSuppressed(int partIndex, bool suppressed);
    QSet<int> suppressedPartIndices() const;

private slots:
    void onApplyClicked();
    void onCancelClicked();

private:
    void refreshSources();
    void updateStatus(const QString& text, bool ok);
    void updatePreview();
    void clearPreview(bool renderNow = true);
    QComboBox* m_sourceCombo = nullptr;
    QComboBox* m_toolCombo = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_applyButton = nullptr;
    QString m_sourceUuid;
    QString m_toolUuid;
    QSet<int> m_suppressedPartIndices;
    QJsonObject m_committedState;
};


class SplitPartDialog : public BasePage
{
    Q_OBJECT
public:
    explicit SplitPartDialog(QWidget* parent = nullptr);
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void setSplitPart(const QString& splitUuid, int partIndex);

private:
    void refreshText();
    QLabel* m_infoLabel = nullptr;
    QString m_splitUuid;
    int m_partIndex = -1;
};
