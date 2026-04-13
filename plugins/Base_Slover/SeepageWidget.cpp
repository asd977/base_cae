#include "SeepageWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/MaterialManagerService.h>

#include <QDir>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSet>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>
#include <QVector>

#include <vtkCell.h>
#include <vtkIdList.h>
#include <vtkPoints.h>
#include <vtkSmartPointer.h>
#include <vtkStaticCellLocator.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

#include "ui_SeepageWidget.h"

SeepageWidget::SeepageWidget(QWidget* parent)
    : BasePage("SeepageAnalysis"), ui(new Ui::SeepageWidget)
{
    ui->setupUi(this);
    connect(ui->btnCompute, &QPushButton::clicked, this, &SeepageWidget::onComputeClicked);
    connect(ui->btnGlobalMappings, &QPushButton::clicked, this,
            &SeepageWidget::onEditGlobalMappingsClicked);
    connect(ui->btnHwDropCurves, &QPushButton::clicked, this,
            &SeepageWidget::onEditHwDropCurvesClicked);

    ui->comboComputeType->addItem("1 - 仅进行稳定渗流计算", 1);
    ui->comboComputeType->addItem("2 - 仅进行非稳定渗流计算", 2);
    ui->comboComputeType->addItem("3 - 按流程全部进行计算", 3);

    ui->comboItagFree->addItem("0 - 不计入", 0);
    ui->comboItagFree->addItem("1 - 计入", 1);

}

SeepageWidget::~SeepageWidget()
{
    delete ui;
}

void SeepageWidget::beforeLoad()
{
}

void SeepageWidget::beforeUnload()
{
}

double SeepageWidget::upstreamWaterLevel() const
{
    return ui->spinUpstreamWaterLevel->value();
}

double SeepageWidget::downstreamWaterLevel() const
{
    return ui->spinDownstreamWaterLevel->value();
}

double SeepageWidget::upstreamFlux() const
{
    return ui->spinUpstreamFlux->value();
}

double SeepageWidget::downstreamFlux() const
{
    return ui->spinDownstreamFlux->value();
}

