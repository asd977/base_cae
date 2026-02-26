#include "SeepageBoundaryPage.h"
#include "ui_SeepageBoundaryPage.h"

#include <PickManager.h>

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTextStream>

#include <algorithm>

#include <vtkPointData.h>
#include <vtkIntArray.h>

SeepageBoundaryPage::SeepageBoundaryPage(const QString& pageType,
                                         const QString& boundaryTitle,
                                         const QString& exportFileName,
                                         QWidget* parent)
    : BasePage(pageType),
      ui(new Ui::SeepageBoundaryPage),
      m_boundaryTitle(boundaryTitle),
      m_exportFileName(exportFileName)
{
    ui->setupUi(this);
    ui->labelBoundaryTitle->setText(boundaryTitle);
    ui->labelFileNameValue->setText(exportFileName);

    connect(ui->btnClear, &QPushButton::clicked, this, &SeepageBoundaryPage::onClearClicked);
    ui->tableNodes->setVisible(false);
    ui->labelCountValue->setText(tr("暂不显示"));
}

SeepageBoundaryPage::~SeepageBoundaryPage()
{
    delete ui;
}

void SeepageBoundaryPage::beforeLoad()
{
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
}

void SeepageBoundaryPage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

QVector<int> SeepageBoundaryPage::selectedNodeIds()
{
    PickManager& pm = PickManager::instance();
    pm.updateStructure();

    auto toNodeIds = [&](GridKind kind) {
        QVector<int> out;
        const QVector<vtkIdType> picked = pm.getIdList(getPageType(), m_uuid, PickMode::Node, kind);
        if (picked.isEmpty()) return out;

        vtkUnstructuredGrid* g = (kind == GridKind::Solid) ? pm.solidGrid() : pm.lineGrid();
        vtkIntArray* nodeArr = nullptr;
        if (g)
        {
            nodeArr = vtkIntArray::SafeDownCast(g->GetPointData()->GetArray("node_id"));
        }

        out.reserve(picked.size());
        for (vtkIdType pid : picked)
        {
            if (nodeArr && pid >= 0 && pid < nodeArr->GetNumberOfValues())
            {
                out.push_back(nodeArr->GetValue(int(pid)));
            }
            else
            {
                out.push_back(int(pid + 1));
            }
        }
        return out;
    };

    QSet<int> merged;
    const QVector<int> solidNodes = toNodeIds(GridKind::Solid);
    const QVector<int> lineNodes = toNodeIds(GridKind::Line);
    for (int id : solidNodes) merged.insert(id);
    for (int id : lineNodes) merged.insert(id);

    QVector<int> result;
    result.reserve(merged.size());
    for (int id : merged) result.push_back(id);

    std::sort(result.begin(), result.end());
    return result;
}

bool SeepageBoundaryPage::exportDatFile()
{
    const QVector<int> ids = selectedNodeIds();

    QDir wd(QDir::currentPath());
    if (!wd.exists("data")) wd.mkpath("data");

    QFile file(wd.filePath("data/" + m_exportFileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QTextStream os(&file);
    os << ids.size() << "\n";
    for (int i = 0; i < ids.size(); ++i)
    {
        os << ids[i] << " " << (i + 1) << "\n";
    }

    file.close();
    return true;
}

void SeepageBoundaryPage::onClearClicked()
{
    PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Node);
}
