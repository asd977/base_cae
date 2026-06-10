#include "GeometryImportWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <IGESControl_Reader.hxx>
#include <Precision.hxx>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTextStream>
#include <QVBoxLayout>
#include <QVector>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>

#include "GeomManagerInstance.h"

namespace
{
struct DxfPair
{
    int code = -1;
    QString value;
};

static double dxfValue(const QVector<DxfPair>& pairs, int code, double fallback = 0.0)
{
    for (const auto& pair : pairs)
    {
        if (pair.code == code)
            return pair.value.toDouble();
    }
    return fallback;
}

static int dxfIntValue(const QVector<DxfPair>& pairs, int code, int fallback = 0)
{
    for (const auto& pair : pairs)
    {
        if (pair.code == code)
            return pair.value.toInt();
    }
    return fallback;
}

static gp_Pnt dxfPoint(const QVector<DxfPair>& pairs, int xCode, int yCode, int zCode,
                       double fallbackZ = 0.0)
{
    return gp_Pnt(dxfValue(pairs, xCode), dxfValue(pairs, yCode),
                  dxfValue(pairs, zCode, fallbackZ));
}

static void addDxfLine(BRep_Builder& builder, TopoDS_Compound& compound, const gp_Pnt& p1,
                       const gp_Pnt& p2)
{
    if (p1.Distance(p2) <= Precision::Confusion())
        return;

    BRepBuilderAPI_MakeEdge makeEdge(p1, p2);
    if (makeEdge.IsDone())
    {
        const TopoDS_Shape edge = makeEdge.Edge();
        builder.Add(compound, edge);
    }
}

static void addDxfCircle(BRep_Builder& builder, TopoDS_Compound& compound, const gp_Pnt& center,
                         double radius)
{
    if (radius <= Precision::Confusion())
        return;

    BRepBuilderAPI_MakeEdge makeEdge(gp_Circ(gp_Ax2(center, gp_Dir(0, 0, 1)), radius));
    if (makeEdge.IsDone())
    {
        const TopoDS_Shape edge = makeEdge.Edge();
        builder.Add(compound, edge);
    }
}

static void addDxfArc(BRep_Builder& builder, TopoDS_Compound& compound, const gp_Pnt& center,
                      double radius, double startDegree, double endDegree)
{
    if (radius <= Precision::Confusion())
        return;

    constexpr double kPi = 3.14159265358979323846;
    const double startRad = startDegree * kPi / 180.0;
    double endRad = endDegree * kPi / 180.0;
    if (endRad <= startRad)
        endRad += 2.0 * kPi;

    BRepBuilderAPI_MakeEdge makeEdge(gp_Circ(gp_Ax2(center, gp_Dir(0, 0, 1)), radius), startRad,
                                     endRad);
    if (makeEdge.IsDone())
    {
        const TopoDS_Shape edge = makeEdge.Edge();
        builder.Add(compound, edge);
    }
}

static void addDxfLwPolyline(BRep_Builder& builder, TopoDS_Compound& compound,
                             const QVector<DxfPair>& pairs)
{
    QVector<gp_Pnt> points;
    double x = 0.0;
    bool hasX = false;
    for (const auto& pair : pairs)
    {
        if (pair.code == 10)
        {
            x = pair.value.toDouble();
            hasX = true;
        }
        else if (pair.code == 20 && hasX)
        {
            points.push_back(gp_Pnt(x, pair.value.toDouble(), 0.0));
            hasX = false;
        }
    }

    for (int i = 1; i < points.size(); ++i)
        addDxfLine(builder, compound, points[i - 1], points[i]);

    const bool closed = (dxfIntValue(pairs, 70) & 1) != 0;
    if (closed && points.size() > 2)
        addDxfLine(builder, compound, points.last(), points.first());
}

static bool readDxfFile(const QString& filePath, TopoDS_Shape* shape, QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = QObject::tr("无法打开 DXF 文件：%1").arg(file.errorString());
        return false;
    }

