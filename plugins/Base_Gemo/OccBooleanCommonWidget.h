#ifndef OCCBOOLEANCOMMONWIDGET_H
#define OCCBOOLEANCOMMONWIDGET_H

#include <BasePage.h>
#include <GeomManagerInstance.h>
#include <GeomShapeObject.h>
#include <PublicConstants.h>

#include <QHash>
#include <QJsonObject>

class QComboBox;
class QPushButton;
class QListView;
class CustomComboBox;

class OccBooleanCommonWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit OccBooleanCommonWidget(QWidget* parent = nullptr);
    ~OccBooleanCommonWidget() override = default;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  private slots:
    void onComboBoxAChanged(int index);
    void onComboBoxBChanged(int index);
    void onComboBoxHovered(int index);
    void onComboBoxDropdownClosed();
    void onApplyClicked();
    void onRefreshClicked();

  private:
    struct ColorBackup
    {
        double r{0.8};
        double g{0.8};
        double b{0.8};
        double opacity{1.0};
    };

    void initUi();
    void initConnections();
    void fillComboBoxes();
    void setHighlight(const QString& key, const QColor& color, bool on);
    void setDimmed(const QString& key, bool on);
    void requestRender();
    void removeComboItem(QComboBox* combo, const QString& key);
    void removeGeometryAndDimNode(const QString& key);

    CustomComboBox* m_comboBoxA = nullptr;
    CustomComboBox* m_comboBoxB = nullptr;
    QComboBox* m_comboBoxOp = nullptr;
    QPushButton* m_applyButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QListView* m_listView = nullptr;

    GeomManager::GeomManagerInstance* m_geomMgr = nullptr;
    QHash<QString, GeomShapeObject*> m_geomMap;
    QHash<QString, ColorBackup> m_colorBackup;
    QHash<QString, ColorBackup> m_dimmedBackup;

    QString m_keyA;
    QString m_keyB;
    QString m_hoveredKey;
};

#endif // OCCBOOLEANCOMMONWIDGET_H
