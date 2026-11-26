#include <CompactDoubleSpinBox.h>
#include "CaveReinforcementFieldPage.h"

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct FieldRow
{
    vtkIdType cellId = -1;
    int material = 0;
    int layer = 1;
    int dimension = 0;
    std::array<double, 3> center{{0.0, 0.0, 0.0}};
    std::array<double, 5> alfa{{1.0, 0.0, -1000.0, 0.0, 0.0}};
};

QVector<double> numbersInLine(const QString& line)
{
    static const QRegularExpression number(
        QStringLiteral(R"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[EeDd][-+]?\d+)?)"));
    QVector<double> values;
    auto it = number.globalMatch(line);
    while (it.hasNext())
    {
        QString token = it.next().captured();
        token.replace(QLatin1Char('D'), QLatin1Char('E'), Qt::CaseInsensitive);
        bool ok = false;
        const double value = token.toDouble(&ok);
        if (ok)
            values.push_back(value);
    }
    return values;
}

bool loadImat(const QString& filePath, QVector<QVector<double>>* values, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
            *error = QStringLiteral("无法读取当前材料设置。请检查材料后重试。");
        return false;
    }
    QTextStream in(&file);
    const QVector<double> header = numbersInLine(in.readLine());
    if (header.size() < 2)
    {
        if (error)
            *error = QStringLiteral("当前材料数据不完整，请重新保存材料设置。");
        return false;
    }
    const int materialCount = static_cast<int>(header.at(0));
    const int parameterCount = static_cast<int>(header.at(1));
    if (materialCount <= 0 || parameterCount <= 0)
    {
        if (error)
            *error = QStringLiteral("当前材料数据无效，请检查材料设置。");
        return false;
    }
    values->fill(QVector<double>(parameterCount + 1, 0.0), materialCount + 1);
    const int groupCount = (materialCount + 9) / 10;
    for (int group = 0; group < groupCount; ++group)
    {
        if (in.atEnd())
            break;
        in.readLine();
        if (in.atEnd())
            break;
        in.readLine();
        for (int parameter = 1; parameter <= parameterCount && !in.atEnd(); ++parameter)
        {
            const QVector<double> row = numbersInLine(in.readLine());
            const int columnCount = qMin(10, static_cast<int>(row.size()) - 1);
            for (int column = 0; column < columnCount; ++column)
            {
                const int material = group * 10 + column + 1;
                if (material <= materialCount)
                    (*values)[material][parameter] = row.at(column);
            }
        }
    }
    return true;
}

double materialValue(const QVector<QVector<double>>& values, int material, int parameter)
{
    if (material <= 0 || material >= values.size() || parameter <= 0 ||
        parameter >= values.at(material).size())
        return 0.0;
    return values.at(material).at(parameter);
}

std::array<double, 3> cellCenter(vtkUnstructuredGrid* grid, vtkCell* cell)
{
    std::array<double, 3> center{{0.0, 0.0, 0.0}};
    if (!grid || !cell || cell->GetNumberOfPoints() <= 0)
        return center;
    for (vtkIdType i = 0; i < cell->GetNumberOfPoints(); ++i)
    {
        double xyz[3]{};
        grid->GetPoint(cell->GetPointId(i), xyz);
        for (int axis = 0; axis < 3; ++axis)
            center[axis] += xyz[axis];
    }
    for (double& value : center)
        value /= cell->GetNumberOfPoints();
    return center;
}

double distanceSquared(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    double result = 0.0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double delta = a[axis] - b[axis];
        result += delta * delta;
    }
    return result;
}

