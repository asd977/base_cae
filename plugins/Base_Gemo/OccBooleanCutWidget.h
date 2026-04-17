#ifndef OCCBOOLEANCUTWIDGET_H
#define OCCBOOLEANCUTWIDGET_H

#include <BasePage.h>
#include <GeomManagerInstance.h>
#include <GeomShapeObject.h>
#include <PublicConstants.h>

#include <QHash>
#include <QJsonObject>
#include <QStringList>

namespace Ui
{
class OccBooleanCutWidget;
}

class OccBooleanCutWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit OccBooleanCutWidget(QWidget* parent = nullptr);
    ~OccBooleanCutWidget() override;

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
    Ui::OccBooleanCutWidget* ui = nullptr;
    GeomManager::GeomManagerInstance* m_geomMgr = nullptr;
    QHash<QString, GeomShapeObject*> m_geomMap;
    QStringList m_selectedKeys;
    QStringList m_operationKeys;
    QString m_savedResultBrep;
};

#endif // OCCBOOLEANCUTWIDGET_H