bool SeepageWidget::exportIseepDatFile() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString projectDirAbs = QDir(projectDir).absolutePath();

    QDir wd(projectDirAbs);
    if (!wd.exists("PREP"))
        wd.mkpath("PREP");
    //    if (!wd.exists("SEEPCASE01")) wd.mkpath("SEEPCASE01");

    auto ensureTrailingSep = [](const QString& path)
    {
        QString p = QDir::toNativeSeparators(path);
        if (!p.endsWith(QDir::separator()))
            p += QDir::separator();
        return p;
    };

    const QString prepDirAbs = ensureTrailingSep(wd.filePath("PREP"));
    const QString seepCaseDirName = QStringLiteral("SEEPCASE01");
    const QString seepCaseDirAbs = ensureTrailingSep(wd.filePath(seepCaseDirName));

    QVector<std::pair<int, int>> globalMappings;
    bool usedServiceMappings = false;
    if (auto* materialService = ServiceRegistry::instance().getService<MaterialManagerService>())
    {
        const auto transitions = materialService->materialTransitions();
        if (!transitions.isEmpty())
        {
            usedServiceMappings = true;
            for (const auto& row : transitions)
                globalMappings.push_back({row.first, row.second});
        }
    }
    if (!usedServiceMappings)
    {
        for (const auto& mapping : globalMappings_)
            globalMappings.push_back({mapping.oldMaterial, mapping.newMaterial});
    }
    const int globalNchange = globalMappings.size();

    QFile file(wd.filePath("ISEEP.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QTextStream os(&file);
    os.setRealNumberNotation(QTextStream::FixedNotation);
    os.setRealNumberPrecision(6);

    os << "'" << prepDirAbs << "'\n";
    os << "'JD_UPSEEP.DAT'\n";
    os << "'JD_DWSEEP.DAT'\n";
    os << "'" << ui->spinMainPropertyNo->value() << "'\n";
    os << "'" << ui->spinControlledPropertyNo->value() << "'\n";
    os << "'" << (ui->checkNaturalFreeSurface->isChecked() ? 1 : 0) << "'\n";
    os << "'JDFREE.DAT'\n";
    os << "'FA_UPSEEP.DAT'\n";
    os << "'FA_DWSEEP.DAT'\n";
    os << "'" << (ui->checkDrainWell->isChecked() ? 1 : 0) << "'\n";
    os << "'JDJING.DAT'\n";
    os << "'JDTRACE.DAT'\n";
    os << "'KETRACE.DAT'\n";
    os << "'TDTRACE.DAT'\n";
    os << "'------------'\n";
    os << "'------------'\n";
    os << "'------------'\n";
    os << "'------------'\n";
    os << "'" << ui->comboComputeType->currentData().toInt() << "'\n";
    os << "'------------'\n";

    os << globalNchange << "     NCHANGE\n";
    for (int i = 0; i < globalNchange; ++i)
    {
        if (i < globalMappings.size())
        {
            os << globalMappings[i].first << " " << globalMappings[i].second
               << "   MOLD MNEW\n";
        }
        else
        {
            os << "0 0   MOLD MNEW\n";
        }
    }

    os << "1     MCASE\n";
    os << "1 1   JISUAN CASE  II\n";
    os << "1     NCASE\n";
    os << "'------------'\n";
    os << "1     ICASE\n";
    os << "0     NCHANGE_CASE\n";
    os << ui->spinIter->value() << " " << ui->comboItagFree->currentData().toInt()
       << " ITER  ITAG_FREE\n";
    os << upstreamWaterLevel() << " " << downstreamWaterLevel() << " " << upstreamFlux() << " "
       << downstreamFlux() << "    HWUP   HWDW\n";
    os << "'" << seepCaseDirAbs << "'     DIRDYNA\n";
    os << ui->spinDt->value() << " " << ui->spinTsta->value() << " " << ui->spinTend->value()
       << "    DT TSTA TEND\n";
    const QString hwDropUp = QStringLiteral("HWDROP_UP.DAT");
    const QString hwDropDw = QStringLiteral("HWDROP_DW.DAT");
    os << "'" << hwDropUp << "'  '" << hwDropDw << "'   HWDROP_UP  HWDROP_DW\n";

    file.close();

    if (!exportHwDropDatFiles(seepCaseDirName))
        return false;

    return exportInouseDatFile() && exportMaMaskDatFile()  &&
           exportKetraceDatFile() && exportJdtraceDatFile();
}

bool SeepageWidget::exportInouseDatFile() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("PREP/INOUSE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    const int configuredNdim =
        qMax(2, PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt());

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    auto grid = meshSvc ? meshSvc->getVtkUnstructuredGrid() : vtkSmartPointer<vtkUnstructuredGrid>{};
    if (!grid || grid->GetNumberOfPoints() <= 0)
    {
        os << "           0    NUM OF NO USE NODE\n";
        os << "           0    NUM OF NO USE ELEMENT\n";
        os << "       0    rod-num\n";
        os << QString("%1%2    NEBAR  NDIM\n").arg(0, 12).arg(configuredNdim, 12);
        return true;
    }

    const vtkIdType nPts = grid->GetNumberOfPoints();
    const vtkIdType nCells = grid->GetNumberOfCells();
    int meshMaxDim = 0;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell)
            continue;
        meshMaxDim = qMax(meshMaxDim, cell->GetCellDimension());
    }
    const int ndim = qMax(2, qMin(configuredNdim, meshMaxDim > 0 ? meshMaxDim : configuredNdim));

    QSet<vtkIdType> usedPointIds;
    usedPointIds.reserve(static_cast<int>(nPts));
    QVector<int> noUseElems;
    noUseElems.reserve(static_cast<int>(nCells / 10 + 1));

    QVector<QVector<int>> pointToSolidElems(static_cast<int>(nPts));
    QVector<int> solidCellIds;
    solidCellIds.reserve(static_cast<int>(nCells));

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell)
            continue;
        vtkIdList* ptIds = cell->GetPointIds();
        const vtkIdType np = ptIds ? ptIds->GetNumberOfIds() : 0;
        if (np <= 0)
        {
            noUseElems.push_back(static_cast<int>(cid + 1));
            continue;
        }

        const bool isSolid = (cell->GetCellDimension() >= ndim);
        if (isSolid)
            solidCellIds.push_back(static_cast<int>(cid + 1));
        for (vtkIdType i = 0; i < np; ++i)
        {
            const vtkIdType pid = ptIds->GetId(i);
            usedPointIds.insert(pid);
            if (isSolid && pid >= 0 && pid < nPts)
            {
                pointToSolidElems[static_cast<int>(pid)].push_back(static_cast<int>(cid + 1));
            }
        }
    }

    vtkSmartPointer<vtkUnstructuredGrid> solidGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    solidGrid->SetPoints(grid->GetPoints());
    QVector<int> solidLocalToGlobal;
    solidLocalToGlobal.reserve(solidCellIds.size());
    for (int ke : std::as_const(solidCellIds))
    {
        vtkCell* solid = grid->GetCell(static_cast<vtkIdType>(ke - 1));
        if (!solid || !solid->GetPointIds())
            continue;
        solidGrid->InsertNextCell(solid->GetCellType(), solid->GetPointIds());
        solidLocalToGlobal.push_back(ke);
    }
    auto solidLocator = vtkSmartPointer<vtkStaticCellLocator>::New();
    if (solidGrid->GetNumberOfCells() > 0)
    {
        solidLocator->SetDataSet(solidGrid);
        solidLocator->BuildLocator();
    }

    QVector<int> noUseNodes;
    noUseNodes.reserve(static_cast<int>(nPts / 10 + 1));
    for (vtkIdType pid = 0; pid < nPts; ++pid)
    {
        if (!usedPointIds.contains(pid))
            noUseNodes.push_back(static_cast<int>(pid + 1));
    }

    struct BarRow
    {
        int ke = 0;
        int ke1 = 0;
        int ke2 = 0;
        int lx = 4;
        QVector<double> rst1;
        QVector<double> rst2;
    };
    QVector<BarRow> barRows;
    QVector<int> suspendNodeIds;

    auto findContainingSolid = [&](const QVector<int>& candidates, const double xyz[3]) -> QVector<int>
    {
        QVector<int> inside;
        inside.reserve(candidates.size());
        for (int ke : candidates)
        {
            vtkCell* solid = grid->GetCell(static_cast<vtkIdType>(ke - 1));
            if (!solid)
                continue;
            std::array<double, 3> closestPt{0.0, 0.0, 0.0};
            std::array<double, 3> pcoords{0.0, 0.0, 0.0};
            int subId = 0;
            double dist2 = 0.0;
            const vtkIdType wn = (solid->GetNumberOfPoints() > 0) ? solid->GetNumberOfPoints() : 1;
            std::vector<double> weights(static_cast<size_t>(wn), 0.0);
            const int evalOk = solid->EvaluatePosition(const_cast<double*>(xyz), closestPt.data(), subId,
                                                       pcoords.data(), dist2, weights.data());
            constexpr double kInsideTol2 = 1.0e-14;
            if (evalOk == 1 || dist2 <= kInsideTol2)
                inside.push_back(ke);
        }
        return inside;
    };

    auto pickRelatedSolid = [&](vtkIdType pid, const double xyzSelf[3], const double xyzOther[3]) -> int
    {
        auto distanceToCell = [&](int ke, const double xyz[3]) -> double
        {
            vtkCell* solid = grid->GetCell(static_cast<vtkIdType>(ke - 1));
            if (!solid)
                return std::numeric_limits<double>::max();
            std::array<double, 3> closestPt{0.0, 0.0, 0.0};
            std::array<double, 3> pcoords{0.0, 0.0, 0.0};
            int subId = 0;
            double dist2 = std::numeric_limits<double>::max();
            const vtkIdType wn = (solid->GetNumberOfPoints() > 0) ? solid->GetNumberOfPoints() : 1;
            std::vector<double> weights(static_cast<size_t>(wn), 0.0);
            solid->EvaluatePosition(const_cast<double*>(xyz), closestPt.data(), subId, pcoords.data(), dist2,
                                    weights.data());
            return dist2;
        };

        QVector<int> candidates;
        QVector<int> related;
        if (pid >= 0 && pid < nPts)
            related = pointToSolidElems[static_cast<int>(pid)];
        candidates = related;
        if (candidates.isEmpty())
        {
            if (solidLocator && solidGrid->GetNumberOfCells() > 0)
            {
                const vtkIdType localCell = solidLocator->FindCell(const_cast<double*>(xyzSelf));
                if (localCell >= 0 && localCell < solidLocalToGlobal.size())
                    return solidLocalToGlobal[static_cast<int>(localCell)];
            }
            return 0;
        }
        if (candidates.isEmpty())
            return 0;

        QVector<int> selfInside = findContainingSolid(candidates, xyzSelf);
        if (selfInside.isEmpty() && !related.isEmpty())
            selfInside = related;
        if (selfInside.isEmpty())
            return 0;
        if (selfInside.size() == 1)
            return selfInside.front();

        double bestDist2 = std::numeric_limits<double>::max();
        int bestKe = selfInside.front();
        for (int ke : selfInside)
        {
            const double dist2 = distanceToCell(ke, xyzOther);
            if (dist2 < bestDist2)
            {
                bestDist2 = dist2;
                bestKe = ke;
            }
        }
        return bestKe;
    };

    auto evaluateRst = [&](int ke, const double xyz[3], QVector<double>& rst)
    {
        rst.fill(0.0, ndim);
        if (ke <= 0)
            return;
        vtkCell* solid = grid->GetCell(static_cast<vtkIdType>(ke - 1));
        if (!solid)
            return;

        std::array<double, 3> closestPt{0.0, 0.0, 0.0};
        std::array<double, 3> pcoords{0.0, 0.0, 0.0};
        int subId = 0;
        double dist2 = 0.0;
        const vtkIdType wn = (solid->GetNumberOfPoints() > 0) ? solid->GetNumberOfPoints() : 1;
        std::vector<double> weights(static_cast<size_t>(wn), 0.0);
        solid->EvaluatePosition(const_cast<double*>(xyz), closestPt.data(), subId, pcoords.data(), dist2,
                                weights.data());
        for (int d = 0; d < ndim && d < 3; ++d)
            rst[d] = 2.0 * pcoords[d] - 1.0;
    };

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell || cell->GetCellDimension() != 1)
            continue;
        vtkIdList* ptIds = cell->GetPointIds();
        if (!ptIds || ptIds->GetNumberOfIds() < 2)
            continue;

        const vtkIdType p0 = ptIds->GetId(0);
        const vtkIdType p1 = ptIds->GetId(1);
        if (p0 < 0 || p0 >= nPts || p1 < 0 || p1 >= nPts)
            continue;

        const auto& rel0 = pointToSolidElems[static_cast<int>(p0)];
        const auto& rel1 = pointToSolidElems[static_cast<int>(p1)];
        const bool s0 = rel0.isEmpty();
        const bool s1 = rel1.isEmpty();

        double xyz0[3] = {0.0, 0.0, 0.0};
        double xyz1[3] = {0.0, 0.0, 0.0};
        grid->GetPoint(p0, xyz0);
        grid->GetPoint(p1, xyz1);

        BarRow row;
        row.ke = static_cast<int>(cid + 1);
        row.ke1 = pickRelatedSolid(p0, xyz0, xyz1);
        row.ke2 = pickRelatedSolid(p1, xyz1, xyz0);
        if (!s0 && !s1)
            row.lx = 1;
        else if (s0 && !s1)
            row.lx = 2;
        else if (!s0 && s1)
            row.lx = 3;
        else
            row.lx = 4;
        evaluateRst(row.ke1, xyz0, row.rst1);
        evaluateRst(row.ke2, xyz1, row.rst2);
        barRows.push_back(row);

        if (s0)
            suspendNodeIds.push_back(static_cast<int>(p0 + 1));
        if (s1)
            suspendNodeIds.push_back(static_cast<int>(p1 + 1));
    }

    std::sort(noUseNodes.begin(), noUseNodes.end());
    std::sort(noUseElems.begin(), noUseElems.end());
    std::sort(suspendNodeIds.begin(), suspendNodeIds.end());
    suspendNodeIds.erase(std::unique(suspendNodeIds.begin(), suspendNodeIds.end()), suspendNodeIds.end());

    os << QString("%1    NUM OF NO USE NODE\n").arg(noUseNodes.size(), 12);
    for (int i = 0; i < noUseNodes.size(); ++i)
    {
        os << QString("%1%2\n").arg(noUseNodes.at(i), 9).arg(i + 1, 10);
    }

    os << QString("%1    NUM OF NO USE ELEMENT\n").arg(noUseElems.size(), 12);
    for (int i = 0; i < noUseElems.size(); ++i)
    {
        os << QString("%1%2\n").arg(noUseElems.at(i), 9).arg(i + 1, 10);
    }

    os << QString("%1    杆单元悬浮节点数\n").arg(suspendNodeIds.size(), 8);
    for (int i = 0; i < suspendNodeIds.size(); ++i)
    {
        os << QString("%1%2\n").arg(suspendNodeIds.at(i), 9).arg(i + 1, 10);
    }

    os << QString("%1%2    NEBAR  NDIM\n").arg(barRows.size(), 12).arg(ndim, 12);
    for (int i = 0; i < barRows.size(); ++i)
    {
        const auto& r = barRows.at(i);
        os << QString("%1%2%3%4").arg(r.ke, 7).arg(r.ke1, 8).arg(r.ke2, 8).arg(r.lx, 8);
        for (double v : r.rst1)
            os << QString("%1").arg(v, 16, 'E', 6);
        for (double v : r.rst2)
            os << QString("%1").arg(v, 16, 'E', 6);
        os << QString("%1\n").arg(i + 1, 8);
    }
    return true;
}

