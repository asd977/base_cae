#include "VectorSettingWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkArrowSource.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkFloatArray.h>
#include <vtkGlyph3D.h>
#include <vtkLookupTable.h>
#include <vtkMaskPoints.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkTecplotReader.h>
#include <vtkUnstructuredGrid.h>

#include <QDebug>
#include <QDir>

#include "PostVisualUtils.h"
#include "ui_VectorSettingWidget.h"

namespace {
const QStringList kResultDirectoriesCommon = {"SEEPCASE01"};
const QStringList kPreferredResultFilesCommon = {"TEC_HP.DAT", "TEC_VELO.DAT"};
}  // namespace

#include <stdio.h>
#include <vtkXMLUnstructuredGridReader.h>

#include <cmath>
#include <limits>

VectorSettingWidget::VectorSettingWidget(
    VisualWidgetService* postProcessingWidget, QWidget* parent)
    : ui(new Ui::VectorSettingWidget),
      BasePage(PublicConstants::kPagePostProcessVector) {
  ui->setupUi(this);

  // 槽函数连接
  connect(ui->confirmPushButton, &QPushButton::clicked, this,
          &VectorSettingWidget::on_confirmPushButton_clicked);
  connect(ui->scaleSlider, &QSlider::valueChanged, this,
          &VectorSettingWidget::on_scaleSlider_valueChanged);

  // 文本框校验：抽样间隔必须为正整数
  ui->maskSzieTextEdit->setValidator(new QIntValidator(1, 1000000, this));
  if (ui->maskSzieTextEdit->text().isEmpty())
    ui->maskSzieTextEdit->setText(QString("1"));

  // ——改这里：确保有合理范围，并把默认值设置为“中点”——
  // 如果 .ui 里没设范围，给个兜底 1~300（对应 0.01~3.00）
  if (ui->scaleSlider->minimum() >= ui->scaleSlider->maximum()) {
    ui->scaleSlider->setRange(1, 300);
  }
  {
    QSignalBlocker block(ui->scaleSlider);  // 防止构造期触发 valueChanged
    const int min = ui->scaleSlider->minimum();
    const int max = ui->scaleSlider->maximum();
    const int mid = min + (max - min) / 2;  // 中点
    ui->scaleSlider->setValue(mid);
  }
  m_scaleFactor =
      ui->scaleSlider->value() / 100.0;  // 初始放大系数=中点(比如1.50)

  m_visualService = postProcessingWidget;
}
VectorSettingWidget::~VectorSettingWidget() { delete ui; }

double VectorSettingWidget::computeAutoBaseScale(vtkUnstructuredGrid* grid) {
  if (!grid) return 1.0;
  double b[6];
  grid->GetBounds(b);
  const double dx = b[1] - b[0];
  const double dy = b[3] - b[2];
  const double dz = b[5] - b[4];
  const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (diag <= 0.0 || !std::isfinite(diag)) return 1.0;
  return diag * 0.1;  // 3% 的对角线：可按需要改 0.02~0.05
}

void VectorSettingWidget::beforeLoad() {
  showPostProcessingOnly(m_visualService, false);
  const QString proj = PublicVariables::instance()
                           .get(PublicConstants::kProjectDirectoryKey)
                           .toString();
  m_dataPath = QDir(proj).filePath(kResultDirectoriesCommon.first());

  if (ui->stepComboBox->count() == 0) {
    // 首次进入：加载文件列表并默认读第一个
    loadFileName(m_dataPath);
  } else {
    // 非首次：恢复之前的显示（如果有箭头 actor，就挂回去）
    if (m_visualService && m_vectorActor) {
      addActorByService(m_visualService, m_vectorActor);
      requestRender();
    }
  }
}

void VectorSettingWidget::beforeUnload() {
  // 页面被切换：仅隐藏当前向量箭头，保留对象以便回来后快速恢复
  if (m_visualService && m_vectorActor) {
    removeActorByService(m_visualService, m_vectorActor);
    requestRender();
  }
  // 不要置空 m_vectorActor/m_glyph/m_grid —— 保持状态以用于恢复
}