    QVector<DxfPair> allPairs;
    QTextStream stream(&file);
    while (!stream.atEnd())
    {
        const QString codeLine = stream.readLine().trimmed();
        if (stream.atEnd())
            break;
        const QString valueLine = stream.readLine().trimmed();
        bool ok = false;
        const int code = codeLine.toInt(&ok);
        if (ok)
            allPairs.push_back({code, valueLine});
    }

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    int edgeCount = 0;

    QString currentType;
    QVector<DxfPair> entityPairs;
    auto flushEntity = [&]()
    {
        if (currentType.isEmpty())
            return;

        if (currentType == "LINE")
        {
            const auto p1 = dxfPoint(entityPairs, 10, 20, 30);
            const auto p2 = dxfPoint(entityPairs, 11, 21, 31);
            if (p1.Distance(p2) > Precision::Confusion())
            {
                addDxfLine(builder, compound, p1, p2);
                ++edgeCount;
            }
        }
        else if (currentType == "CIRCLE")
        {
            const double radius = dxfValue(entityPairs, 40);
            if (radius > Precision::Confusion())
            {
                addDxfCircle(builder, compound, dxfPoint(entityPairs, 10, 20, 30), radius);
                ++edgeCount;
            }
        }
        else if (currentType == "ARC")
        {
            const double radius = dxfValue(entityPairs, 40);
            if (radius > Precision::Confusion())
            {
                addDxfArc(builder, compound, dxfPoint(entityPairs, 10, 20, 30), radius,
                          dxfValue(entityPairs, 50), dxfValue(entityPairs, 51));
                ++edgeCount;
            }
        }
        else if (currentType == "LWPOLYLINE")
        {
            const int pointCount = std::count_if(entityPairs.cbegin(), entityPairs.cend(),
                                                 [](const DxfPair& pair)
                                                 {
                                                     return pair.code == 10;
                                                 });
            if (pointCount > 1)
            {
                addDxfLwPolyline(builder, compound, entityPairs);
                edgeCount += pointCount - 1 + (((dxfIntValue(entityPairs, 70) & 1) != 0) ? 1 : 0);
            }
        }
    };

    for (const auto& pair : allPairs)
    {
        if (pair.code == 0)
        {
            flushEntity();
            currentType.clear();
            entityPairs.clear();
            const QString type = pair.value.trimmed().toUpper();
            if (type == "LINE" || type == "CIRCLE" || type == "ARC" || type == "LWPOLYLINE")
                currentType = type;
        }
        else if (!currentType.isEmpty())
        {
            entityPairs.push_back(pair);
        }
    }
    flushEntity();

    if (edgeCount <= 0)
    {
        if (errorMessage)
            *errorMessage =
                QObject::tr("DXF 文件中未找到可导入的 LINE/CIRCLE/ARC/LWPOLYLINE 实体。");
        return false;
    }

    *shape = compound;
    return true;
}
} // namespace

GeometryImportWidget::GeometryImportWidget(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessImportGeometry)
{
    // 创建 UI 组件
    pathLabel = new QLabel("几何文件路径:", this);
    pathLineEdit = new QLineEdit(this);
    browseButton = new QPushButton("浏览", this);
    importButton = new QPushButton("导入", this);

    // 设置 QLineEdit 为只读，防止手动编辑路径
    pathLineEdit->setReadOnly(true);
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    auto* pageHeaderLabel = new QLabel(QString("导入几何体"), this);
    pageHeaderLabel->setObjectName("pageHeaderLabel");
    mainLayout->addWidget(pageHeaderLabel);
    // 创建水平布局
    QHBoxLayout* layout = new QHBoxLayout(this);
    mainLayout->addWidget(pathLabel);
    mainLayout->addWidget(pathLineEdit);
    layout->addWidget(browseButton);
    layout->addWidget(importButton);
    layout->addStretch();
    mainLayout->addLayout(layout);
    mainLayout->addStretch();
    // 设置按钮大小
    browseButton->setFixedWidth(60);
    importButton->setFixedWidth(60);

    // 连接信号和槽
    connect(browseButton, &QPushButton::clicked, this,
            &GeometryImportWidget::onBrowseButtonClicked);
    connect(importButton, &QPushButton::clicked, this,
            &GeometryImportWidget::onImportButtonClicked);

    // 设置布局
    setLayout(mainLayout);
}

