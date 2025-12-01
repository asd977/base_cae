#include "ThinFilmSettingWidget.h"
#include "ui_ThinFilmSettingWidget.h"

ThinFilmSettingWidget::ThinFilmSettingWidget(QWidget *parent) :
    ui(new Ui::ThinFilmSettingWidget),
    BasePage("ThinFilmSettingWidget")
{
    ui->setupUi(this);
}

ThinFilmSettingWidget::~ThinFilmSettingWidget()
{
    delete ui;
}

void ThinFilmSettingWidget::beforeLoad()
{

}

void ThinFilmSettingWidget::beforeUnload()
{

}
