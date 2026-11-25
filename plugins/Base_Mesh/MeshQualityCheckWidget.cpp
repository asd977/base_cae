#include <CompactDoubleSpinBox.h>
#include "MeshQualityCheckWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{

const QStringList& qualityCaseNames()
{
    static const QStringList names = {
        QStringLiteral("全面检查"), QStringLiteral("研究对象合理性检查"),
        QStringLiteral("垂直轴合理性检查"), QStringLiteral("单元材料检查"),
        QStringLiteral("单元类型合理性检查"), QStringLiteral("单元施工层检查"),
        QStringLiteral("单元重复性检查"), QStringLiteral("单元其他属性合理性检查"),
        QStringLiteral("杆单元端点位置检查"), QStringLiteral("线对线及点对点单元检查"),
        QStringLiteral("共线不合理节点检查"), QStringLiteral("同坐标不同节点编号检查"),
        QStringLiteral("计算范围检查"), QStringLiteral("土坝计算层检查"),
        QStringLiteral("单元节点排序检查"), QStringLiteral("雅可比求逆检查"),
        QStringLiteral("夹层单元厚度检查"), QStringLiteral("单元长细比检查"),
        QStringLiteral("土坝悬空填筑单元检查"), QStringLiteral("材料参数合理性检查")};
    return names;
}

QDoubleSpinBox* createDoubleSpin(QWidget* parent,
                                 double minimum,
                                 double maximum,
                                 int decimals)
{
    auto* spin = new CompactDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setKeyboardTracking(false);
    return spin;
}

} // namespace

