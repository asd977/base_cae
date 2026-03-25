#pragma once

#include <BasePage.h>

class QCheckBox;
class QDoubleSpinBox;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

class MeshSweepMeshingWidget : public BasePage
{
    Q_OBJECT

  public:
    enum class WorkMode
    {
        Mapped2D,
        Sweep3D
    };

    explicit MeshSweepMeshingWidget(WorkMode mode, QWidget* parent = nullptr);
    ~MeshSweepMeshingWidget() override = default;

    void beforeLoad() override;
    void beforeUnload() override;

  private slots:
    void onGenerate();

  private:
    void appendLog(const QString& text);
    bool runMapped(const QString& stepPath, QString* errMsg);
    bool runSweep(const QString& stepPath, QString* errMsg);
    bool prepareModel(const QString& stepPath, QString* errMsg);

  private:
    WorkMode m_mode = WorkMode::Sweep3D;
    QSpinBox* m_curveDivisions = nullptr;
    QSpinBox* m_sweepLayers = nullptr;
    QDoubleSpinBox* m_curveBias = nullptr;
    QCheckBox* m_recombineFaces = nullptr;
    QCheckBox* m_recombineVolumes = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QPushButton* m_generateButton = nullptr;
};
