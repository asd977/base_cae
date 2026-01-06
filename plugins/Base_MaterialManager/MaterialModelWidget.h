#ifndef MATERIALMODELWIDGET_H
#define MATERIALMODELWIDGET_H

#include <BasePage.h>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "MaterialLibraryData.h"

class MaterialManager;

class MaterialModelWidget : public BasePage
{
    Q_OBJECT
public:
    explicit MaterialModelWidget(MaterialManager *manager, QWidget *parent = nullptr);

    void setModel(const MaterialModel &model);
    void beforeLoad() override {}
    void beforeUnload() override {}

private slots:
    void onOpenLibrary();

private:
    MaterialManager *m_materialManager;
    QString m_modelKey;
    QLabel *m_titleLabel;
    QLabel *m_summaryLabel;
};

#endif // MATERIALMODELWIDGET_H
