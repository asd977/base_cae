#ifndef GLOBALPARAMETERSETTINGWIDGET_H
#define GLOBALPARAMETERSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>
#include <QVariantMap>

class QTableWidgetItem;

namespace Ui { class GlobalParameterSettingWidget; }

class GlobalParameterSettingWidget : public BasePage
{
    Q_OBJECT
public:
    explicit GlobalParameterSettingWidget(const QString& pageUuid, QWidget *parent = nullptr);
    ~GlobalParameterSettingWidget();

    void beforeLoad() override {}     // 按要求不处理
    void beforeUnload() override {}   // 按要求不处理

    QString pageUuid() const { return m_pageUuid; }
    void setPageUuid(const QString& uuid);

private slots:
    void onAddRow();
    void onRemoveSelectedRows();
    void onItemChanged(QTableWidgetItem *item);

private:
    void setupUiBehavior();
    void reloadFromPublicVariables();
    void addRow(const QString& key = QString(), const QVariant& value = QVariant());
    QVariant guessTyped(const QString& text) const;
    bool keyExistsInTable(const QString& key, int excludeRow = -1) const;

private:
    Ui::GlobalParameterSettingWidget *ui;
    QString m_pageUuid;
    bool m_blockItemChanged = false;
};

#endif // GLOBALPARAMETERSETTINGWIDGET_H