bool SeepageWidget::exportMaMaskDatFile() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("PREP/MA_MASK.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os << "   1   NUM\n";
    os << "  11     1\n";
    return true;
}

bool SeepageWidget::exportKetraceDatFile() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("PREP/KETRACE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os << "           0   NUM\n";
    return true;
}

bool SeepageWidget::exportJdtraceDatFile() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("PREP/JDTRACE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os << "           0   NUM\n";
    return true;
}

bool SeepageWidget::exportHwDropDatFiles(const QString& seepCaseDirName) const
{
    Q_UNUSED(seepCaseDirName);
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    auto writeCurve = [&](const QString& fileName, bool isUpstream)
    {
        QFile file(wd.filePath("PREP/" + fileName));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;

        QTextStream os(&file);
        os.setRealNumberNotation(QTextStream::FixedNotation);
        os.setRealNumberPrecision(6);
        os << waterLevelPoints_.size() << "\n";
        os << "'TDT(day)   HEAD(m)'\n";
        for (const auto& point : waterLevelPoints_)
            os << point.time << " " << (isUpstream ? point.upHead : point.dwHead) << "\n";
        return true;
    };

    return writeCurve(QStringLiteral("HWDROP_UP.DAT"), true) &&
           writeCurve(QStringLiteral("HWDROP_DW.DAT"), false);
}