double pointSegmentDistance(const std::array<double, 3>& point,
                            const std::array<double, 3>& a,
                            const std::array<double, 3>& b,
                            double extension,
                            bool* withinExtendedSegment)
{
    std::array<double, 3> ab{};
    double lengthSquared = 0.0;
    double projection = 0.0;
    for (int axis = 0; axis < 3; ++axis)
    {
        ab[axis] = b[axis] - a[axis];
        lengthSquared += ab[axis] * ab[axis];
        projection += (point[axis] - a[axis]) * ab[axis];
    }
    if (lengthSquared <= 1.0e-24)
    {
        if (withinExtendedSegment)
            *withinExtendedSegment = false;
        return std::numeric_limits<double>::max();
    }
    const double length = std::sqrt(lengthSquared);
    const double t = projection / lengthSquared;
    const double extensionRatio = extension / length;
    if (withinExtendedSegment)
        *withinExtendedSegment = t >= -extensionRatio && t <= 1.0 + extensionRatio;
    std::array<double, 3> closest{};
    for (int axis = 0; axis < 3; ++axis)
        closest[axis] = a[axis] + t * ab[axis];
    return std::sqrt(distanceSquared(point, closest));
}

struct FormulaResult
{
    double stiffnessRatio = 1.0;
    double deltaC = 0.0;
    double deltaPhi = 0.0;
    double deltaPressure = 0.0;
};

bool reinforcementFormula(int tag, double rockE, double rockC, double rockPhi,
                          double diameter, double barE, double shearStrength,
                          double spacing, double rowSpacing, double groupFactor,
                          double prestress, FormulaResult* output)
{
    if (!output || rockE <= 0.0 || diameter <= 0.0 || spacing <= 0.0 ||
        rowSpacing <= 0.0)
        return false;
    const double radians = rockPhi * kPi / 180.0;
    const double sinPhi = std::sin(radians);
    const double cosPhi = std::cos(radians);
    const double tanPhi = std::tan(radians);
    const double area = spacing * rowSpacing;
    const double singleBarArea = kPi * diameter * diameter / 4.0;
    const double pressure = prestress / area;
    double reinforcedE = rockE + 2.5 * pressure;
    double deltaC1 = 0.0;
    double deltaC2 = 0.0;
    double reinforcedPhi = rockPhi;

    if (tag == 1)
    {
        deltaC1 = groupFactor * shearStrength * singleBarArea / area;
    }
    else if (tag == 2 || tag == 3)
    {
        if (tag == 3)
            deltaC1 = groupFactor * shearStrength * singleBarArea / area;
        if (std::abs(sinPhi) <= 1.0e-12)
            return false;
        deltaC2 = groupFactor * prestress * tanPhi / (2.0 * area)
                  * (1.0 + 1.0 / sinPhi);
    }
    else if (tag == 4)
    {
        const double radius = diameter / 2.0;
        const double alpha = 2.0 * kPi * radius * tanPhi / area;
        reinforcedE = (barE * kPi * radius * radius
                       + rockE * (area - kPi * radius * radius)) / area;
        const double denominator = (1.0 + sinPhi) * alpha + 2.0;
        if (std::abs(denominator) <= 1.0e-12)
            return false;
        const double sinReinforced = qBound(-1.0,
            ((1.0 + sinPhi) * alpha + 2.0 * sinPhi) / denominator, 1.0);
        reinforcedPhi = std::asin(sinReinforced) * 180.0 / kPi;
        const double cosReinforced = std::cos(reinforcedPhi * kPi / 180.0);
        const double strengthDenominator = (1.0 + sinPhi) * cosReinforced;
        if (std::abs(strengthDenominator) <= 1.0e-12)
            return false;
        deltaC1 = rockC * (1.0 + alpha) * (1.0 - sinReinforced) * cosPhi
                  / strengthDenominator;
    }
    else
    {
        return false;
    }

    output->stiffnessRatio = reinforcedE / rockE;
    output->deltaC = deltaC1 + deltaC2;
    output->deltaPhi = reinforcedPhi - rockPhi;
    output->deltaPressure = pressure;
    return std::isfinite(output->stiffnessRatio) && std::isfinite(output->deltaC)
           && std::isfinite(output->deltaPhi) && std::isfinite(output->deltaPressure);
}

QString scientific(double value)
{
    return QString::number(value, 'E', 6);
}

