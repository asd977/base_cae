#ifndef SLOPESTANDARDMETHODDIALOG_H
#define SLOPESTANDARDMETHODDIALOG_H

#include <QDialog>
#include <QJsonObject>
#include <QProcess>

#include "SlopeStandardDatModel.h"

class QCheckBox;
class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QCloseEvent;
class SlopeStandardInputPreviewWidget;
class SlopeStandardPlotWidget;

class SlopeStandardMethodDialog final : public QDialog
{
    Q_OBJECT

  public:
    explicit SlopeStandardMethodDialog(const QString& analysisUuid,
                                       const QString& datText,
                                       const QString& lastJobDirectory,
                                       QWidget* parent = nullptr);

    static QString defaultDatText();
    QString datText() const;

  signals:
    void datTextChanged(const QString& text);
    void lastJobDirectoryChanged(const QString& directory);
    void runningChanged(bool running);

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    enum class Operation
    {
        None,
        Validate,
        Calculate
    };

    void buildUi();
    bool loadFormFromDat(const QString& text, QString* errorMessage = nullptr);
    bool collectFormModel(SlopeStandardDatModel* model,
                          QString* errorMessage = nullptr) const;
    bool syncDatFromForm(QString* errorMessage = nullptr);
    void updateControlAvailability();
    void updateInputPreview();
    void addTableRow(QTableWidget* table);
    void removeSelectedRows(QTableWidget* table);
    void renumberIdentityColumn(QTableWidget* table);
    void startOperation(Operation operation);
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
    void stopProcess();
    void importDat();
    void exportDat();
    void openOutputDirectory();
    void extractSectionFromCurrentMesh();

    QString m_analysisUuid;
    QString m_jobDirectory;
    QString m_statusPath;
    QString m_plotPath;
    QString m_resultPath;
    QString m_processOutput;
    Operation m_operation{Operation::None};
    bool m_stopRequested{false};
    QProcess* m_process{nullptr};

    QTabWidget* m_tabs{nullptr};
    QPlainTextEdit* m_datEditor{nullptr};
    QLineEdit* m_titleEdit{nullptr};
    QComboBox* m_methodCombo{nullptr};
    QComboBox* m_modeCombo{nullptr};
    QDoubleSpinBox* m_searchStepSpin{nullptr};
    QSpinBox* m_sliceCountSpin{nullptr};
    QDoubleSpinBox* m_radiusSpin{nullptr};
    QDoubleSpinBox* m_circleXSpin{nullptr};
    QDoubleSpinBox* m_circleYSpin{nullptr};
    QDoubleSpinBox* m_waterWeightSpin{nullptr};
    QCheckBox* m_outsideWaterCheck{nullptr};
    QDoubleSpinBox* m_outsideWaterSpin{nullptr};
    QComboBox* m_poreMethodCombo{nullptr};
    QCheckBox* m_earthquakeCheck{nullptr};
    QDoubleSpinBox* m_slopeHeightSpin{nullptr};
    QSpinBox* m_earthquakeIntensitySpin{nullptr};
    QComboBox* m_earthquakeModeCombo{nullptr};
    QComboBox* m_earthquakeDirectionHCombo{nullptr};
    QComboBox* m_earthquakeDirectionVCombo{nullptr};
    QTableWidget* m_soilTable{nullptr};
    QTableWidget* m_nodeTable{nullptr};
    QTableWidget* m_segmentTable{nullptr};
    QTableWidget* m_surfaceTable{nullptr};
    QTableWidget* m_phreaticTable{nullptr};
    SlopeStandardInputPreviewWidget* m_inputPreview{nullptr};
    QLabel* m_summaryLabel{nullptr};
    QLabel* m_stateLabel{nullptr};
    QPlainTextEdit* m_resultViewer{nullptr};
    SlopeStandardPlotWidget* m_plotWidget{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QPushButton* m_validateButton{nullptr};
    QPushButton* m_calculateButton{nullptr};
    QPushButton* m_stopButton{nullptr};
    QPushButton* m_openDirectoryButton{nullptr};
};

#endif // SLOPESTANDARDMETHODDIALOG_H
