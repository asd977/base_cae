#ifndef OCCBOOLEANTOOLWIDGET_H
#define OCCBOOLEANTOOLWIDGET_H

#include <BasePage.h>
#include <QHash>
#include <QJsonObject>
#include <QString>

#include <GeomManagerInstance.h>
#include <GeomShapeObject.h>
#include <PublicConstants.h>

#include <TopoDS_Shape.hxx>

class OccBooleanTool
{
public:
    static TopoDS_Shape Fuse(const TopoDS_Shape& a, const TopoDS_Shape& b);
    static TopoDS_Shape Cut(const TopoDS_Shape& a, const TopoDS_Shape& b);
    static TopoDS_Shape Common(const TopoDS_Shape& a, const TopoDS_Shape& b);
};
class vtkActor;
class PreProcessingWidget;
namespace Ui {
class OccBooleanToolWidget;
}

class OccBooleanToolWidget : public BasePage
{
    Q_OBJECT
public:
    explicit OccBooleanToolWidget(PreProcessingWidget *preProcessingWidget,QWidget *parent = nullptr);
    ~OccBooleanToolWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

private slots:
    void on_comboBoxA_currentIndexChanged(int index);
    void on_comboBoxB_currentIndexChanged(int index);
    void on_pushButtonApply_clicked();

    void on_pushButton_clicked();

private:
    Ui::OccBooleanToolWidget* ui;

    struct ColorBackup
    {
        double r{0.8};
        double g{0.8};
        double b{0.8};
    };

    GeomManager::GeomManagerInstance* m_geomMgr = nullptr;
    QHash<QString, GeomShapeObject*>  m_geomMap;       // key -> 几何对象
    QHash<QString, ColorBackup>       m_colorBackup;   // key -> 原始颜色备份

    QString m_keyA;
    QString m_keyB;
    PreProcessingWidget *m_preProcessingWidget;
    QString test_tmp;
private:
    void initConnections();
    void fillComboBoxes();
    void setHighlight(const QString& key, const QColor& color, bool on);
    void requestRender();  // 根据你自己的渲染系统实现
};
#endif // OCCBOOLEANTOOLWIDGET_H
