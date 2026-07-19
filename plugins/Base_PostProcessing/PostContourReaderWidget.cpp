#include "PostContourReaderWidget.h"

#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>

#include <vtkActor.h>
#include <vtkActor2D.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkLabeledDataMapper.h>
#include <vtkPoints.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkStringArray.h>
#include <vtkTextProperty.h>
#include <vtkUnsignedCharArray.h>

#include <QCheckBox>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTextStream>

#include <algorithm>

#include "PostVisualUtils.h"
#include "ui_PostContourReaderWidget.h"

namespace
{
QString simplifiedUpper(QString text)
{
    return text.trimmed().simplified().toUpper();
}
} // namespace

PostContourReaderWidget::PostContourReaderWidget(QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessContourReader), ui(new Ui::PostContourReaderWidget)
{
    ui->setupUi(this);
    if (parent)
        setParent(parent);

    m_linePolyData = vtkSmartPointer<vtkPolyData>::New();
    m_lineMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_lineActor = vtkSmartPointer<vtkActor>::New();

    connect(ui->browseButton, &QPushButton::clicked, this, &PostContourReaderWidget::onBrowseClicked);
    connect(ui->loadButton, &QPushButton::clicked, this, &PostContourReaderWidget::onLoadClicked);
    connect(ui->clearButton, &QPushButton::clicked, this, &PostContourReaderWidget::onClearClicked);
    connect(ui->showTextCheckBox, &QCheckBox::toggled, this,
            &PostContourReaderWidget::onTextVisibleChanged);

    connect(ui->lineWidthDoubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double value) {
                if (m_lineActor)
                    m_lineActor->GetProperty()->SetLineWidth(value);
                if (auto* rw = m_visualService ? m_visualService->renderWindow() : nullptr)
                    rw->Render();
            });

    connect(ui->textFontSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) {
                if (ui->showTextCheckBox->isChecked() && !m_model.texts.isEmpty())
                    displayModel(false);
            });

    ui->showTextCheckBox->setChecked(false);
    ui->textFontSizeSpinBox->setEnabled(false);

    setStatusMessage(QStringLiteral("未选择轮廓图数据文件。"));
}

PostContourReaderWidget::~PostContourReaderWidget()
{
    beforeUnload();
    delete ui;
}

void PostContourReaderWidget::beforeLoad()
{
    ensureVisualService();

    if (m_visualService)
        showPostProcessingOnly(m_visualService, false);

    if (!m_model.lines.isEmpty())
        displayModel(false);
}

void PostContourReaderWidget::beforeUnload()
{
    removeActorsFromRenderer();
}

bool PostContourReaderWidget::ensureVisualService()
{
    if (!m_visualService)
        m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();

    if (!m_visualService)
    {
        setStatusMessage(QStringLiteral("未找到后处理显示服务，无法显示洞室变形轮廓图。"));
        return false;
    }

    return true;
}

void PostContourReaderWidget::onBrowseClicked()
{
    const QString startDir = ui->filePathLineEdit->text().isEmpty()
                                 ? QDir::homePath()
                                 : QFileInfo(ui->filePathLineEdit->text()).absolutePath();

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择洞室变形轮廓图数据文件"),
        startDir,
        QStringLiteral("AutoCAD Script (*.scr *.SCR);;所有文件 (*.*)"));

    if (filePath.isEmpty())
        return;

    ui->filePathLineEdit->setText(QDir::toNativeSeparators(filePath));
}

void PostContourReaderWidget::onLoadClicked()
{
    const QString filePath = QDir::fromNativeSeparators(ui->filePathLineEdit->text().trimmed());

    if (filePath.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选择轮廓图数据文件。"));
        return;
    }

    ScrModel parsed;
    QString error;

    if (!parseScrFile(filePath, parsed, &error))
    {
        QMessageBox::warning(this, QStringLiteral("显示失败"), error);
        setStatusMessage(error);
        return;
    }

    m_model = parsed;
    displayModel(true);

    setStatusMessage(QStringLiteral("已显示洞室变形轮廓图。"));
}

void PostContourReaderWidget::onClearClicked()
{
    removeActorsFromRenderer();
    m_model = ScrModel();
    m_linePolyData = vtkSmartPointer<vtkPolyData>::New();
    setStatusMessage(QStringLiteral("已清空洞室变形轮廓图。"));
}

void PostContourReaderWidget::onTextVisibleChanged(bool checked)
{
    ui->textFontSizeSpinBox->setEnabled(checked);

    if (m_model.lines.isEmpty() && m_model.texts.isEmpty())
        return;

    displayModel(false);
}