QJsonObject GeometryImportWidget::saveJson()
{
    QJsonObject state;
    state["path"] = getFilePath();
    return state;
}

void GeometryImportWidget::loadJson(const QJsonObject& state)
{
    if (state.contains("path"))
    {
        QString value = state["path"].toString();
        setFilePath(value);
    }
}

QString GeometryImportWidget::getFilePath() const
{
    return pathLineEdit->text();
}

void GeometryImportWidget::onImportButtonClicked()
{
    // 获取当前路径
    QString filePath = pathLineEdit->text();
    if (filePath.isEmpty())
    {
        return;
    }

    TopoDS_Shape* shape = new TopoDS_Shape();
    QString errorMessage;
    bool success = false;
    if (filePath.endsWith(".step", Qt::CaseInsensitive) ||
        filePath.endsWith(".stp", Qt::CaseInsensitive))
    {
        STEPControl_Reader readerSTEP;
        IFSelect_ReturnStatus statSTEP = readerSTEP.ReadFile(filePath.toUtf8().constData());
        if (statSTEP == IFSelect_RetDone)
        {
            IFSelect_PrintCount modeSTEP = IFSelect_ListByItem;
            readerSTEP.PrintCheckLoad(Standard_False, modeSTEP);
            readerSTEP.TransferRoots();
            *shape = readerSTEP.OneShape();
            success = !shape->IsNull();
        }
    }
    else if (filePath.endsWith(".iges", Qt::CaseInsensitive) ||
             filePath.endsWith(".igs", Qt::CaseInsensitive))
    {
        IGESControl_Reader readerIGES;
        IFSelect_ReturnStatus statIGES = readerIGES.ReadFile(filePath.toUtf8().constData());
        if (statIGES == IFSelect_RetDone)
        {
            readerIGES.TransferRoots();
            *shape = readerIGES.OneShape();
            success = !shape->IsNull();
        }
    }
    else if (filePath.endsWith(".brep", Qt::CaseInsensitive))
    {
        BRep_Builder builder;
        success =
            BRepTools::Read(*shape, filePath.toUtf8().constData(), builder) && !shape->IsNull();
    }
    else if (filePath.endsWith(".dxf", Qt::CaseInsensitive))
    {
        success = readDxfFile(filePath, shape, &errorMessage);
    }

    if (!success || shape->IsNull())
    {
        delete shape;
        QMessageBox::warning(this, tr("导入失败"),
                             errorMessage.isEmpty()
                                 ? tr("当前支持导入 STEP/STP、IGES/IGS、BREP、DXF 文件。")
                                 : errorMessage);
        return;
    }

    GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = new GeomShapeObject(m_uuid, shape, getDisplayName());
    mgr->appendGeomShapeObject(object);

    VisualWidgetService* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry", m_uuid + "_face");
    vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry", m_uuid + "_face");

    if (auto* projectManagerService =
            ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        projectManagerService->setNodeDimmed(m_uuid, false);
    }

    emit fileImported(filePath, m_uuid);
}

void GeometryImportWidget::onBrowseButtonClicked()
{
    // 打开文件选择对话框
    QString filePath =
        QFileDialog::getOpenFileName(this, "选择几何文件", QDir::homePath(),
                                     "几何文件 (*.stp *.step *.igs *.iges *.brep *.dxf);");

    // 如果用户选择了文件，更新 QLineEdit
    if (!filePath.isEmpty())
    {
        pathLineEdit->setText(filePath);
    }
}

void GeometryImportWidget::setFilePath(const QString& path)
{
    pathLineEdit->setText(path);
}