void SeepageWidget::onEditGlobalMappingsClicked()
{
    auto* materialService = ServiceRegistry::instance().getService<MaterialManagerService>();
    if (!materialService)
        return;
    materialService->showMaterialTransitionDialog();
}

void SeepageWidget::onEditHwDropCurvesClicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle("水位变化过程");
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel("三列：时间、上游水位、下游水位。", &dlg));

    auto* table = new QTableWidget(&dlg);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels(QStringList() << "时间"
                                                   << "上游水位"
                                                   << "下游水位");
    table->horizontalHeader()->setStretchLastSection(true);
    table->setRowCount(qMax(1, waterLevelPoints_.size()));
    for (int i = 0; i < waterLevelPoints_.size(); ++i)
    {
        table->setItem(i, 0, new QTableWidgetItem(QString::number(waterLevelPoints_[i].time)));
        table->setItem(i, 1, new QTableWidgetItem(QString::number(waterLevelPoints_[i].upHead)));
        table->setItem(i, 2, new QTableWidgetItem(QString::number(waterLevelPoints_[i].dwHead)));
    }
    layout->addWidget(table);

    auto* buttonRow = new QHBoxLayout();
    auto* btnAdd = new QPushButton("添加行", &dlg);
    auto* btnRemove = new QPushButton("删除选中行", &dlg);
    buttonRow->addWidget(btnAdd);
    buttonRow->addWidget(btnRemove);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);
    connect(btnAdd, &QPushButton::clicked, &dlg, [table]() { table->insertRow(table->rowCount()); });
    connect(btnRemove, &QPushButton::clicked, &dlg, [table]()
            {
                const int row = table->currentRow();
                if (row >= 0)
                    table->removeRow(row);
            });

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return;

    auto parseTable = [](QTableWidget* srcTable)
    {
        QVector<WaterLevelPoint> points;
        for (int r = 0; r < srcTable->rowCount(); ++r)
        {
            const auto* tItem = srcTable->item(r, 0);
            const auto* upItem = srcTable->item(r, 1);
            const auto* dwItem = srcTable->item(r, 2);
            if (!tItem || !upItem || !dwItem)
                continue;
            bool tOk = false;
            bool upOk = false;
            bool dwOk = false;
            const double t = tItem->text().trimmed().toDouble(&tOk);
            const double up = upItem->text().trimmed().toDouble(&upOk);
            const double dw = dwItem->text().trimmed().toDouble(&dwOk);
            if (tOk && upOk && dwOk)
                points.push_back({t, up, dw});
        }
        return points;
    };
    waterLevelPoints_ = parseTable(table);
}