bool writeAlfaie(const QString& path, int dimension, const QVector<FieldRow>& rows,
                 QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error)
            *error = QStringLiteral("无法准备围岩加固计算数据。");
        return false;
    }
    QTextStream out(&file);
    out << QStringLiteral("%1 %2 %3  NELEMT  MADIM  MAALFA\n")
               .arg(rows.size(), 8).arg(dimension, 8).arg(5, 8);
    out << (dimension == 2
                ? QStringLiteral("      KE          XC            YC              AKP       DC          LATYPE_JA      DFAI          DP\n")
                : QStringLiteral("      KE          XC            YC            ZC              AKP       DC          LATYPE_JA      DFAI          DP\n"));
    for (int index = 0; index < rows.size(); ++index)
    {
        const FieldRow& row = rows.at(index);
        out << QStringLiteral("%1 ").arg(index + 1, 8);
        for (int axis = 0; axis < dimension; ++axis)
            out << scientific(row.center[axis]).rightJustified(14) << QLatin1Char(' ');
        out << QString::number(row.alfa[0], 'f', 6).rightJustified(11) << QLatin1Char(' ');
        for (int column = 1; column < 5; ++column)
            out << scientific(row.alfa[column]).rightJustified(14) << QLatin1Char(' ');
        out << QLatin1Char('\n');
    }
    if (!file.commit())
    {
        if (error)
            *error = QStringLiteral("无法保存围岩加固计算数据。");
        return false;
    }
    return true;
}

bool writeAlfajd(const QString& path, vtkUnstructuredGrid* grid, int targetDimension,
                 const QVector<FieldRow>& rows, QString* error)
{
    if (!grid)
        return false;
    QVector<QVector<int>> connected(static_cast<int>(grid->GetNumberOfPoints()));
    for (int row = 0; row < rows.size(); ++row)
    {
        const FieldRow& value = rows.at(row);
        if (value.dimension != targetDimension || value.material == 1 || value.layer < 1)
            continue;
        vtkCell* cell = grid->GetCell(value.cellId);
        if (!cell || !cell->GetPointIds())
            continue;
        for (vtkIdType i = 0; i < cell->GetPointIds()->GetNumberOfIds(); ++i)
        {
            const vtkIdType pid = cell->GetPointId(i);
            if (pid >= 0 && pid < connected.size())
                connected[static_cast<int>(pid)].push_back(row);
        }
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error)
            *error = QStringLiteral("无法准备围岩加固计算数据。");
        return false;
    }
    QTextStream out(&file);
    out << QStringLiteral("%1 %2  NUMNP,MAALFA\n")
               .arg(grid->GetNumberOfPoints(), 8).arg(5, 8);
    out << QStringLiteral("      JD      AKP       DC          LATYPE_JA      DFAI          DP\n");
    for (vtkIdType pid = 0; pid < grid->GetNumberOfPoints(); ++pid)
    {
        std::array<double, 5> value{{1.0, 0.0, -1000.0, 0.0, 0.0}};
        const QVector<int>& adjacent = connected.at(static_cast<int>(pid));
        if (!adjacent.isEmpty())
        {
            value = {{0.0, 0.0, -1000.0, 0.0, 0.0}};
            for (int row : adjacent)
            {
                const auto& source = rows.at(row).alfa;
                value[0] += source[0] / adjacent.size();
                value[1] += source[1] / adjacent.size();
                value[2] = qMax(value[2], source[2]);
                value[3] += source[3] / adjacent.size();
                value[4] += source[4] / adjacent.size();
            }
        }
        out << QStringLiteral("%1 ").arg(pid + 1, 8)
            << QString::number(value[0], 'f', 6).rightJustified(11) << QLatin1Char(' ');
        for (int column = 1; column < 5; ++column)
            out << scientific(value[column]).rightJustified(14) << QLatin1Char(' ');
        out << QLatin1Char('\n');
    }
    if (!file.commit())
    {
        if (error)
            *error = QStringLiteral("无法保存围岩加固计算数据。");
        return false;
    }
    return true;
}

}

