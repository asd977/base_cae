#ifndef SLOPESTANDARDMETHODDIALOG_H
#define SLOPESTANDARDMETHODDIALOG_H

#include <BasePage.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <memory>
#include <vtkSmartPointer.h>

#include "SlopeStandardDatModel.h"

class QButtonGroup;
class QCheckBox;
class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QTreeWidget;
class vtkActor;
class vtkCallbackCommand;
class vtkPolyData;
class vtkProp;

class QTreeWidget;
class QTreeWidgetItem;

// NOTE:
// The file name is kept for build-system compatibility with the existing plugin,
// but this is no longer a dialog. It is an independent project-tree analysis page.
class SlopeStandardAnalysisWidget final : public BasePage
{
    Q_OBJECT

  public:
    enum class PageRole
    {
        CombinedLegacy,
        SectionEditor,
        AnalysisSettings
    };

    using SharedState = std::shared_ptr<QJsonObject>;

    explicit SlopeStandardAnalysisWidget(const QString& analysisUuid,
                                         const QJsonArray& savedCases,
                                         const QString& currentCaseId,
                                         const QString& legacyDatText,
                                         const QString& legacyLastJobDirectory,
                                         QWidget* parent = nullptr,
                                         bool treeCaseMode = false,
                                         PageRole pageRole = PageRole::CombinedLegacy,
                                         SharedState sharedState = {});
    ~SlopeStandardAnalysisWidget() override;