void VectorSettingWidget::loadFileName(QString dataPath) {
  ui->stepComboBox->blockSignals(true);
  ui->stepComboBox->clear();

  const QDir directory(dataPath);
  if (!directory.exists()) {
    qWarning() << "Directory not found:" << dataPath;
    ui->stepComboBox->blockSignals(false);
    return;
  }

  for (const QString& name : kPreferredResultFilesCommon) {
    const QString fullPath = directory.filePath(name);
    if (!QFileInfo::exists(fullPath)) continue;
    const QString itemName = QFileInfo(fullPath).completeBaseName();
    ui->stepComboBox->addItem(itemName, fullPath);
  }

  ui->stepComboBox->blockSignals(false);

  if (ui->stepComboBox->count() > 0) {
    ui->stepComboBox->setCurrentIndex(0);
    on_stepComboBox_currentIndexChanged(0);
  } else {
    qWarning() << "No configured result files found in" << dataPath;
  }
}

void VectorSettingWidget::on_stepComboBox_currentIndexChanged(int index) {
  if (index < 0) return;

  const QString filePath = ui->stepComboBox->currentData().toString();
  if (filePath.isEmpty()) {
    qWarning() << "[Vector] No data file mapped for selection.";
    return;
  }
  setFileName(filePath);

  // 切文件时，先把旧的箭头从场景移除，避免看到旧结果
  if (m_visualService && m_vectorActor) {
    removeActorByService(m_visualService, m_vectorActor);
    // 不置空指针，等用户点“确认”后再重建新 glyph/actor
  }

  // 重新加载新数据（只读，不立即建箭头；等用户点“确认”）
  ensureDataLoaded();

  // 可选：如果希望切换后自动重建箭头，放开下面一行
  // rebuildPipeline();
}

void VectorSettingWidget::setFileName(const QString& file) {
  if (m_fileName == file) return;
  m_fileName = file;
  m_grid = nullptr;  // 强制下次重建
}

bool VectorSettingWidget::ensureDataLoaded() {
  if (m_grid) return true;
  if (m_fileName.isEmpty()) return false;

  QFileInfo info(m_fileName);
  QString ext = info.suffix().toLower();

  vtkSmartPointer<vtkUnstructuredGrid> grid;

  if (ext == "txt") {
    // ---- 原来的 Tecplot ----
    auto reader = vtkSmartPointer<vtkTecplotReader>::New();
    reader->SetFileName(m_fileName.toUtf8().constData());
    reader->Update();

    auto multi = vtkMultiBlockDataSet::SafeDownCast(reader->GetOutput());
    if (!multi) return false;

    auto appender = vtkSmartPointer<vtkAppendFilter>::New();
    for (int i = 0; i < multi->GetNumberOfBlocks(); ++i) {
      if (auto* block = vtkUnstructuredGrid::SafeDownCast(multi->GetBlock(i))) {
        appender->AddInputData(block);
      }
    }
    appender->Update();

    grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->ShallowCopy(appender->GetOutput());
  } else if (ext == "vtu") {
    // ---- 新增的 vtu ----
    grid = readVtu(m_fileName);
  } else {
    qWarning() << "[Vector] Unsupported file:" << m_fileName;
    return false;
  }

  if (!grid || grid->GetNumberOfPoints() == 0) return false;

  m_grid = grid;
  populateComponentCombos();
  return true;
}

void VectorSettingWidget::populateComponentCombos() {
  if (!m_grid) return;

  QStringList names;
  vtkPointData* pd = m_grid->GetPointData();
  const int nArr = pd->GetNumberOfArrays();
  for (int i = 0; i < nArr; ++i) {
    vtkDataArray* arr = pd->GetArray(i);
    if (!arr || !arr->GetName()) continue;
    if (arr->GetNumberOfComponents() == 1 ||
        arr->GetNumberOfComponents() == 3) {
      names << QString::fromUtf8(arr->GetName());
    }
  }
  names.removeDuplicates();
  names.sort();

  auto setCombo = [&](QComboBox* cb) {
    cb->clear();
    cb->addItems(names);
  };
  setCombo(ui->xComboBox);
  setCombo(ui->yComboBox);
  setCombo(ui->zComboBox);

  auto trySelect = [&](QComboBox* cb, const char* want) {
    int idx = cb->findText(QString::fromUtf8(want));
    if (idx >= 0) cb->setCurrentIndex(idx);
  };
  trySelect(ui->xComboBox, "SX");
  trySelect(ui->yComboBox, "SY");
  trySelect(ui->zComboBox, "SZ");

  if (ui->xComboBox->currentIndex() < 0 && ui->xComboBox->count() > 0)
    ui->xComboBox->setCurrentIndex(0);
  if (ui->yComboBox->currentIndex() < 0 && ui->yComboBox->count() > 1)
    ui->yComboBox->setCurrentIndex(1);
  if (ui->zComboBox->currentIndex() < 0 && ui->zComboBox->count() > 2)
    ui->zComboBox->setCurrentIndex(2);
}