MeshQualityCheckWidget::MeshQualityCheckWidget(QWidget* parent)
    : BasePage(QStringLiteral("MeshQualityCheck"))
{
    setParent(parent);
    setObjectName(QStringLiteral("meshQualityCheckPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("meshQualityCheckScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setObjectName(QStringLiteral("meshQualityCheckViewport"));
    scroll->viewport()->setAttribute(Qt::WA_StyledBackground, true);
    pageLayout->addWidget(scroll);

    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("meshQualityCheckContent"));
    content->setAttribute(Qt::WA_StyledBackground, true);
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(14, 12, 14, 14);
    mainLayout->setSpacing(10);

    m_meshSummary = new QLabel(QStringLiteral("尚未载入网格"), content);
    m_meshSummary->setWordWrap(true);
    mainLayout->addWidget(m_meshSummary);

    auto* basicGroup = new QGroupBox(QStringLiteral("检查内容"), content);
    auto* basicForm = new QFormLayout(basicGroup);
    basicForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_checkCase = new QComboBox(basicGroup);
    const auto& names = qualityCaseNames();
    for (int i = 0; i < names.size(); ++i)
        m_checkCase->addItem(QStringLiteral("%1：%2").arg(i).arg(names.at(i)), i);
    basicForm->addRow(QStringLiteral("检查项目"), m_checkCase);

    m_expectedObject = new QComboBox(basicGroup);
    m_expectedObject->addItem(QStringLiteral("洞室"), 6);
    m_expectedObject->addItem(QStringLiteral("均质坝"), 8);
    m_expectedObject->addItem(QStringLiteral("边坡"), 7);
    basicForm->addRow(QStringLiteral("研究对象"), m_expectedObject);
    mainLayout->addWidget(basicGroup);

    m_handlingGroup = new QGroupBox(QStringLiteral("异常处理"), content);
    m_handlingForm = new QFormLayout(m_handlingGroup);
    m_handlingForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_objectHandling = new QComboBox(m_handlingGroup);
    m_objectHandling->addItem(QStringLiteral("仅报告，不修改"), false);
    m_objectHandling->addItem(QStringLiteral("按所选研究对象修正"), true);
    m_handlingForm->addRow(QStringLiteral("研究对象不一致"), m_objectHandling);

    m_verticalHandling = new QComboBox(m_handlingGroup);
    m_verticalHandling->addItem(QStringLiteral("仅报告，不修改"), 0);
    m_verticalHandling->addItem(QStringLiteral("修正为 Y 轴"), 2);
    m_verticalHandling->addItem(QStringLiteral("修正为 Z 轴"), 3);
    m_handlingForm->addRow(QStringLiteral("垂直轴无效"), m_verticalHandling);

    m_attributeHandling = new QComboBox(m_handlingGroup);
    m_attributeHandling->addItem(QStringLiteral("仅报告，不修改"), false);
    m_attributeHandling->addItem(QStringLiteral("清零异常属性"), true);
    m_handlingForm->addRow(QStringLiteral("单元其他属性异常"), m_attributeHandling);

    m_nearbyHandling = new QComboBox(m_handlingGroup);
    m_nearbyHandling->addItem(QStringLiteral("仅报告，不替换节点"), false);
    m_nearbyHandling->addItem(QStringLiteral("合并距离过近的节点"), true);
    m_handlingForm->addRow(QStringLiteral("同坐标节点"), m_nearbyHandling);

    m_boundaryHandling = new QComboBox(m_handlingGroup);
    m_boundaryHandling->addItem(QStringLiteral("按当前极值归整邻近节点"), false);
    m_boundaryHandling->addItem(QStringLiteral("使用指定边界值"), true);
    m_handlingForm->addRow(QStringLiteral("计算范围"), m_boundaryHandling);

    m_applyCorrections = new QCheckBox(QStringLiteral("将修正结果应用到当前网格"), m_handlingGroup);
    m_applyCorrections->setChecked(false);
    m_handlingForm->addRow(QString(), m_applyCorrections);
    mainLayout->addWidget(m_handlingGroup);

    m_parameterGroup = new QGroupBox(QStringLiteral("检查参数"), content);
    m_parameterForm = new QFormLayout(m_parameterGroup);
    m_parameterForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_minimumDistance = createDoubleSpin(m_parameterGroup, 0.0, 1.0e15, 10);
    m_parameterForm->addRow(QStringLiteral("节点最小距离"), m_minimumDistance);

    m_boundaryTolerance = createDoubleSpin(m_parameterGroup, 0.0, 1.0e15, 10);
    m_parameterForm->addRow(QStringLiteral("边界坐标容限"), m_boundaryTolerance);

    const QStringList boundNames = {QStringLiteral("X 最小值"), QStringLiteral("X 最大值"),
                                    QStringLiteral("Y 最小值"), QStringLiteral("Y 最大值"),
                                    QStringLiteral("Z 最小值"), QStringLiteral("Z 最大值")};
    for (int i = 0; i < static_cast<int>(m_boundSpins.size()); ++i)
    {
        m_boundSpins[i] = createDoubleSpin(m_parameterGroup, -1.0e15, 1.0e15, 8);
        m_parameterForm->addRow(boundNames.at(i), m_boundSpins[i]);
    }

    m_jacobianTolerance = createDoubleSpin(m_parameterGroup, 0.0, 1.0e15, 12);
    m_parameterForm->addRow(QStringLiteral("雅可比行列式容限"), m_jacobianTolerance);

    m_jacobianStart = new QSpinBox(m_parameterGroup);
    m_jacobianStart->setRange(1, 2147483647);
    m_jacobianStart->setKeyboardTracking(false);
    m_parameterForm->addRow(QStringLiteral("雅可比检查起始单元"), m_jacobianStart);

    m_aspectRatio = createDoubleSpin(m_parameterGroup, 0.0, 1.0e12, 6);
    m_parameterForm->addRow(QStringLiteral("最大长细比"), m_aspectRatio);
    mainLayout->addWidget(m_parameterGroup);

    m_status = new QLabel(QStringLiteral("未执行检查"), content);
    m_status->setWordWrap(true);
    mainLayout->addWidget(m_status);

    m_startButton = new QPushButton(QStringLiteral("开始检查"), content);
    mainLayout->addWidget(m_startButton, 0, Qt::AlignRight);
    mainLayout->addStretch(1);
    scroll->setWidget(content);

    connect(m_checkCase, &QComboBox::currentIndexChanged,
            this, [this]() { updateVisibleRows(); });
    connect(m_boundaryHandling, &QComboBox::currentIndexChanged,
            this, [this]() { updateVisibleRows(); });
    connect(m_startButton, &QPushButton::clicked, this, [this]() {
        if (m_running)
            emit stopRequested();
        else
            emit checkRequested();
    });

    MeshQualityCheckOptions defaults;
    setOptions(defaults);
    updateVisibleRows();
}

MeshQualityCheckOptions MeshQualityCheckWidget::options() const
{
    MeshQualityCheckOptions value;
    value.checkCase = m_checkCase->currentData().toInt();
    value.expectedObject = m_expectedObject->currentData().toInt();
    value.repairObject = m_objectHandling->currentData().toBool();
    value.repairVerticalAxis = m_verticalHandling->currentData().toInt();
    value.repairAttributes = m_attributeHandling->currentData().toBool();
    value.minimumNodeDistance = m_minimumDistance->value();
    value.replaceNearbyNodes = m_nearbyHandling->currentData().toBool();
    value.boundaryTolerance = m_boundaryTolerance->value();
    value.useSpecifiedBounds = m_boundaryHandling->currentData().toBool();
    for (int i = 0; i < static_cast<int>(m_boundSpins.size()); ++i)
        value.bounds[i] = m_boundSpins[i]->value();
    value.jacobianTolerance = m_jacobianTolerance->value();
    value.jacobianStartElement = m_jacobianStart->value();
    value.maximumAspectRatio = m_aspectRatio->value();
    value.applyCorrections = m_applyCorrections->isChecked();
    return value;
}

void MeshQualityCheckWidget::setOptions(const MeshQualityCheckOptions& value)
{
    m_checkCase->setCurrentIndex(qBound(0, value.checkCase, m_checkCase->count() - 1));
    int objectIndex = m_expectedObject->findData(value.expectedObject);
    if (objectIndex < 0)
        objectIndex = 0;
    m_expectedObject->setCurrentIndex(objectIndex);
    m_objectHandling->setCurrentIndex(value.repairObject ? 1 : 0);
    const int verticalIndex = m_verticalHandling->findData(value.repairVerticalAxis);
    m_verticalHandling->setCurrentIndex(verticalIndex >= 0 ? verticalIndex : 0);
    m_attributeHandling->setCurrentIndex(value.repairAttributes ? 1 : 0);
    m_minimumDistance->setValue(value.minimumNodeDistance);
    m_nearbyHandling->setCurrentIndex(value.replaceNearbyNodes ? 1 : 0);
    m_boundaryTolerance->setValue(value.boundaryTolerance);
    m_boundaryHandling->setCurrentIndex(value.useSpecifiedBounds ? 1 : 0);
    for (int i = 0; i < static_cast<int>(m_boundSpins.size()); ++i)
        m_boundSpins[i]->setValue(value.bounds[i]);
    m_jacobianTolerance->setValue(value.jacobianTolerance);
    m_jacobianStart->setValue(value.jacobianStartElement);
    m_aspectRatio->setValue(value.maximumAspectRatio);
    m_applyCorrections->setChecked(value.applyCorrections);
    updateVisibleRows();
}

void MeshQualityCheckWidget::setMeshContext(int suggestedObject,
                                            int verticalAxis,
                                            qint64 cellCount,
                                            int dimension,
                                            const std::array<double, 6>& bounds,
                                            double characteristicLength,
                                            bool useSuggestedObject)
{
    m_meshBounds = bounds;
    m_characteristicLength = characteristicLength > 0.0 ? characteristicLength : 1.0;
    m_meshDimension = qBound(2, dimension, 3);
    m_cellCount = static_cast<int>(qBound<qint64>(qint64(1), cellCount,
                                                  qint64(2147483647)));
    m_jacobianStart->setMaximum(m_cellCount);

    if (useSuggestedObject)
    {
        const int index = m_expectedObject->findData(suggestedObject);
        if (index >= 0)
            m_expectedObject->setCurrentIndex(index);
    }
    const int verticalIndex = m_verticalHandling->findData(verticalAxis == 3 ? 3 : 2);
    if (verticalIndex >= 0 && m_verticalHandling->currentData().toInt() != 0)
        m_verticalHandling->setCurrentIndex(verticalIndex);

    m_meshSummary->setText(
        QStringLiteral("当前网格：%1 个单元，%2 维，特征尺寸约 %3")
            .arg(cellCount)
            .arg(m_meshDimension)
            .arg(m_characteristicLength, 0, 'g', 8));

    if (useSuggestedObject)
        updateAutomaticParameters();
}

void MeshQualityCheckWidget::setRunning(bool running)
{
    m_running = running;
    m_startButton->setEnabled(true);
    m_startButton->setText(running ? QStringLiteral("停止检查") : QStringLiteral("开始检查"));
    if (running)
        setStatus(QStringLiteral("检查正在进行，详细过程请查看日志。"));
}

void MeshQualityCheckWidget::setStatus(const QString& text, bool isError)
{
    Q_UNUSED(isError);
    m_status->setText(text);
}

QJsonObject MeshQualityCheckWidget::saveJson()
{
    const MeshQualityCheckOptions value = options();
    QJsonObject object;
    object[QStringLiteral("check_case")] = value.checkCase;
    object[QStringLiteral("expected_object")] = value.expectedObject;
    object[QStringLiteral("repair_object")] = value.repairObject;
    object[QStringLiteral("repair_vertical_axis")] = value.repairVerticalAxis;
    object[QStringLiteral("repair_attributes")] = value.repairAttributes;
    object[QStringLiteral("minimum_node_distance")] = value.minimumNodeDistance;
    object[QStringLiteral("replace_nearby_nodes")] = value.replaceNearbyNodes;
    object[QStringLiteral("boundary_tolerance")] = value.boundaryTolerance;
    object[QStringLiteral("use_specified_bounds")] = value.useSpecifiedBounds;
    QJsonArray bounds;
    for (double bound : value.bounds)
        bounds.append(bound);
    object[QStringLiteral("bounds")] = bounds;
    object[QStringLiteral("jacobian_tolerance")] = value.jacobianTolerance;
    object[QStringLiteral("jacobian_start_element")] = value.jacobianStartElement;
    object[QStringLiteral("maximum_aspect_ratio")] = value.maximumAspectRatio;
    object[QStringLiteral("apply_corrections")] = value.applyCorrections;
    return object;
}

void MeshQualityCheckWidget::loadJson(const QJsonObject& object)
{
    MeshQualityCheckOptions value;
    value.checkCase = object.value(QStringLiteral("check_case")).toInt(value.checkCase);
    value.expectedObject = object.value(QStringLiteral("expected_object")).toInt(value.expectedObject);
    value.repairObject = object.value(QStringLiteral("repair_object")).toBool(value.repairObject);
    value.repairVerticalAxis = object.value(QStringLiteral("repair_vertical_axis"))
                                   .toInt(value.repairVerticalAxis);
    value.repairAttributes = object.value(QStringLiteral("repair_attributes"))
                                 .toBool(value.repairAttributes);
    value.minimumNodeDistance = object.value(QStringLiteral("minimum_node_distance"))
                                    .toDouble(value.minimumNodeDistance);
    value.replaceNearbyNodes = object.value(QStringLiteral("replace_nearby_nodes"))
                                   .toBool(value.replaceNearbyNodes);
    value.boundaryTolerance = object.value(QStringLiteral("boundary_tolerance"))
                                  .toDouble(value.boundaryTolerance);
    value.useSpecifiedBounds = object.value(QStringLiteral("use_specified_bounds"))
                                   .toBool(value.useSpecifiedBounds);
    const QJsonArray bounds = object.value(QStringLiteral("bounds")).toArray();
    for (int i = 0; i < qMin(bounds.size(), static_cast<int>(value.bounds.size())); ++i)
        value.bounds[i] = bounds.at(i).toDouble(value.bounds[i]);
    value.jacobianTolerance = object.value(QStringLiteral("jacobian_tolerance"))
                                  .toDouble(value.jacobianTolerance);
    value.jacobianStartElement = object.value(QStringLiteral("jacobian_start_element"))
                                     .toInt(value.jacobianStartElement);
    value.maximumAspectRatio = object.value(QStringLiteral("maximum_aspect_ratio"))
                                   .toDouble(value.maximumAspectRatio);
    value.applyCorrections = object.value(QStringLiteral("apply_corrections"))
                                 .toBool(value.applyCorrections);
    setOptions(value);
}

void MeshQualityCheckWidget::beforeLoad()
{
}

void MeshQualityCheckWidget::beforeUnload()
{
}

void MeshQualityCheckWidget::updateVisibleRows()
{
    const int selectedCase = m_checkCase->currentData().toInt();
    const auto applies = [selectedCase](int caseNumber) {
        return selectedCase == 0 || selectedCase == caseNumber;
    };
    m_handlingForm->setRowVisible(m_objectHandling, applies(1));
    m_handlingForm->setRowVisible(m_verticalHandling, applies(2));
    m_handlingForm->setRowVisible(m_attributeHandling, applies(7));
    m_handlingForm->setRowVisible(m_nearbyHandling, applies(11));
    m_handlingForm->setRowVisible(m_boundaryHandling, applies(12));
    m_parameterForm->setRowVisible(m_minimumDistance, applies(11));
    m_parameterForm->setRowVisible(m_boundaryTolerance, applies(12));
    for (auto* spin : m_boundSpins)
        m_parameterForm->setRowVisible(spin,
                                       applies(12) && m_boundaryHandling->currentData().toBool());
    m_parameterForm->setRowVisible(m_jacobianTolerance, applies(15));
    m_parameterForm->setRowVisible(m_jacobianStart, applies(15));
    m_parameterForm->setRowVisible(m_aspectRatio, applies(17));
    m_handlingGroup->setVisible(applies(1) || applies(2) || applies(7)
                                || applies(11) || applies(12));
    m_parameterGroup->setVisible(applies(11) || applies(12) || applies(15) || applies(17));
}

void MeshQualityCheckWidget::updateAutomaticParameters()
{
    double diagonalSquared = 0.0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double length = m_meshBounds[axis * 2 + 1] - m_meshBounds[axis * 2];
        diagonalSquared += length * length;
    }
    const double diagonal = std::sqrt(diagonalSquared);
    const double numericalFloor = std::max(diagonal * 1.0e-10, 1.0e-12);
    const double scale = std::max(m_characteristicLength, numericalFloor);

    m_minimumDistance->setValue(std::max(scale * 1.0e-6, numericalFloor));
    m_boundaryTolerance->setValue(std::max(scale * 1.0e-5, numericalFloor));
    for (int i = 0; i < static_cast<int>(m_boundSpins.size()); ++i)
        m_boundSpins[i]->setValue(m_meshBounds[i]);
    m_jacobianTolerance->setValue(
        std::max(std::pow(scale, m_meshDimension) * 1.0e-6, 1.0e-12));
    m_jacobianStart->setValue(1);
    m_aspectRatio->setValue(10.0);
}
