#ifndef OCCBOOLEANBASEWIDGET_H
#define OCCBOOLEANBASEWIDGET_H

#include <BasePage.h>
#include <QHash>
#include <QJsonObject>
#include <QString>

#include <GeomManagerInstance.h>
#include <GeomShapeObject.h>
#include <PublicConstants.h>

#include <TopoDS_Shape.hxx>

class vtkActor;
class QComboBox;

class OccBooleanTool
{
public:
    static TopoDS_Shape Fuse(const TopoDS_Shape& a, const TopoDS_Shape& b);
    static TopoDS_Shape Cut(const TopoDS_Shape& a, const TopoDS_Shape& b);
    static TopoDS_Shape Common(const TopoDS_Shape& a, const TopoDS_Shape& b);
};

namespace Ui {
class OccBooleanBaseWidget;
}

class OccBooleanBaseWidget : public BasePage
{
    Q_OBJECT
public:
    enum class BooleanOp
    {
        Cut,
        Fuse,
        Common
    };

    explicit OccBooleanBaseWidget(BooleanOp opType, QWidget *parent = nullptr);
    ~OccBooleanBaseWidget() override;

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
    struct ColorBackup
    {
        double r{0.8};
        double g{0.8};
        double b{0.8};
        double opacity{1.0};
    };

    Ui::OccBooleanBaseWidget* ui;

    GeomManager::GeomManagerInstance* m_geomMgr = nullptr;
    QHash<QString, GeomShapeObject*>  m_geomMap;       // key -> 几何对象
    QHash<QString, ColorBackup>       m_colorBackup;   // key -> 原始颜色备份
    QHash<QString, ColorBackup>       m_dimmedBackup;  // key -> 灰显前的颜色备份

    QString m_keyA;
    QString m_keyB;
    BooleanOp m_opType;

    void initConnections();
    void fillComboBoxes();
    void setHighlight(const QString& key, const QColor& color, bool on);
    void setDimmed(const QString& key, bool on);
    void requestRender();
    QString opDisplayName() const;
    QString makeResultName() const;
    void removeGeometryAndDimNode(const QString& key);
    void removeComboItem(QComboBox* combo, const QString& key);
};

#endif // OCCBOOLEANBASEWIDGET_H
