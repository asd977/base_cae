#include "TestP.h"
#include "ui_TestP.h"

TestPage::TestPage(QWidget *parent) :
    BasePage("test"),
    ui(new Ui::TestPage)
{
    ui->setupUi(this);
}

TestPage::~TestPage()
{
    delete ui;
}

void TestPage::beforeLoad()
{

}

void TestPage::beforeUnload()
{

}
