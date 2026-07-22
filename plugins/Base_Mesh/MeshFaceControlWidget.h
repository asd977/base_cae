#ifndef MESHFACECONTROLWIDGET_H
#define MESHFACECONTROLWIDGET_H

#include <BasePage.h>
#include <services/MeshModuleControllerService.h>

#include <QMap>
#include <QStringList>
#include <QVector>

class vtkActor;

namespace Ui
{
class MeshFaceControlWidget;
}

class MeshFaceControlWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit MeshFaceControlWidget(QWidget* parent = nullptr);
    ~MeshFaceControlWidget() override;
    void beforeLoad() override;
    void beforeUnload() override;

  private slots:
    void onAddControl();
    void onDeleteControl();
    void onResetForm();
    void onSelectionChanged();
    void onParameterEdited();

    void onAppendSelectedEntity();

  private:
    struct FaceControlItem
    {
        QString faceIds;
        double maxSize = 1.0;
    };

    void setupUiState();
    bool validateInput(QString& err, int ignoreRow = -1) const;
    FaceControlItem collectFormData() const;
    void fillFormData(const FaceControlItem& item);
    void appendOrUpdateTableRow(int row, const FaceControlItem& item);
    void refreshButtons();

    void rebuildFaceCandidates();
    void clearHighlight();
    void updateEntityUiState();
    void syncRulesToService();
    void loadRulesFromService();

  private:
    Ui::MeshFaceControlWidget* ui;
    QVector<FaceControlItem> m_items;
    QStringList m_entityIds;
    QString m_highlightPropId;
};

#endif // MESHFACECONTROLWIDGET_H
