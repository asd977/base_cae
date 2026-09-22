#pragma once
#include <BasePage.h>
#include <QObject>
#include <QWidget>
#include <BasePage.h>
#include "CadDocument.h"
#include "CadBody.h"
#include <PublicConstants.h>
#include <QHash>
#include <QJsonObject>
#include <QStringList>
#include <QPushButton>




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
    void onCancelClicked();
    void restoreSavedResult();

  private:
    void refreshGeometryMap();
    void refreshSourceCombo();
    void refreshSelectedList();
    void updatePreview();
    void clearPreview(bool renderNow = true);
    void requestRender();

  private:
    Ui::OccBooleanFuseWidget* ui = nullptr;
    GeomManager::GeomManagerInstance* m_geomMgr = nullptr;
    QHash<QString, GeomShapeObject*> m_geomMap;
    QStringList m_selectedKeys;
    QStringList m_operationKeys;
    QStringList m_committedKeys;
    QString m_savedResultBrep;
};




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
    void onSetTargetClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onApplyClicked();
    void onCancelClicked();
    void restoreSavedResult();

  private:
    void refreshGeometryMap();
    void refreshSourceCombo();
    void refreshSelectedList();
    void updatePreview();
    void clearPreview(bool renderNow = true);
    void requestRender();

  private:
    Ui::OccBooleanCutWidget* ui = nullptr;
    GeomManager::GeomManagerInstance* m_geomMgr = nullptr;
    QHash<QString, GeomShapeObject*> m_geomMap;
    QStringList m_selectedKeys;
    QStringList m_operationKeys;
    QStringList m_committedKeys;
    QString m_savedResultBrep;
};




namespace Ui
{
class OccBooleanCommonWidget;
}

class OccBooleanCommonWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit OccBooleanCommonWidget(QWidget* parent = nullptr);
    ~OccBooleanCommonWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  private slots:
    void onAddClicked();
    void onRemoveSelectedClicked();
    void onApplyClicked();
    void onCancelClicked();
    void restoreSavedResult();

  private:
    void refreshGeometryMap();
    void refreshSourceCombo();
    void refreshSelectedList();
    void updatePreview();
    void clearPreview(bool renderNow = true);
    void requestRender();

  private:
    Ui::OccBooleanCommonWidget* ui = nullptr;
    GeomManager::GeomManagerInstance* m_geomMgr = nullptr;
    QHash<QString, GeomShapeObject*> m_geomMap;
    QStringList m_selectedKeys;
    QStringList m_operationKeys;
    QStringList m_committedKeys;
    QString m_savedResultBrep;
};

