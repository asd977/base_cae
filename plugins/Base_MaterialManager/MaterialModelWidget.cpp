#include "MaterialModelWidget.h"

#include "MaterialManager.h"

#include <QHBoxLayout>

MaterialModelWidget::MaterialModelWidget(MaterialManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_materialManager(manager)
    , m_titleLabel(new QLabel(this))
    , m_summaryLabel(new QLabel(this))
{
    auto *openButton = new QPushButton(QStringLiteral("打开材料库"), this);
    connect(openButton, &QPushButton::clicked, this, &MaterialModelWidget::onOpenLibrary);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_summaryLabel);
    layout->addStretch();
    layout->addWidget(openButton);
    layout->setContentsMargins(8, 8, 8, 8);
}

void MaterialModelWidget::setModel(const MaterialModel &model)
{
    m_modelKey = model.key;
    m_titleLabel->setText(model.displayName);
    m_summaryLabel->setText(QStringLiteral("材料数：%1").arg(model.materials.size()));
}

void MaterialModelWidget::onOpenLibrary()
{
    if (m_modelKey.isEmpty()) {
        return;
    }
    m_materialManager->openLibrary(m_modelKey, false);
}
