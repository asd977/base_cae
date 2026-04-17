#ifndef OCCBOOLEANFUSEWIDGET_H
#define OCCBOOLEANFUSEWIDGET_H

#include <BasePage.h>
#include <GeomManagerInstance.h>
#include <GeomShapeObject.h>
#include <PublicConstants.h>

#include <QHash>
#include <QJsonObject>
#include <QStringList>

namespace Ui
{
class OccBooleanFuseWidget;
}

class OccBooleanFuseWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit OccBooleanFuseWidget(QWidget* parent = nullptr);
    ~OccBooleanFuseWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  private slots:
    void onAddClicked();
    void onRemoveSelectedClicked();
    void onApplyClicked();
    void restoreSavedResult();

  private:
    void refreshGeometryMap();
    void refreshSourceCombo();
    void refreshSelectedList();
    void requestRender();
    void removeGeometryAndDimNode(const QString& key);

  private:
    Ui::OccBooleanFuseWidget* ui = nullptr;
    GeomManager::GeomManagerInstance* m_geomMgr = nullptr;
    QHash<QString, GeomShapeObject*> m_geomMap;
    QStringList m_selectedKeys;
    QStringList m_operationKeys;
    QString m_savedResultBrep;
};

#endif // OCCBOOLEANFUSEWIDGET_H
