#include "CurveSettingsWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <services/VisualWidgetService.h>

#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

CurveSettingsWidget::CurveSettingsWidget(VisualWidgetService* visualService, QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessCurve), m_visualService(visualService)
{
    if (parent)
        setParent(parent);

    auto* mainLayout = new QVBoxLayout(this);
    auto* topLayout = new QHBoxLayout();

    m_reloadBtn = new QPushButton("加载并显示TRACE曲线", this);
    m_backTo3dBtn = new QPushButton("返回三维可视化", this);
    m_statusLabel = new QLabel("等待加载 CASE01/TRACE.TXT", this);
    m_statusLabel->setWordWrap(true);

    topLayout->addWidget(m_reloadBtn);
    topLayout->addWidget(m_backTo3dBtn);
    topLayout->addWidget(m_statusLabel, 1);

    m_logEdit = new QPlainTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setPlaceholderText("TRACE.TXT 打印输出");

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_logEdit, 1);

    connect(m_reloadBtn, &QPushButton::clicked, this, &CurveSettingsWidget::reloadAndShowCurve);
    connect(m_backTo3dBtn, &QPushButton::clicked, this, &CurveSettingsWidget::switchBackTo3d);
}

void CurveSettingsWidget::beforeLoad()
{
    reloadAndShowCurve();
}

void CurveSettingsWidget::beforeUnload()
{
}

QJsonObject CurveSettingsWidget::saveJson()
{
    QJsonObject obj;
    obj["tracePath"] = m_tracePath;
    return obj;
}

void CurveSettingsWidget::loadJson(const QJsonObject& obj)
{
    m_tracePath = obj.value("tracePath").toString();
    if (!m_tracePath.isEmpty())
        reloadAndShowCurve();
}

void CurveSettingsWidget::reloadAndShowCurve()
{
    QString path = m_tracePath;
    if (path.isEmpty() || !QFile::exists(path))
        path = resolveDefaultTracePath();

    if (path.isEmpty())
    {
        m_statusLabel->setText("未找到 TRACE.TXT（已尝试：项目目录/CASE01 与当前目录/CASE01）");
        m_logEdit->clear();
        return;
    }

    QString printErr;
    if (!printTraceFile(path, &printErr))
    {
        m_statusLabel->setText(printErr);
        return;
    }

    if (!m_visualService)
    {
        m_statusLabel->setText("VisualWidgetService 不可用");
        return;
    }

    QString curveErr;
    if (!m_visualService->showTraceCurveFromFile(path, &curveErr))
    {
        m_statusLabel->setText(curveErr.isEmpty() ? QString("曲线显示失败：%1").arg(path) : curveErr);
        return;
    }

    m_tracePath = path;
    m_statusLabel->setText(QString("已显示曲线并打印 TRACE：%1").arg(path));
}

void CurveSettingsWidget::switchBackTo3d()
{
    if (m_visualService)
        m_visualService->showVisualizationView();
}

QString CurveSettingsWidget::resolveDefaultTracePath() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    QStringList candidates;
    if (!projectDir.isEmpty())
        candidates << QDir(projectDir).filePath("CASE01/TRACE.TXT");
    candidates << QDir::current().filePath("CASE01/TRACE.TXT");

    for (const QString& candidate : candidates)
    {
        if (QFile::exists(candidate))
            return candidate;
    }
    return QString();
}

bool CurveSettingsWidget::printTraceFile(const QString& filePath, QString* errMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QString msg = QString("TRACE 文件打开失败：%1").arg(filePath);
        if (errMsg)
            *errMsg = msg;
        return false;
    }

    const QString content = QString::fromLocal8Bit(file.readAll());
    m_logEdit->setPlainText(content);
    if (errMsg)
        errMsg->clear();
    return true;
}