CaveReinforcementFieldPage::CaveReinforcementFieldPage(QWidget* parent)
    : BasePage(QStringLiteral("CaveReinforcementField"))
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto* modeForm = new QFormLayout();
    m_mode = new QComboBox(this);
    m_mode->addItem(QStringLiteral("关闭（不使用等效加固参数场）"),
                    static_cast<int>(Mode::Disabled));
    m_mode->addItem(QStringLiteral("根据现有杆单元计算"),
                    static_cast<int>(Mode::ExistingBars));
    m_mode->addItem(QStringLiteral("按开挖面概化参数计算"),
                    static_cast<int>(Mode::Conceptual));
    modeForm->addRow(QStringLiteral("计算方式"), m_mode);
    root->addLayout(modeForm);

    m_actualBox = new QGroupBox(QStringLiteral("根据现有杆单元计算"), this);
    auto* actualForm = new QFormLayout(m_actualBox);
    m_formula = new QComboBox(m_actualBox);
    m_formula->addItem(QStringLiteral("张建海系列公式"), 1);
    m_formula->addItem(QStringLiteral("Indraratna 公式"), 2);
    m_actualStiffness = new CompactDoubleSpinBox(m_actualBox);
    m_actualStiffness->setRange(1.0, 1000.0);
    m_actualStiffness->setDecimals(6);
    m_actualStiffness->setValue(1.05);
    m_actualStiffness->setToolTip(
        QStringLiteral("张建海系列公式使用的加固区模量增长倍数。"));
    m_indraratnaStiffnessCorrection = new CompactDoubleSpinBox(m_actualBox);
    m_indraratnaStiffnessCorrection->setRange(0.01, 1000.0);
    m_indraratnaStiffnessCorrection->setDecimals(6);
    m_indraratnaStiffnessCorrection->setValue(1.0);
    m_indraratnaStiffnessCorrection->setToolTip(
        QStringLiteral("乘在 Indraratna 公式自动计算的模量增长倍数上；1.0 表示不修正原公式结果，最终值不低于 1.0。"));
    actualForm->addRow(QStringLiteral("计算公式"), m_formula);
    actualForm->addRow(QStringLiteral("张建海公式模量增长倍数"), m_actualStiffness);
    actualForm->addRow(QStringLiteral("Indraratna 模量结果修正系数"),
                       m_indraratnaStiffnessCorrection);
    root->addWidget(m_actualBox);

    m_conceptBox = new QGroupBox(QStringLiteral("按开挖面概化参数计算"), this);
    auto* conceptForm = new QFormLayout(m_conceptBox);
    const auto makePositive = [this](double value)
    {
        auto* spin = new CompactDoubleSpinBox(this);
        spin->setRange(0.0, 1.0e9);
        spin->setDecimals(6);
        spin->setValue(value);
        return spin;
    };
    m_barLength = makePositive(9.0);
    m_barDeltaC = makePositive(0.30);
    m_cableLength = makePositive(30.0);
    m_cableDeltaC = makePositive(0.50);
    m_conceptStiffness = makePositive(1.05);
    m_conceptStiffness->setMinimum(1.0);
    conceptForm->addRow(QStringLiteral("锚杆作用深度（m）"), m_barLength);
    conceptForm->addRow(QStringLiteral("锚杆黏聚力增量（MPa）"), m_barDeltaC);
    conceptForm->addRow(QStringLiteral("锚索作用深度（m）"), m_cableLength);
    conceptForm->addRow(QStringLiteral("锚索黏聚力增量（MPa）"), m_cableDeltaC);
    conceptForm->addRow(QStringLiteral("加固区模量增长倍数"), m_conceptStiffness);
    root->addWidget(m_conceptBox);

    root->addStretch(1);

    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CaveReinforcementFieldPage::updateModeUi);
    connect(m_formula, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CaveReinforcementFieldPage::updateModeUi);
    updateModeUi();
}

void CaveReinforcementFieldPage::beforeLoad() {}
void CaveReinforcementFieldPage::beforeUnload() {}

CaveReinforcementFieldPage::Mode CaveReinforcementFieldPage::mode() const
{
    return static_cast<Mode>(m_mode->currentData().toInt());
}

