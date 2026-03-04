#include "StressStrainWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QTextStream>

#include "ui_StressStrainWidget.h"

StressStrainWidget::StressStrainWidget(QWidget* parent)
    : BasePage("StressStrainAnalysis"), ui(new Ui::StressStrainWidget) {
  ui->setupUi(this);
  connect(ui->btnCompute, &QPushButton::clicked, this,
          &StressStrainWidget::onComputeClicked);
}

StressStrainWidget::~StressStrainWidget() { delete ui; }

void StressStrainWidget::beforeLoad() {}

void StressStrainWidget::beforeUnload() {}

bool StressStrainWidget::exportIfillDatFile() const {
  const QString projectDir = PublicVariables::instance()
                                 .get(PublicConstants::kProjectDirectoryKey)
                                 .toString();
  const QString projectDirAbs = QDir(projectDir).absolutePath();

  QDir wd(projectDirAbs);
  if (!wd.exists("PREP") && !wd.mkpath("PREP")) return false;

  auto ensureTrailingSep = [](const QString& path) {
    QString p = QDir::toNativeSeparators(path);
    if (!p.endsWith(QDir::separator())) p += QDir::separator();
    return p;
  };

  const QString prepDirAbs = ensureTrailingSep(wd.filePath("PREP"));
  const QString caseDirAbs = ensureTrailingSep(wd.filePath("CASE01FILL01"));

  QFile file(wd.filePath("IFILL.DAT"));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

  QTextStream os(&file);
  os.setRealNumberNotation(QTextStream::FixedNotation);
  os.setRealNumberPrecision(6);

  os << "'" << prepDirAbs << "'       1\n";
  os << "'FA_DAMUP.DAT'       2\n";
  os << "'FA_DAMDW.DAT'       3\n";
  os << "'FA_DAMBASE.DAT'       4\n";
  os << "'FA_DAMMID.DAT'       5\n";
  os << "'MA_ROCK.DAT'       6\n";
  os << "'MA_DAMUP.DAT'       7\n";
  os << "'MA_DAMDW.DAT'       8\n";
  os << "'MA_CORE.DAT'       9\n";
  os << "'4'      10\n";
  os << "'MA_PLATE.DAT'      11\n";
  os << "'JDTRACE.DAT'      12\n";
  os << "'KETRACE.DAT'      13\n";
  os << "'TDTRACE.DAT'      14\n";
  os << "'0'      15\n";
  os << "'0'      16\n";
  os << "'0.05'      17\n";
  os << "'0.0'      18\n";
  os << "'1'      19\n";
  os << "'4   10'      20\n";
  os << "'     0.000'      21\n";
  os << "'1.962'      22\n";
  os << "'1.308'      23\n";
  os << "'0.1'      24\n";
  os << "'0.2'      25\n";
  os << "'2.0'      26\n";
  os << "'0.9'      27\n";
  os << "'7.0'      28\n";
  os << "'0'      29\n";
  os << "'10'      30\n";
  os << "     0.000000       0.000000       0.000000        X1      X2      "
        "X3\n";
  os << "     0.000000       0.000000       0.000000     ALFA1   ALFA2   "
        "ALFA3\n";
  os << "   1     3       0.000000       0.000000       0.000000       "
        "0.000000  IX IPGEN XA  YA  XB YB\n";
  os << "   1     0    IFROCK   IFISO\n";
  os << "   1   MCASE\n";
  os << "   1     1\n";
  os << "   1    NCASE\n";
  os << " ----------------------------------------------\n";
  os << "    1    ICASE--------------------------------------\n";
  os << "   0     0     1     IFIIWL_SEEP   IFIIWL_BODY  IFIIWL_FACE\n";
  os << "   1     0     NUM_WET        IFWET\n";
  os << "   1     1     1    M1_DRY    M1WET   II\n";
  os << "   1     0     NUM_DROP       IFDROP\n";
  os << "  3200.000    3160.000       1  HW_STA    HW_END  II\n";
  os << "   0     NCHANGE\n";
  os << "   1  NCIRCLE\n";
  os << "JPACK_STA JPACK_END\tTT_PACK         HWJ_STA\t      HWJ_END\t TT_FILL "
        "IF_PACK IF_PLIU IF_FILL  IF_FLIU\tCAHEAD CABODY\n";
  os << "   2     9           0.0000         0.0000        28.0000         "
        "0.0000       1     0     1     0  'NONE'  'NONE'     1\n";
  os << "'" << caseDirAbs << "'     DIRDYNA\n";
  os << "   2   NUM_DYNA\n";
  os << "   3    23     1    MACHAN    MACHAN  II DYNA\n";
  os << "  10    30     2    MACHAN    MACHAN  II DYNA\n";
  os << "   0     1     0   IFMODE   IFWAVE     IFSETT\n";
  os << "  0.020000    0.000000   28.000000    0.000000    DT TSTA TEND "
        "WOMI_1\n";
  os << "'WAVE-AX.DAT'  'WAVE-AY.DAT'  'WAVE-AZ.DAT'   AX  AY  AZ\n";

  return true;
}

QJsonObject StressStrainWidget::saveJson() { return QJsonObject(); }

void StressStrainWidget::loadJson(const QJsonObject&) {}

void StressStrainWidget::onComputeClicked() { emit computeRequested(); }