QJsonObject SeepageWidget::saveJson()
{
    QJsonObject obj;
    obj["upstream_water_level"] = ui->spinUpstreamWaterLevel->value();
    obj["downstream_water_level"] = ui->spinDownstreamWaterLevel->value();
    obj["upstream_flux"] = ui->spinUpstreamFlux->value();
    obj["downstream_flux"] = ui->spinDownstreamFlux->value();
    obj["main_property_no"] = ui->spinMainPropertyNo->value();
    obj["controlled_property_no"] = ui->spinControlledPropertyNo->value();
    obj["natural_free_surface"] = ui->checkNaturalFreeSurface->isChecked();
    obj["drain_well"] = ui->checkDrainWell->isChecked();
    obj["compute_type"] = ui->comboComputeType->currentData().toInt();
    QJsonArray globalMappingsArray;
    for (const auto& mapping : std::as_const(globalMappings_))
    {
        QJsonObject rowObj;
        rowObj["old"] = mapping.oldMaterial;
        rowObj["new"] = mapping.newMaterial;
        globalMappingsArray.append(rowObj);
    }
    obj["global_mappings_table"] = globalMappingsArray;
    obj["iter"] = ui->spinIter->value();
    obj["itag_free"] = ui->comboItagFree->currentData().toInt();
    obj["seep_case_dir"] = QStringLiteral("SEEPCASE01");
    obj["dt"] = ui->spinDt->value();
    obj["tsta"] = ui->spinTsta->value();
    obj["tend"] = ui->spinTend->value();
    QJsonArray waterLevelArray;
    for (const auto& point : std::as_const(waterLevelPoints_))
    {
        QJsonObject row;
        row["time"] = point.time;
        row["up_head"] = point.upHead;
        row["dw_head"] = point.dwHead;
        waterLevelArray.append(row);
    }
    obj["water_level_points"] = waterLevelArray;
    return obj;
}