void CaveReinforcementFieldPage::updateModeUi()
{
    m_actualBox->setVisible(mode() == Mode::ExistingBars);
    m_conceptBox->setVisible(mode() == Mode::Conceptual);
    const bool useZhangFormula = m_formula->currentData().toInt() == 1;
    m_actualStiffness->setEnabled(useZhangFormula);
    m_indraratnaStiffnessCorrection->setEnabled(!useZhangFormula);
}

QJsonObject CaveReinforcementFieldPage::saveJson()
{
    QJsonObject obj;
    obj[QStringLiteral("mode")] = static_cast<int>(mode());
    obj[QStringLiteral("formula")] = m_formula->currentData().toInt();
    obj[QStringLiteral("actual_stiffness")] = m_actualStiffness->value();
    obj[QStringLiteral("indraratna_stiffness_correction")] =
        m_indraratnaStiffnessCorrection->value();
    obj[QStringLiteral("bar_length")] = m_barLength->value();
    obj[QStringLiteral("bar_delta_c_mpa")] = m_barDeltaC->value();
    obj[QStringLiteral("cable_length")] = m_cableLength->value();
    obj[QStringLiteral("cable_delta_c_mpa")] = m_cableDeltaC->value();
    obj[QStringLiteral("concept_stiffness")] = m_conceptStiffness->value();
    return obj;
}

void CaveReinforcementFieldPage::loadJson(const QJsonObject& obj)
{
    int storedMode = obj.value(QStringLiteral("mode")).toInt(0);
    if (storedMode == 3)
        storedMode = static_cast<int>(Mode::Disabled);
    const int modeIndex = m_mode->findData(storedMode);
    if (modeIndex >= 0)
        m_mode->setCurrentIndex(modeIndex);
    const int formulaIndex = m_formula->findData(obj.value(QStringLiteral("formula")).toInt(1));
    if (formulaIndex >= 0)
        m_formula->setCurrentIndex(formulaIndex);
    m_actualStiffness->setValue(obj.value(QStringLiteral("actual_stiffness")).toDouble(1.05));
    m_indraratnaStiffnessCorrection->setValue(
        obj.value(QStringLiteral("indraratna_stiffness_correction")).toDouble(1.0));
    m_barLength->setValue(obj.value(QStringLiteral("bar_length")).toDouble(9.0));
    m_barDeltaC->setValue(obj.value(QStringLiteral("bar_delta_c_mpa")).toDouble(0.30));
    m_cableLength->setValue(obj.value(QStringLiteral("cable_length")).toDouble(30.0));
    m_cableDeltaC->setValue(obj.value(QStringLiteral("cable_delta_c_mpa")).toDouble(0.50));
    m_conceptStiffness->setValue(obj.value(QStringLiteral("concept_stiffness")).toDouble(1.05));
    updateModeUi();
}