bool PostContourReaderWidget::parseScrFile(const QString& filePath,
                                           ScrModel& model,
                                           QString* errorMessage) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法打开轮廓图数据文件：%1").arg(filePath);
        return false;
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif

    QStringList lines;
    while (!in.atEnd())
        lines << in.readLine();

    model = ScrModel();
    model.filePath = QFileInfo(filePath).absoluteFilePath();
    model.totalLines = lines.size();

    QMap<QString, ScrPoint> points;
    QString currentLayer = QStringLiteral("0");
    int currentColor = 7;

    const QRegularExpression pointRegex(
        QStringLiteral(R"(^\s*\(\s*setq\s+(\w+)\s+'\(\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[Ee][+-]?\d+)?)\s+([+-]?(?:\d+\.?\d*|\.\d+)(?:[Ee][+-]?\d+)?)\s+([+-]?(?:\d+\.?\d*|\.\d+)(?:[Ee][+-]?\d+)?)\s*\)\s*\)\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpression lineRegex(
        QStringLiteral(R"(^\s*LINE\s+!(\w+)\s+!(\w+).*$)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpression textRegex(
        QStringLiteral(R"(^\s*TEXT\s+!(\w+).*$)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpression layerSetRegex(
        QStringLiteral(R"(^\s*LAYER\s+S\s+([^\s]+).*$)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpression colorRegex(
        QStringLiteral(R"(^\s*COLOR\s+([+-]?\d+).*$)"),
        QRegularExpression::CaseInsensitiveOption);

    for (int i = 0; i < lines.size(); ++i)
    {
        const QString raw = lines.at(i);
        const QString compactUpper = simplifiedUpper(raw);

        if (compactUpper.isEmpty())
            continue;

        QRegularExpressionMatch match = layerSetRegex.match(raw);
        if (match.hasMatch())
        {
            currentLayer = match.captured(1).trimmed();
            continue;
        }

        match = colorRegex.match(raw);
        if (match.hasMatch())
        {
            bool ok = false;
            const int value = match.captured(1).toInt(&ok);
            if (ok)
                currentColor = value;
            continue;
        }

        match = pointRegex.match(raw);
        if (match.hasMatch())
        {
            ScrPoint p;
            p.x = match.captured(2).toDouble();
            p.y = match.captured(3).toDouble();
            p.z = match.captured(4).toDouble();
            points.insert(match.captured(1).trimmed().toUpper(), p);
            ++model.pointDefinitionCount;
            includePointInBounds(model, p);
            continue;
        }

        match = lineRegex.match(raw);
        if (match.hasMatch())
        {
            const QString p1Name = match.captured(1).trimmed().toUpper();
            const QString p2Name = match.captured(2).trimmed().toUpper();

            if (!points.contains(p1Name) || !points.contains(p2Name))
            {
                ++model.skippedLineCount;
                continue;
            }

            ScrLine item;
            item.p1 = points.value(p1Name);
            item.p2 = points.value(p2Name);
            item.layer = currentLayer;
            item.colorIndex = currentColor;
            item.sourceLine = i + 1;

            model.lines.push_back(item);
            model.layerLineCounts[item.layer] += 1;
            continue;
        }

        match = textRegex.match(raw);
        if (match.hasMatch())
        {
            const QString pName = match.captured(1).trimmed().toUpper();
            if (!points.contains(pName) || i + 3 >= lines.size())
            {
                ++model.skippedLineCount;
                continue;
            }

            ScrText text;
            text.p = points.value(pName);
            text.layer = currentLayer;
            text.colorIndex = currentColor;
            text.sourceLine = i + 1;
            text.height = lines.at(i + 1).trimmed().toDouble();
            text.angle = lines.at(i + 2).trimmed().toDouble();
            text.text = lines.at(i + 3).trimmed();

            if (!text.text.isEmpty())
            {
                model.texts.push_back(text);
                model.layerTextCounts[text.layer] += 1;
            }

            i += 3;
            continue;
        }
    }

    if (model.lines.isEmpty() && model.texts.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("轮廓图数据文件中没有解析到线段或文字标注：%1").arg(filePath);
        return false;
    }

    return true;
}

void PostContourReaderWidget::displayModel(bool resetCamera)
{
    if (!ensureVisualService())
        return;

    removeActorsFromRenderer();
    buildLineActor();

    if (ui->showTextCheckBox->isChecked())
        buildTextActors();
    else
        m_textActors.clear();

    auto* ren = m_visualService->renderer();
    if (!ren)
    {
        setStatusMessage(QStringLiteral("后处理绘图功能尚未就绪，无法显示洞室变形轮廓图。"));
        return;
    }

    if (m_lineActor && m_model.lines.size() > 0)
    {
        ren->AddActor(m_lineActor);
        m_lineActorAttached = true;
    }

    for (auto& actor : m_textActors)
        ren->AddActor2D(actor);

    if (resetCamera)
        ren->ResetCamera();

    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

void PostContourReaderWidget::buildLineActor()
{
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetName("ScrAciColor");
    colors->SetNumberOfComponents(3);

    for (const ScrLine& line : m_model.lines)
    {
        const vtkIdType id1 = points->InsertNextPoint(line.p1.x, line.p1.y, line.p1.z);
        const vtkIdType id2 = points->InsertNextPoint(line.p2.x, line.p2.y, line.p2.z);

        vtkIdType ids[2] = {id1, id2};
        cells->InsertNextCell(2, ids);

        const QColor c = aciColor(line.colorIndex);
        unsigned char rgb[3] = {static_cast<unsigned char>(c.red()),
                                static_cast<unsigned char>(c.green()),
                                static_cast<unsigned char>(c.blue())};
        colors->InsertNextTypedTuple(rgb);
    }

    m_linePolyData = vtkSmartPointer<vtkPolyData>::New();
    m_linePolyData->SetPoints(points);
    m_linePolyData->SetLines(cells);
    m_linePolyData->GetCellData()->SetScalars(colors);

    m_lineMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_lineMapper->SetInputData(m_linePolyData);
    m_lineMapper->SetScalarModeToUseCellData();
    m_lineMapper->ScalarVisibilityOn();

    m_lineActor = vtkSmartPointer<vtkActor>::New();
    m_lineActor->SetMapper(m_lineMapper);
    m_lineActor->GetProperty()->SetLineWidth(ui->lineWidthDoubleSpinBox->value());
    m_lineActor->GetProperty()->SetOpacity(1.0);
}

void PostContourReaderWidget::buildTextActors()
{
    m_textActors.clear();

    if (m_model.texts.isEmpty())
        return;

    QMap<int, QVector<ScrText>> grouped;
    for (const ScrText& text : m_model.texts)
        grouped[text.colorIndex].push_back(text);

    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it)
    {
        auto points = vtkSmartPointer<vtkPoints>::New();
        auto labels = vtkSmartPointer<vtkStringArray>::New();
        labels->SetName("ScrText");

        for (const ScrText& text : it.value())
        {
            points->InsertNextPoint(text.p.x, text.p.y, text.p.z);
            labels->InsertNextValue(text.text.toUtf8().constData());
        }

        auto polyData = vtkSmartPointer<vtkPolyData>::New();
        polyData->SetPoints(points);
        polyData->GetPointData()->AddArray(labels);

        auto mapper = vtkSmartPointer<vtkLabeledDataMapper>::New();
        mapper->SetInputData(polyData);
        mapper->SetLabelModeToLabelFieldData();
        mapper->SetFieldDataName("ScrText");
        mapper->GetLabelTextProperty()->SetFontSize(ui->textFontSizeSpinBox->value());

        const QColor c = aciColor(it.key());
        mapper->GetLabelTextProperty()->SetColor(c.redF(), c.greenF(), c.blueF());

        auto actor = vtkSmartPointer<vtkActor2D>::New();
        actor->SetMapper(mapper);
        m_textActors.push_back(actor);
    }
}

void PostContourReaderWidget::removeActorsFromRenderer()
{
    if (!m_visualService)
        return;

    auto* ren = m_visualService->renderer();
    if (!ren)
        return;

    if (m_lineActor)
        ren->RemoveActor(m_lineActor);

    for (auto& actor : m_textActors)
        ren->RemoveActor2D(actor);

    m_lineActorAttached = false;

    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

void PostContourReaderWidget::setStatusMessage(const QString& message)
{
    if (!ui)
        return;

    qInfo() << "PostContourReaderWidget:" << message;
}

QColor PostContourReaderWidget::aciColor(int colorIndex)
{
    switch (colorIndex)
    {
    case 1:
        return QColor(220, 40, 40);      // red
    case 2:
        return QColor(210, 160, 0);      // yellow, slightly darkened for light viewport
    case 3:
        return QColor(40, 160, 40);      // green
    case 4:
        return QColor(0, 150, 170);      // cyan, slightly darkened for light viewport
    case 5:
        return QColor(40, 80, 220);      // blue
    case 6:
        return QColor(190, 60, 190);     // magenta
    case 7:
        return QColor(30, 30, 30);       // black/white in CAD, use dark for white viewport
    case 8:
        return QColor(130, 130, 130);    // gray
    case 9:
        return QColor(180, 180, 180);    // light gray
    default:
        return QColor(45, 45, 45);
    }
}

void PostContourReaderWidget::includePointInBounds(ScrModel& model, const ScrPoint& p)
{
    if (!model.hasBounds)
    {
        model.minX = model.maxX = p.x;
        model.minY = model.maxY = p.y;
        model.minZ = model.maxZ = p.z;
        model.hasBounds = true;
        return;
    }

    model.minX = std::min(model.minX, p.x);
    model.maxX = std::max(model.maxX, p.x);
    model.minY = std::min(model.minY, p.y);
    model.maxY = std::max(model.maxY, p.y);
    model.minZ = std::min(model.minZ, p.z);
    model.maxZ = std::max(model.maxZ, p.z);
}