vtkDataArray* VectorSettingWidget::arrayByName(vtkUnstructuredGrid* grid,
                                               const QString& name) {
  if (!grid || name.isEmpty()) return nullptr;
  return grid->GetPointData()->GetArray(name.toUtf8().constData());
}

bool VectorSettingWidget::buildVectorArraysFromSelections(
    vtkUnstructuredGrid* grid, double& magMin, double& magMax) {
  if (!grid) return false;

  const QString xName = ui->xComboBox->currentText();
  const QString yName = ui->yComboBox->currentText();
  const QString zName = ui->zComboBox->currentText();

  vtkDataArray* arrX = arrayByName(grid, xName);
  vtkDataArray* arrY = arrayByName(grid, yName);
  vtkDataArray* arrZ = arrayByName(grid, zName);

  if (!arrX || !arrY || !arrZ) {
    qWarning() << "[Vector] 所选分量数组不存在。";
    return false;
  }

  const vtkIdType nPts = grid->GetNumberOfPoints();
  if (arrX->GetNumberOfTuples() < nPts || arrY->GetNumberOfTuples() < nPts ||
      arrZ->GetNumberOfTuples() < nPts) {
    qWarning() << "[Vector] 分量数组长度与点数不一致。";
    return false;
  }

  const bool packed3 =
      (xName == yName && xName == zName && arrX->GetNumberOfComponents() == 3);

  auto vec = vtkSmartPointer<vtkFloatArray>::New();
  vec->SetName("Vector");
  vec->SetNumberOfComponents(3);
  vec->SetNumberOfTuples(nPts);

  auto mag = vtkSmartPointer<vtkFloatArray>::New();
  mag->SetName("VectorMag");
  mag->SetNumberOfComponents(1);
  mag->SetNumberOfTuples(nPts);

  magMin = std::numeric_limits<double>::infinity();
  magMax = -std::numeric_limits<double>::infinity();

  if (packed3) {
    double t[3];
    for (vtkIdType i = 0; i < nPts; ++i) {
      arrX->GetTuple(i, t);
      vec->SetTuple3(i, static_cast<float>(t[0]), static_cast<float>(t[1]),
                     static_cast<float>(t[2]));
      const double m = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
      mag->SetTuple1(i, static_cast<float>(m));
      if (m < magMin) magMin = m;
      if (m > magMax) magMax = m;
    }
  } else {
    for (vtkIdType i = 0; i < nPts; ++i) {
      const double x = arrX->GetComponent(i, 0);
      const double y = arrY->GetComponent(i, 0);
      const double z = arrZ->GetComponent(i, 0);
      vec->SetTuple3(i, static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(z));
      const double m = std::sqrt(x * x + y * y + z * z);
      mag->SetTuple1(i, static_cast<float>(m));
      if (m < magMin) magMin = m;
      if (m > magMax) magMax = m;
    }
  }

  auto* pd = grid->GetPointData();
  pd->SetVectors(vec);                // 朝向/长度基于 Vector
  pd->AddArray(mag);                  // 上色使用 VectorMag
  pd->SetActiveScalars("VectorMag");  // 不是必须，但有助于调试/检查

  return true;
}