bool CaveReinforcementFieldPage::exportFieldFiles(
    const QString& inputDirectory, vtkUnstructuredGrid* grid, int targetDimension,
    const QHash<vtkIdType, int>& effectiveCellLayers,
    const QVector<ReinforcementFaceSample>& excavationFaces, QString* errorMessage) const
{
    QDir directory(inputDirectory);
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath()))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法准备围岩加固计算数据。请先保存工程后重试。");
        return false;
    }
    const QString alfaiePath = directory.filePath(QStringLiteral("ALFAIE.DAT"));
    const QString alfajdPath = directory.filePath(QStringLiteral("ALFAJD.DAT"));
    if (mode() == Mode::Disabled)
    {
        QFile::remove(alfaiePath);
        QFile::remove(alfajdPath);
        return true;
    }
    if (!grid || grid->GetNumberOfCells() <= 0 || targetDimension < 2 || targetDimension > 3)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("当前网格不能生成围岩等效加固参数场。");
        return false;
    }

    vtkDataArray* materials = grid->GetCellData()->GetArray("MATYPE");
    vtkDataArray* layers = grid->GetCellData()->GetArray("LAYER");
    QVector<FieldRow> rows;
    rows.reserve(static_cast<int>(grid->GetNumberOfCells()));
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || (cell->GetCellDimension() != targetDimension &&
                      cell->GetCellDimension() != 1))
            continue;
        FieldRow row;
        row.cellId = cellId;
        row.dimension = cell->GetCellDimension();
        row.material = materials ? static_cast<int>(materials->GetTuple1(cellId)) : 2;
        row.layer = effectiveCellLayers.contains(cellId)
                        ? effectiveCellLayers.value(cellId)
                        : (layers ? static_cast<int>(layers->GetTuple1(cellId)) : 1);
        row.center = cellCenter(grid, cell);
        if (row.dimension == 1)
            row.alfa[2] = 0.0;
        rows.push_back(row);
    }
    if (rows.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("当前网格没有可导出的实体或杆单元。");
        return false;
    }

    QVector<QVector<double>> imat;
    if (!loadImat(directory.filePath(QStringLiteral("IMAT.DAT")), &imat, errorMessage))
        return false;

    if (mode() == Mode::Conceptual)
    {
        if (excavationFaces.isEmpty())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("没有可用于概化加固计算的开挖面。");
            return false;
        }
        if (m_cableLength->value() < m_barLength->value())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("锚索作用深度不能小于锚杆作用深度。");
            return false;
        }
        for (FieldRow& row : rows)
        {
            row.alfa = {{1.0, 0.0, 0.0, 0.0, 0.0}};
            if (row.dimension != targetDimension || row.material == 1 || row.layer < 0)
                continue;
            const int excavationFlag = static_cast<int>(materialValue(imat, row.material, 100));
            if (excavationFlag == -1 || excavationFlag == 1)
                continue;

            double minimumDistanceSquared = std::numeric_limits<double>::max();
            int nearestLayer = 0;
            for (const ReinforcementFaceSample& face : excavationFaces)
            {
                const double value = distanceSquared(row.center, face.center);
                if (value < minimumDistanceSquared)
                {
                    minimumDistanceSquared = value;
                    nearestLayer = face.layer;
                }
            }
            const double distance = std::sqrt(minimumDistanceSquared);
            if (distance <= m_barLength->value())
            {
                row.alfa[0] = m_conceptStiffness->value();
                row.alfa[1] = (m_barDeltaC->value() + m_cableDeltaC->value()) * 1.0e6;
                row.alfa[2] = nearestLayer;
            }
            else if (distance < m_cableLength->value())
            {
                row.alfa[0] = m_conceptStiffness->value();
                row.alfa[1] = m_cableDeltaC->value() * 1.0e6;
                row.alfa[2] = nearestLayer;
            }
        }
    }
    else if (mode() == Mode::ExistingBars)
    {
            struct Bar
            {
                std::array<double, 3> a{};
                std::array<double, 3> b{};
                int material = 0;
                int layer = -1;
                double length = 0.0;
            };
            QVector<Bar> bars;
            for (const FieldRow& row : rows)
            {
                if (row.dimension != 1)
                    continue;
                vtkCell* cell = grid->GetCell(row.cellId);
                if (!cell || cell->GetNumberOfPoints() < 2)
                    continue;
                Bar bar;
                bar.material = row.material;
                bar.layer = row.layer;
                const double configuredArea = materialValue(imat, bar.material, 8);
                if (configuredArea <= 0.0)
                    continue;
                double xyz[3]{};
                grid->GetPoint(cell->GetPointId(0), xyz);
                std::copy(xyz, xyz + 3, bar.a.begin());
                grid->GetPoint(cell->GetPointId(1), xyz);
                std::copy(xyz, xyz + 3, bar.b.begin());
                bar.length = std::sqrt(distanceSquared(bar.a, bar.b));
                if (bar.length > 1.0e-12)
                    bars.push_back(bar);
            }
            if (bars.isEmpty())
            {
                if (errorMessage)
                    *errorMessage = QStringLiteral("当前网格中没有已配置杆材料的一维杆单元。请先布置杆单元并完成杆材料设置。");
                return false;
            }
            const int formula = m_formula->currentData().toInt();
            for (FieldRow& row : rows)
            {
                if (row.dimension != targetDimension || row.material == 1 || row.layer < 0)
                    continue;
                const int excavationFlag = static_cast<int>(materialValue(imat, row.material, 100));
                if (excavationFlag == -1 || excavationFlag == 1)
                    continue;
                const double rockE = materialValue(imat, row.material, 2);
                const double rockC = materialValue(imat, row.material, 12);
                const double rockPhi = materialValue(imat, row.material, 11);
                std::array<double, 4> maxC{};
                std::array<double, 4> maxPhi{};
                std::array<double, 4> maxPressure{};
                bool reinforced = false;
                double maximumStiffnessRatio = 1.0;
                double nearestDistance = std::numeric_limits<double>::max();
                int nearestLayer = -1000;
                for (const Bar& bar : bars)
                {
                    const double spacing = materialValue(imat, bar.material, 38);
                    const double rowSpacing = materialValue(imat, bar.material, 37);
                    if (spacing <= 0.0 || rowSpacing <= 0.0 || spacing > 9.9 || rowSpacing > 9.9)
                        continue;
                    const double equivalentRadius = std::sqrt(spacing * rowSpacing / kPi);
                    const double boundingDistance = qMax(spacing, rowSpacing);
                    bool outsideBounds = false;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        if (row.center[axis] < qMin(bar.a[axis], bar.b[axis]) - boundingDistance
                            || row.center[axis] > qMax(bar.a[axis], bar.b[axis]) + boundingDistance)
                        {
                            outsideBounds = true;
                            break;
                        }
                    }
                    if (outsideBounds)
                        continue;
                    bool within = false;
                    const double distance = pointSegmentDistance(
                        row.center, bar.a, bar.b, 0.5 * equivalentRadius, &within);
                    const double influenceDistance = qMin(0.5 * bar.length,
                                                          qMax(spacing, rowSpacing));
                    if (!within || distance >= influenceDistance)
                        continue;

                    const double barArea = materialValue(imat, bar.material, 8);
                    const double diameter = std::sqrt(4.0 * barArea / kPi);
                    const double tensileStrength = materialValue(imat, bar.material, 92);
                    const double prestress = materialValue(imat, bar.material, 93)
                                             * materialValue(imat, bar.material, 89);
                    int tag = 4;
                    if (formula == 1)
                    {
                        if (prestress < 0.01)
                            tag = 1;
                        else
                            tag = tensileStrength < 390.0e6 ? 3 : 2;
                    }
                    FormulaResult result;
                    if (!reinforcementFormula(
                            tag, rockE, rockC, rockPhi, diameter,
                            materialValue(imat, bar.material, 2),
                            materialValue(imat, bar.material, 12), spacing, rowSpacing,
                            3.5, prestress, &result))
                        continue;
                    reinforced = true;
                    const int index = qBound(1, tag, 4) - 1;
                    maxC[index] = qMax(maxC[index], result.deltaC);
                    maxPhi[index] = qMax(maxPhi[index], result.deltaPhi);
                    maxPressure[index] = qMax(maxPressure[index], result.deltaPressure);
                    maximumStiffnessRatio = qMax(maximumStiffnessRatio,
                                                  result.stiffnessRatio);
                    if (distance < nearestDistance)
                    {
                        nearestDistance = distance;
                        nearestLayer = bar.layer;
                    }
                }
                if (!reinforced)
                    continue;
                row.alfa[0] = formula == 1 ? m_actualStiffness->value()
                                           : qMax(1.0, maximumStiffnessRatio
                                                           * m_indraratnaStiffnessCorrection->value());
                for (int index = 0; index < 4; ++index)
                {
                    row.alfa[1] += maxC[index];
                    row.alfa[3] += maxPhi[index];
                    row.alfa[4] += maxPressure[index];
                }
                row.alfa[2] = nearestLayer;
            }
        }

    if (!writeAlfaie(alfaiePath, targetDimension, rows, errorMessage))
        return false;
    if (!writeAlfajd(alfajdPath, grid, targetDimension, rows, errorMessage))
        return false;
    return true;
}
