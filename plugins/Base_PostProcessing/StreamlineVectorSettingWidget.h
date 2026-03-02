#ifndef STREAMLINEVECTORSETTINGWIDGET_H
#define STREAMLINEVECTORSETTINGWIDGET_H

#include <BasePage.h>
#include <services/VisualWidgetService.h>

#include <QWidget>
#include <vtkSmartPointer.h>

class vtkActor;
class vtkUnstructuredGrid;

namespace Ui {
class StreamlineVectorSettingWidget;
}

class StreamlineVectorSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit StreamlineVectorSettingWidget(VisualWidgetService* visualService,
                                           QWidget* parent = nullptr);
    ~StreamlineVectorSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void on_buildPushButton_clicked();

private:
    vtkSmartPointer<vtkUnstructuredGrid> readFixedGrid() const;
    bool preparePointVectors(vtkUnstructuredGrid* grid) const;
    void buildStreamline();

private:
    Ui::StreamlineVectorSettingWidget* ui{nullptr};
    VisualWidgetService* m_visualService{nullptr};

    vtkSmartPointer<vtkActor> m_streamlineActor;
};

#endif // STREAMLINEVECTORSETTINGWIDGET_H
