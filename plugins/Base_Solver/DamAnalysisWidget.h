#pragma once

#include <BasePage.h>
#include <QPointer>

#include <QJsonObject>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>

#include <struct/MeshMap.h>

class QCheckBox;
class QuietBoundarySelector;
class QPushButton;
class QLineEdit;
class QWidget;
class QComboBox;
class QGroupBox;
class QLabel;
class QSpinBox;
class QTableWidget;

namespace Ui
{
class DamAnalysisWidget;
}

class DamAnalysisWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit DamAnalysisWidget(QWidget* parent = nullptr);
    ~DamAnalysisWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportIfillDatFile(int mcase, const QString& cycleSectionText) const;
    bool validateInputs(QString* errorMessage) const;
    void setMCaseCount(int mcase);
    bool freezeThawEnabled() const;
    double creepTimeStep(bool* ok = nullptr) const;
    bool dynamicAnalysisEnabled() const;
    bool dynamicOnlyEnabled() const;
    void setModalAnalysisRequired(bool required);
    int modalExtractionOrder() const;
    void setModalExtractionOrder(int order);
    void setUpliftReductionRequired(bool required);
    QPair<int, int> constructionLayerRange() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();
    void stopRequested();
    void requirementsChanged();
    void constructionLayersChanged(int firstLayer, int lastLayer);


  private slots:
    void onComputeClicked();
    void setComputing(bool computing);


  private:
    void initValueEditors();
    QString quoteValue(const QString& value) const;
    QString defaultValueByLine(int lineNo) const;
    QString descriptionByLine(int lineNo) const;
    QString normalizeEnumValue(const QString& rawValue) const;
    QString lineValue(int lineNo) const;
    void setLineValue(int lineNo, const QString& value);
    bool isFixedFileLine(int lineNo) const;
    void updateAnalysisModeVisibility();
    void updateDynamicOptionVisibility();
    void updateFreezeThawOptionState();
    void updateUpliftOptionState();
    void setupConstructionLayerUi();
    void rebuildConstructionLayerPanel();
    void applyConstructionLayerChanges();
    void markConstructionLayerDirty();
    void updateConstructionLayerHighlights();
    void clearConstructionLayerHighlights(bool renderNow = true);
    QString constructionRegionDisplayText(const MeshTagKey& key) const;

    Ui::DamAnalysisWidget* ui;
    QMap<int, QWidget*> m_lineEditors;
    QComboBox* m_comboMethodIter = nullptr;
    QLineEdit* m_editIter = nullptr;
    QLineEdit* m_editX1 = nullptr;
    QLineEdit* m_editX2 = nullptr;
    QLineEdit* m_editX3 = nullptr;
    QLineEdit* m_editAlfa1 = nullptr;
    QLineEdit* m_editAlfa2 = nullptr;
    QLineEdit* m_editAlfa3 = nullptr;
    QComboBox* m_comboUpstreamAxis = nullptr;
    QComboBox* m_comboPgen = nullptr;
    QComboBox* m_comboIfRock = nullptr;
    QComboBox* m_comboIfIso = nullptr;
    QPointer<QCheckBox> m_quietBoundaryCheck = nullptr;
    QPointer<QuietBoundarySelector> m_quietBoundarySelection = nullptr;
    QPointer<QLabel> m_quietBoundaryStatus = nullptr;
    QGroupBox* m_groupFreezeThaw = nullptr;
    QLineEdit* m_editIceExpansion = nullptr;
    QGroupBox* m_groupConstructionLayers = nullptr;
    QLabel* m_labelConstructionLayerStatus = nullptr;
    QTableWidget* m_constructionRegionTable = nullptr;
    QSpinBox* m_fillLayerCount = nullptr;
    QVector<MeshTagKey> m_constructionRegionKeys;
    QVector<QVector<MeshIdType>> m_constructionRegionCells;
    bool m_constructionLayerUiActive = false;
    bool m_constructionLayerUiUpdating = false;
    bool m_constructionLayerUiDirty = false;
    bool m_existingConstructionLayersValid = false;
    qulonglong m_constructionPanelGridMTime = 0;
    QString m_constructionBaseHighlightSignature;
    bool m_upliftReductionRequired = false;
};
