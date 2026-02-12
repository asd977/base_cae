#ifndef MESHFACECONTROLWIDGET_H
#define MESHFACECONTROLWIDGET_H

#include <BasePage.h>
#include <QMap>
#include <QVector>
#include <QStringList>
#include <services/MeshModuleControllerService.h>

class vtkActor;

namespace Ui {
class MeshFaceControlWidget;
}

class MeshFaceControlWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshFaceControlWidget(QWidget *parent = nullptr);
    ~MeshFaceControlWidget() override;
    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onAddControl();
    void onUpdateControl();
    void onDeleteControl();
    void onResetForm();
    void onSelectionChanged();
    void onParameterEdited();

    void onRefreshEntities();
    void onAppendSelectedEntity();
    void onHighlightSelectedEntity();

private:
    struct FaceControlItem
    {
        QString faceIds;
        double minSize = 0.2;
        double maxSize = 1.0;
        bool curvatureRefine = true;
        int curvatureLevel = 2;
    };

    void setupUiState();
    bool validateInput(QString &err) const;
    FaceControlItem collectFormData() const;
    void fillFormData(const FaceControlItem &item);
    void appendOrUpdateTableRow(int row, const FaceControlItem &item);
    void refreshButtons();
    void refreshSummary();

    void rebuildFaceCandidates();
    void clearHighlight();
    void updateEntityUiState();
    void syncRulesToService();
    void loadRulesFromService();
    bool appendEntityIdToEdit(int id);

private:
    Ui::MeshFaceControlWidget *ui;
    QVector<FaceControlItem> m_items;
    QStringList m_entityIds;
    QString m_highlightPropId;
};

#endif // MESHFACECONTROLWIDGET_H
