#pragma once

#include <BasePage.h>
#include <PickManager.h>
#include <QVector3D>

class QDoubleSpinBox;

namespace Ui
{
class CaveBlockPlanePage;
}

class CaveBlockPlanePage : public BasePage
{
    Q_OBJECT
  public:
    explicit CaveBlockPlanePage(QWidget* parent = nullptr);
    ~CaveBlockPlanePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QString icutLine(int ii);
    QString paraLine(int ii);
    QVector3D p1Vector();
    QVector3D normalVector();
    double pickedFaceArea();
    int spatialDimension() const;
    bool validateInputs(QString* errorMessage) const;

    // 同一“块体稳定分析”下的滑面共享一个分组，用于组合预览候选滑块。
    void setBlockGroupId(const QString& groupId);
    static void setGroupSearchBounds(const QString& groupId, const double bounds[6]);
    static void refreshGroupPreview(const QString& groupId, bool renderNow = true);
    static void clearGroupPreview(const QString& groupId, bool renderNow = true);

  private:
    QString pickDomain() const;
    QString valueOrDefault(QDoubleSpinBox* edit, QString def);
    void syncPointsFromSelectedFace();
    PickMode currentPickMode();
    GridKind currentGridKind();

    QVector<vtkIdType> selectedFaceKeys();
    vtkIdType chooseSingleFaceKey(const QVector<vtkIdType>& keys) const;
    void normalizeAndSyncSelectedFace();
    bool isTwoDimensionalPlane() const;
    void refreshDimensionUi();
    QString coordinatePreviewKey() const;
    void updateCoordinatePreview();
    void clearCoordinatePreview(bool renderNow = true);
    bool lineDefinition2D(double& x1, double& y1, double& x2, double& y2) const;
    void flipTwoDimensionalBlockSide();

  private slots:
    void onPickSelectionChanged(const QString& domain,
                                const QString& uuid,
                                PickMode mode,
                                GridKind kind);
    void onNodePicked(const QString& domain,
                      const QString& activeUuid,
                      GridKind kind,
                      vtkIdType pid,
                      int nodeId,
                      double x,
                      double y,
                      double z,
                      const QString& label);

  private:
    Ui::CaveBlockPlanePage* ui = nullptr;
    vtkIdType m_selectedFaceKey = -1;
    bool m_enforcingSingleFace = false;
    bool m_pageActive = false;
    int m_referenceDimension = 0;
    QVector<vtkIdType> m_linePointIds;
    bool m_has2DLineDefinition = false;
    QString m_blockGroupId;
};