    static QString defaultDatText();
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& object) override;
    QString datText() const;
    QJsonArray casesJson() const;
    QString currentCaseId() const;
    QString lastJobDirectory() const;
    bool isRunning() const;
    SharedState sharedState() const { return m_sharedState; }
    PageRole pageRole() const { return m_pageRole; }

  signals:
    void casesChanged();
    void currentCaseChanged(const QString& caseId);
    void runningChanged(bool running);
    void resultReady(const QString& jobDirectory, double requiredFactor,
                     bool horizontalReversed, double horizontalMinimum,
                     double horizontalMaximum);
    void openSectionRequested();
    void openAnalysisRequested();
    // Integration hook for Base_Geometry. Base_Solver deliberately does not
    // depend on a concrete geometry plugin API.
    void geometrySectionRequested(const QString& caseId);

  private:
    enum class Operation
    {
        None,
        Validate,
        Calculate
    };

    struct CaseRecord
    {
        QString id;
        QString name;
        QString dat;
        QString lastJobDirectory;
        QString sourceType;
        QString sourceSummary;
        QJsonObject state;
        double requiredFactor{1.30};
        QStringList materialNames;
        QVector<int> sourceMaterialIds;
    };

    void buildUi();
    void buildSectionPage();
    void buildCasePage();
    void buildResultPage();
    void setCurrentStep(int step);
    void updateStepButtons();
    void applyPageRole();
    void updateSectionStatusSummary();
    void rebuildMaterialRegionEditor();
    void loadMaterialRegionIntoEditor(int row);
    void setMaterialTableValue(int row, int column, double value);
    void syncFromSharedState();
    void syncToSharedState();

    void loadCases(const QJsonArray& savedCases,
                   const QString& currentCaseId,
                   const QString& legacyDatText,
                   const QString& legacyLastJobDirectory);
    void rebuildCaseCombo();
    void saveCurrentCase();
    void loadCase(int index);
    void createCase(bool copyCurrent);
    void removeCurrentCase();
    static QString newCaseId();
    static QJsonObject caseToJson(const CaseRecord& record);
    static CaseRecord caseFromJson(const QJsonObject& object);

    void resetEmptyForm();
    QJsonObject serializeCurrentState() const;
    void applyCurrentState(const QJsonObject& state);
    bool loadFormFromDat(const QString& text, QString* errorMessage = nullptr);
    bool collectFormModel(SlopeStandardDatModel* model,
                          QString* errorMessage = nullptr) const;
    SlopeStandardDatModel collectPreviewModel() const;
    bool syncDatFromForm(QString* errorMessage = nullptr);
    void updateControlAvailability();
    void updateInputPreview();
    void updateVisualization();
    void updateResultVisualization(const QJsonObject& plot);
    void clearVisualization(bool renderNow = true);
    void setOriginalModelVisible(bool visible);
    void restoreOriginalModelVisibility();
    void setSectionAntialiasing(bool enabled);
    void saveSectionOnlyCameraState();
    void applySectionOnlyCamera();
    void restoreSectionOnlyCameraState();
    void updateMaterialRegionHighlight(int row);
    bool hasAvailableGeometrySection() const;
    void markCaseDirty();
    void updateRequiredFactorConclusion(double factor = -1.0);
    void focusErrorField(const QString& error);

    void addTableRow(QTableWidget* table);
    void removeSelectedRows(QTableWidget* table);
    void renumberIdentityColumn(QTableWidget* table);
    void setSelectedSurfaceSegments(const QVector<int>& segmentIds);

    void initializeMeshSectionControls();
    void rebuildPlaneChoices();
    void updatePlaneRange(bool resetPosition = true);
    void scheduleMeshSectionPreview();
    bool extractSectionFromCurrentMesh(bool silent);
    bool extractSectionFromCurrent2DMesh();
    void requestGeometrySection();
    void importDat();
    void exportDat();
    void loadExampleForAdvancedUse();

    void startValidateAndCalculate();
    void startOperation(Operation operation, bool reuseCurrentJob = false);
    void setRunning(bool running);
    void consumeProcessOutput();
    void handleProcessError(QProcess::ProcessError error);
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    bool writeInputDat(const QString& path, QString* errorMessage) const;
    QJsonObject readJsonObject(const QString& path, QString* errorMessage = nullptr) const;
    void showStatus(const QJsonObject& status, const QString& jobDirectory,
                    bool calculation);
    void loadExistingResult(const QString& jobDirectory);
    QString createJobDirectory(QString* errorMessage) const;
    QString engineExecutable() const;
    QString errorDescription(int exitCode, const QJsonObject& status) const;
    QString userFacingEngineError(const QJsonObject& status, int exitCode) const;
    void stopProcess();
    void openOutputDirectory();

    enum class SectionInteractionMode
    {
        None,
        SurfaceEdit,
        PhreaticDraw
    };
    void setSectionInteractionMode(SectionInteractionMode mode);
    void installSectionInteractor();
    void uninstallSectionInteractor();
    void handleSectionViewClick(int displayX, int displayY);
    void toggleSurfaceSegment(int segmentId);
    void appendPhreaticPoint(double x, double y);
    void clearPhreaticLine();
    bool extractSectionFromCurrentGeometry(bool silent = false);
    QVector<vtkSmartPointer<vtkPolyData>> currentGeometrySectionRegions(QString* errorMessage = nullptr);

    QString m_analysisUuid;
    QVector<CaseRecord> m_cases;
    int m_currentCaseIndex{-1};
    bool m_loadingCase{false};
    bool m_caseDirty{false};
    bool m_treeCaseMode{false};
    PageRole m_pageRole{PageRole::CombinedLegacy};
    SharedState m_sharedState;
    bool m_syncingSharedState{false};

    QString m_jobDirectory;
    QString m_statusPath;
    QString m_plotPath;
    QString m_resultPath;
    QString m_processOutput;
    Operation m_operation{Operation::None};
    bool m_stopRequested{false};
    bool m_autoCalculateAfterValidate{false};
    QProcess* m_process{nullptr};

    QComboBox* m_caseCombo{nullptr};
    QPushButton* m_newCaseButton{nullptr};
    QPushButton* m_copyCaseButton{nullptr};
    QPushButton* m_deleteCaseButton{nullptr};
    QVector<QPushButton*> m_stepButtons;
    QStackedWidget* m_pages{nullptr};

    // Step 1: section source and preview.
    QWidget* m_meshSectionControls{nullptr};
    QButtonGroup* m_sourceButtonGroup{nullptr};
    QPushButton* m_geometrySourceButton{nullptr};
    QPushButton* m_mesh3dSourceButton{nullptr};
    QPushButton* m_mesh2dSourceButton{nullptr};
    QPushButton* m_importSourceButton{nullptr};
    QPushButton* m_sectionNextButton{nullptr};
    QLabel* m_sourceStatusLabel{nullptr};
    QComboBox* m_verticalCombo{nullptr};
    QComboBox* m_planeCombo{nullptr};
    QSlider* m_sectionPositionSlider{nullptr};
    QDoubleSpinBox* m_sectionPositionSpin{nullptr};
    QComboBox* m_materialFieldCombo{nullptr};
    QComboBox* m_surfaceCandidateCombo{nullptr};
    QDoubleSpinBox* m_simplificationToleranceSpin{nullptr};
    QPushButton* m_editSurfaceButton{nullptr};
    QToolButton* m_advancedGeometryToggle{nullptr};
    QWidget* m_advancedGeometryPanel{nullptr};
    QCheckBox* m_showOriginalModelCheck{nullptr};
    QCheckBox* m_showSectionFillCheck{nullptr};
    QCheckBox* m_showSurfaceCheck{nullptr};
    QHash<QString, bool> m_savedPropVisibilities;
    QVector<vtkProp*> m_savedRendererProps;
    bool m_originalModelHiddenByPage{false};
    bool m_sectionOnlyCameraSaved{false};
    double m_savedCameraPosition[3]{0.0, 0.0, 1.0};
    double m_savedCameraFocalPoint[3]{0.0, 0.0, 0.0};
    double m_savedCameraViewUp[3]{0.0, 1.0, 0.0};
    double m_savedCameraParallelScale{1.0};
    double m_savedCameraViewAngle{30.0};
    int m_savedCameraParallelProjection{0};
    bool m_sectionHorizontalReversed{false};
    double m_sectionHorizontalMinimum{0.0};
    double m_sectionHorizontalMaximum{0.0};
    int m_sectionHorizontalAxis{0};
    int m_sectionVerticalAxis{1};
    int m_sectionNormalAxis{2};
    double m_sectionWorldOffset{0.0};

    // Step 2: engineering case input.
    QLineEdit* m_titleEdit{nullptr};
    QComboBox* m_methodCombo{nullptr};
    QComboBox* m_modeCombo{nullptr};
    QComboBox* m_searchPrecisionCombo{nullptr};
    QComboBox* m_computePrecisionCombo{nullptr};
    QDoubleSpinBox* m_requiredFactorSpin{nullptr};
    QDoubleSpinBox* m_searchStepSpin{nullptr};
    QSpinBox* m_sliceCountSpin{nullptr};
    QDoubleSpinBox* m_radiusSpin{nullptr};
    QDoubleSpinBox* m_circleXSpin{nullptr};
    QDoubleSpinBox* m_circleYSpin{nullptr};
    QDoubleSpinBox* m_waterWeightSpin{nullptr};
    QCheckBox* m_outsideWaterCheck{nullptr};
    QDoubleSpinBox* m_outsideWaterSpin{nullptr};
    QComboBox* m_poreMethodCombo{nullptr};
    QPushButton* m_drawPhreaticButton{nullptr};
    QPushButton* m_clearPhreaticButton{nullptr};
    QCheckBox* m_earthquakeCheck{nullptr};
    QDoubleSpinBox* m_slopeHeightSpin{nullptr};
    QSpinBox* m_earthquakeIntensitySpin{nullptr};
    QComboBox* m_earthquakeModeCombo{nullptr};
    QComboBox* m_earthquakeDirectionHCombo{nullptr};
    QComboBox* m_earthquakeDirectionVCombo{nullptr};
    // Hidden compatibility storage used for DAT/state serialization. Normal users edit one
    // material region at a time through the controls below rather than a raw table.
    QTableWidget* m_soilTable{nullptr};
    QComboBox* m_materialRegionCombo{nullptr};
    QDoubleSpinBox* m_frictionAngleSpin{nullptr};
    QDoubleSpinBox* m_cohesionSpin{nullptr};
    QDoubleSpinBox* m_naturalUnitWeightSpin{nullptr};
    QDoubleSpinBox* m_saturatedUnitWeightSpin{nullptr};
    QDoubleSpinBox* m_ruSpin{nullptr};
    QLabel* m_ruEditorLabel{nullptr};
    bool m_updatingMaterialEditor{false};
    QToolButton* m_advancedSettingsToggle{nullptr};
    QWidget* m_advancedSettingsPanel{nullptr};
    QLabel* m_sectionSummaryLabel{nullptr};
    QPushButton* m_openSectionButton{nullptr};

    // Advanced geometry / protocol tables. Hidden in normal mode.
    QTableWidget* m_nodeTable{nullptr};
    QTableWidget* m_segmentTable{nullptr};
    QTableWidget* m_surfaceTable{nullptr};
    QTableWidget* m_phreaticTable{nullptr};
    QPlainTextEdit* m_datEditor{nullptr};

    // Step 3: result.
    QLabel* m_factorLabel{nullptr};
    QLabel* m_requiredLabel{nullptr};
    QLabel* m_conclusionLabel{nullptr};
    QLabel* m_summaryLabel{nullptr};
    QLabel* m_stateLabel{nullptr};
    QPlainTextEdit* m_resultViewer{nullptr};
    QToolButton* m_resultDetailToggle{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QPushButton* m_calculateButton{nullptr};
    QPushButton* m_stopButton{nullptr};
    QPushButton* m_openDirectoryButton{nullptr};
    QJsonObject m_lastPlot;
    bool m_pageActive{false};
    bool m_sectionAntialiasingActive{false};
    bool m_savedUseFXAA{false};
    SectionInteractionMode m_sectionInteractionMode{SectionInteractionMode::None};
    vtkSmartPointer<vtkActor> m_sectionPickActor;
    vtkSmartPointer<vtkActor> m_sectionFillPickActor;
    vtkSmartPointer<vtkCallbackCommand> m_interactorCallback;
    unsigned long m_interactorObserverTag{0};
    int m_pendingPhreaticNodeId{0};
    int m_pendingPhreaticSoilId{1};
};

#endif // SLOPESTANDARDMETHODDIALOG_H