void SeepageWidget::loadJson(const QJsonObject& obj)
{
    ui->spinUpstreamWaterLevel->setValue(
        obj.value("upstream_water_level").toDouble(ui->spinUpstreamWaterLevel->value()));
    ui->spinDownstreamWaterLevel->setValue(
        obj.value("downstream_water_level").toDouble(ui->spinDownstreamWaterLevel->value()));
    ui->spinUpstreamFlux->setValue(
        obj.value("upstream_flux").toDouble(ui->spinUpstreamFlux->value()));
    ui->spinDownstreamFlux->setValue(
        obj.value("downstream_flux").toDouble(ui->spinDownstreamFlux->value()));
    ui->spinMainPropertyNo->setValue(
        obj.value("main_property_no").toInt(ui->spinMainPropertyNo->value()));
    ui->spinControlledPropertyNo->setValue(
        obj.value("controlled_property_no").toInt(ui->spinControlledPropertyNo->value()));
    ui->checkNaturalFreeSurface->setChecked(
        obj.value("natural_free_surface").toBool(ui->checkNaturalFreeSurface->isChecked()));
    ui->checkDrainWell->setChecked(obj.value("drain_well").toBool(ui->checkDrainWell->isChecked()));
    const int computeType = obj.value("compute_type").toInt(ui->comboComputeType->currentData().toInt());
    int computeTypeIndex = ui->comboComputeType->findData(computeType);
    if (computeTypeIndex < 0)
        computeTypeIndex = 0;
    ui->comboComputeType->setCurrentIndex(computeTypeIndex);
    const QJsonArray globalMappingsArray = obj.value("global_mappings_table").toArray();
    globalMappings_.clear();
    for (int r = 0; r < globalMappingsArray.size(); ++r)
    {
        const QJsonObject rowObj = globalMappingsArray.at(r).toObject();
        globalMappings_.push_back(
            {rowObj.value("old").toInt(), rowObj.value("new").toInt()});
    }
    ui->spinIter->setValue(obj.value("iter").toInt(ui->spinIter->value()));
    const int itagFree = obj.value("itag_free").toInt(ui->comboItagFree->currentData().toInt());
    int itagIndex = ui->comboItagFree->findData(itagFree);
    if (itagIndex < 0)
        itagIndex = 0;
    ui->comboItagFree->setCurrentIndex(itagIndex);
    ui->spinDt->setValue(obj.value("dt").toDouble(ui->spinDt->value()));
    ui->spinTsta->setValue(obj.value("tsta").toDouble(ui->spinTsta->value()));
    ui->spinTend->setValue(obj.value("tend").toDouble(ui->spinTend->value()));
    auto loadPoints = [](const QJsonArray& array)
    {
        QVector<WaterLevelPoint> points;
        points.reserve(array.size());
        for (const auto& v : array)
        {
            const QJsonObject row = v.toObject();
            points.push_back(
                {row.value("time").toDouble(), row.value("up_head").toDouble(),
                 row.value("dw_head").toDouble()});
        }
        return points;
    };
    waterLevelPoints_ = loadPoints(obj.value("water_level_points").toArray());
}

void SeepageWidget::onComputeClicked()
{
    emit computeRequested();
}
