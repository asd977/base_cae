#include "PostContourReaderWidget.h"

#include <PublicConstants.h>

#include "ui_PostContourReaderWidget.h"

PostContourReaderWidget::PostContourReaderWidget(QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessContourReader), ui(new Ui::PostContourReaderWidget)
{
    ui->setupUi(this);
    if (parent)
        setParent(parent);
}

PostContourReaderWidget::~PostContourReaderWidget()
{
    delete ui;
}

void PostContourReaderWidget::beforeLoad()
{
}

void PostContourReaderWidget::beforeUnload()
{
}