void VectorSettingWidget::rebuildPipeline() {
  if (!ensureDataLoaded()) return;

  // 抽样间隔
  m_maskOnRatio = ui->maskSzieTextEdit->text().toInt();
  if (m_maskOnRatio < 1) m_maskOnRatio = 1;

  // 生成 Vector / VectorMag，并得到其范围
  double magMin = 0.0, magMax = 1.0;
  if (!buildVectorArraysFromSelections(m_grid, magMin, magMax)) return;

  auto nearlyEqual = [](double a, double b) {
    const double abs = 1e-15, rel = 1e-12;
    return std::fabs(a - b) <=
           std::max(abs, rel * std::max(std::fabs(a), std::fabs(b)));
  };

  // 移除旧 actor
  if (m_vectorActor) {
    if (m_visualService) removeActorByService(m_visualService, m_vectorActor);
    m_vectorActor = nullptr;
  }

  // 抽样
  auto mask = vtkSmartPointer<vtkMaskPoints>::New();
  mask->SetInputData(m_grid);
  mask->SetOnRatio(m_maskOnRatio);  // 每隔 N 个取 1 个
  mask->RandomModeOff();
  mask->Update();

  // 箭头源
  auto arrow = vtkSmartPointer<vtkArrowSource>::New();
  arrow->SetTipLength(0.35);    // 默认 0.35（可调）
  arrow->SetTipRadius(0.08);    // 更粗的锥头
  arrow->SetShaftRadius(0.02);  // 更粗的箭杆

  // Glyph：向量用于朝向；上色用 VectorMag
  m_glyph = vtkSmartPointer<vtkGlyph3D>::New();
#if VTK_MAJOR_VERSION >= 9
  m_glyph->SetInputConnection(mask->GetOutputPort());
  m_glyph->SetSourceConnection(arrow->GetOutputPort());
#else
  m_glyph->SetInputConnection(0, mask->GetOutputPort());
  m_glyph->SetSourceConnection(0, arrow->GetOutputPort());
#endif
  m_glyph->OrientOn();
  m_glyph->SetVectorModeToUseVector();
  m_glyph->SetScaleModeToDataScalingOff();  // 等长
  m_glyph->SetScaleFactor(m_scaleFactor);   // 使用滑条设置
  // 指定向量与标量数组
  m_glyph->SetInputArrayToProcess(
      1, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, "Vector");  // vectors
  m_glyph->SetInputArrayToProcess(0, 0, 0,
                                  vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                  "VectorMag");  // scalars

  m_glyph->SetScaleModeToDataScalingOff();  // 等长
  {
    const double base = computeAutoBaseScale(m_grid);
    m_glyph->SetScaleFactor(base * m_scaleFactor);  // 自适应尺度 × 滑条倍率
  }
  m_glyph->Update();

  // Mapper：显式按 VectorMag 上色
  auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  mapper->SetInputConnection(m_glyph->GetOutputPort());
  mapper->ScalarVisibilityOn();
  mapper->SetScalarModeToUsePointFieldData();
  mapper->SelectColorArray("VectorMag");
  mapper->SetColorModeToMapScalars();

  // 同步色标 LUT（与 VectorMag 一致）
  if (!(magMax > magMin) || nearlyEqual(magMax, magMin)) {
    // 常量场兜底，使 LUT 有一个有效范围
    magMin = 0.0;
    magMax = 1.0;
  }

  if (vtkSmartPointer<vtkScalarBarActor> scalarBar =
          m_visualService ? m_visualService->scalarBarActor() : nullptr) {
    vtkLookupTable* lut =
        vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable());
    if (!lut) {
      auto l = vtkSmartPointer<vtkLookupTable>::New();
      l->Build();
      lut = l;
      scalarBar->SetLookupTable(lut);
    }
    lut->SetRange(magMin, magMax);
    mapper->SetLookupTable(lut);
    mapper->UseLookupTableScalarRangeOn();

    scalarBar->SetTitle("‖Vector‖");
    scalarBar->Modified();
  } else {
    mapper->SetScalarRange(magMin, magMax);
  }

  // Actor
  m_vectorActor = vtkSmartPointer<vtkActor>::New();
  m_vectorActor->SetMapper(mapper);
  m_vectorActor->GetProperty()->SetRepresentationToSurface();

  if (m_visualService) addActorByService(m_visualService, m_vectorActor);

  requestRender();
}

void VectorSettingWidget::requestRender() const {
  if (!m_visualService) return;
  m_visualService->renderWindow()->Render();
}

void VectorSettingWidget::on_confirmPushButton_clicked() { rebuildPipeline(); }

void VectorSettingWidget::on_scaleSlider_valueChanged(int value) {
  m_scaleFactor = value / 100.0;
  if (m_glyph) {
    const double base = computeAutoBaseScale(m_grid);
    m_glyph->SetScaleFactor(base * m_scaleFactor);
    m_glyph->Update();
    requestRender();
  }
}

vtkSmartPointer<vtkUnstructuredGrid> VectorSettingWidget::readVtu(
    const QString& fileName) {
  auto reader = vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();
  reader->SetFileName(fileName.toUtf8().constData());
  reader->Update();

  vtkUnstructuredGrid* ug = reader->GetOutput();
  if (!ug || ug->GetNumberOfPoints() == 0) {
    qWarning() << "[Vector] Failed to read VTU file:" << fileName;
    return nullptr;
  }

  auto result = vtkSmartPointer<vtkUnstructuredGrid>::New();
  result->ShallowCopy(ug);
  return result;
}
