#include "MaterialModelWidget.h"

#include <QHBoxLayout>

#include "MaterialManager.h"

MaterialModelWidget::MaterialModelWidget(MaterialManager* manager, QWidget* parent)
    : BasePage("MaterialModelWidget"), m_materialManager(manager), m_titleLabel(new QLabel(this)),
      m_summaryLabel(new QLabel(this))
{
    auto* openButton = new QPushButton(QString("新增材料"), this);
    connect(openButton, &QPushButton::clicked, this, &MaterialModelWidget::onOpenLibrary);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_summaryLabel);
    layout->addStretch();
    layout->addWidget(openButton);
    layout->setContentsMargins(8, 8, 8, 8);
}

void MaterialModelWidget::setModel(const MaterialModel& model)
{
    m_modelKey = model.key;
    m_titleLabel->setText(model.displayName);
    m_summaryLabel->setText(QString("材料数：%1").arg(model.materials.size()));
}

void MaterialModelWidget::beforeLoad()
{
    if (!m_materialManager)
    {
        return;
    }
}

void MaterialModelWidget::beforeUnload()
{
    if (!m_materialManager)
    {
        return;
    }
}

void MaterialModelWidget::onOpenLibrary()
{
    if (m_modelKey.isEmpty())
    {
        return;
    }
    const QString entryId = m_materialManager->addMaterialToModel(m_modelKey, true);
    if (!entryId.isEmpty())
    {
        m_materialManager->focusOnMaterial(entryId);
    }
}
