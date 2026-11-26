#pragma once

#include <BasePage.h>

#include "SmeshMeshGenerator.h"
#include "core/MeshControlSelectionPresentation.h"

#include <QString>
#include <QWidget>

class QThread;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QLabel;
class QPushButton;

namespace Ui
{
class MeshGlobalMeshingWidget;
}

struct Params
{
    QString inputGeometryPath;
    int dimension = 3;
    int elemType = 3;
    double lenMin = 0.5;
    double lenMax = 5.0;
    double fineness = 0.5;
    int nativeFineness = 2;
    double growthRate = 0.3;
    double segmentsPerEdge = 1.0;
    double segmentsPerRadius = 2.0;
    int elementOrder = 1;
    bool optimize = true;
    bool surfaceCurvature = true;
    double tolerance = 1e-6;
    bool exportVtk = true;
    bool exportNas = true;

    QVector<SmeshLocalSizeControl> localSizeControls;
    int skippedLocalControlCount = 0;
    int clampedLocalControlCount = 0;
    SmeshWorkflowSettings workflow;
    bool allGeometry = true;
    bool autoPartition = true;
    QVector<SmeshEntityReference> geometryScope;
};

class MeshGlobalWorker : public QObject
{
    Q_OBJECT
  public:
    explicit MeshGlobalWorker(Params p, QObject* parent = nullptr);

  signals:
    void log(const QString& s);
    void finishedOK(const QString& vtk, const QString& nas);
    void finishedFail(const QString& err);

  public slots:
    void run();

  public:
    QString m_tempVtkPath;
    QString m_tempNasPath;

  private:
    Params m_p;
};

class MeshGlobalMeshingWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit MeshGlobalMeshingWidget(QWidget* parent = nullptr);
    ~MeshGlobalMeshingWidget();

    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject&) override;
    int meshingDimension() const;
    bool usesAllGeometry() const;
    QList<int> selectedGeometryIds() const;
    void generateSubmesh(const QString& uuid = {});

  signals:
    void meshGenerated(const QString& vtkPath, const QString& nasPath);
    void logMessage(const QString& msg);

  private slots:
    void onGenerate();
    void onExportAs();
    void onOpenTempDir();
    void onDimChanged(int idx);
    void onElemTypeChanged(int idx);
    void on_generateButton_clicked();

  private:
    bool event(QEvent* event) override;
    void setupGeometryScope();
    void refreshGeometryScope();
    void updateGeometryScope();
    void stopGeometryPick();
    void initCombos();
    void refreshAlgoCombo(int dim);
    void refreshElemCombo(int dim);
    Params collectParams() const;
    void updateFinenessControls();
    void updateAutomaticSize();

    QString tempDirPath() const;
    QString tempVtkPath() const;
    QString tempNasPath() const;

    void appendLog(const QString& s);
    void showMeshVtk(const QString& vtkPath);

  private:
    Ui::MeshGlobalMeshingWidget* ui = nullptr;
    QThread* m_thread = nullptr;
    QString m_computeSubmeshUuid;
    QCheckBox* m_automaticMin = nullptr;
    QCheckBox* m_automaticMax = nullptr;
    QComboBox* m_fineness = nullptr;
    QDoubleSpinBox* m_growthRate = nullptr;
    QDoubleSpinBox* m_segmentsPerEdge = nullptr;
    QDoubleSpinBox* m_segmentsPerRadius = nullptr;
    QComboBox* m_scopeMode = nullptr;
    QListWidget* m_geometryList = nullptr;
    QLabel* m_geometryStatus = nullptr;
    QPushButton* m_pickGeometry = nullptr;
    QCheckBox* m_autoPartition = nullptr;
    QList<int> m_geometryIds[4];
    bool m_refreshingGeometry = false;
    bool m_pickingGeometry = false;
    MeshControlSelectionPresentation m_selectionPresentation;
};
