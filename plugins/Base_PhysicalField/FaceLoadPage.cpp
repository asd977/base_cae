#include "FaceLoadPage.h"
#include "ui_FaceLoadPage.h"

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>
#include <ServiceRegistry.h>

#include "PickManager.h"
#include "vtkPointData.h"

#include <services/MeshModuleControllerService.h>

FaceLoadPage::FaceLoadPage(QWidget* parent)
    : BasePage("FaceLoadPage")
    , ui(new Ui::FaceLoadPage)
{
    ui->setupUi(this);
}

FaceLoadPage::~FaceLoadPage()
{
    delete ui;
}

void FaceLoadPage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Face);
}

void FaceLoadPage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Face);
}

double FaceLoadPage::fxValue()  { return ui->lineEditX->text().toDouble(); }
double FaceLoadPage::fyValue()  { return ui->lineEditY->text().toDouble(); }
double FaceLoadPage::fzValue()  { return ui->lineEditZ->text().toDouble(); }
