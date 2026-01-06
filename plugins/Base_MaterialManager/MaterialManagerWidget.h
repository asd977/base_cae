#ifndef MATERIALMANAGERWIDGET_H
#define MATERIALMANAGERWIDGET_H

#include <QWidget>

#include<QStringList>

#include "MaterialLibraryData.h"

class MaterialManager;
class QTreeWidgetItem;
namespace Ui {
class MaterialManagerWidget;
}

class MaterialManagerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MaterialManagerWidget(MaterialManager *manager,QWidget *parent = nullptr);
    ~MaterialManagerWidget();
    void refreshSummary(const QVector<MaterialModel> &models);


private slots:

    void on_pushButton_2_clicked();

    void on_pushButton_3_clicked();
    void on_treeWidget_itemDoubleClicked(QTreeWidgetItem *item, int column);
    void on_addToTreeButton_clicked();
    void on_removeFromTreeButton_clicked();
    void on_generateImatButton_clicked();

private:
    Ui::MaterialManagerWidget *ui;
    MaterialManager *m_materialManager;
};

#endif // MATERIALMANAGERWIDGET_H
