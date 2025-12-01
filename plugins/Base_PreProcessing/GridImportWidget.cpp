#include "GridImportWidget.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <PublicVariables.h>
#include <PublicConstants.h>

GridImportWidget::GridImportWidget(QWidget *parent) : BasePage(PublicConstants::PAGE_PREPROCESS_IMPORT_GRID)
{
	// 创建 UI 组件
	pathLabel = new QLabel("网格文件路径:", this);
	pathLineEdit = new QLineEdit(this);
	browseButton = new QPushButton("浏览", this);
	importButton = new QPushButton("导入", this);

	// 设置 QLineEdit 为只读，防止手动编辑路径
	pathLineEdit->setReadOnly(true);
	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	// 创建水平布局
	QHBoxLayout *layout = new QHBoxLayout(this);
	mainLayout->addWidget(pathLabel);
	mainLayout->addWidget(pathLineEdit);
	layout->addWidget(browseButton);
	layout->addWidget(importButton);
	layout->addStretch();
	mainLayout->addLayout(layout);
	mainLayout->addStretch();
	// 设置按钮大小（可选）
	browseButton->setIcon(QIcon(":/resources/icon/geomSearch.png"));
	importButton->setIcon(QIcon(":/resources/icon/geomImport.png"));
	browseButton->setFixedWidth(60);
	importButton->setFixedWidth(60);
	QString buttonStyle = R"(
        QPushButton {
            border: none; /* 去除边框 */
            background: transparent; /* 透明背景 */
            padding: 2px; /* 减少内边距，文字和图标更紧凑 */
            color: #333; /* 文字颜色，深灰色 */
        }
        QPushButton:hover {
            background: #e0e0e0; /* 鼠标悬停时浅灰背景 */
            border-radius: 4px; /* 圆角效果 */
        }
        QPushButton:pressed {
            background: #c0c0c0; /* 点击时更深的背景 */
        }
    )";
	browseButton->setStyleSheet(buttonStyle);
	importButton->setStyleSheet(buttonStyle);

	// 连接信号和槽
	connect(browseButton, &QPushButton::clicked, this, &GridImportWidget::onBrowseButtonClicked);
	connect(importButton, &QPushButton::clicked, this, &GridImportWidget::onImportButtonClicked);

	// 设置布局
	setLayout(mainLayout);
}

QJsonObject GridImportWidget::saveJson()
{
	QJsonObject state;
	state["path"] = getFilePath();
	return state;
}

void GridImportWidget::loadJson(const QJsonObject & state)
{
	if (state.contains("path"))
	{
		QString value = state["path"].toString();
		setFilePath(value);
	}
}

QString GridImportWidget::getFilePath() const
{
	return pathLineEdit->text();
}

void GridImportWidget::onImportButtonClicked()
{
	// 获取当前路径
	QString filePath = pathLineEdit->text();
	if (filePath.isEmpty()) 
	{
		return;
	}
	emit fileImported(filePath, m_uuid);
}


void GridImportWidget::onBrowseButtonClicked()
{
	// 打开文件选择对话框
	QString filePath = QFileDialog::getOpenFileName(
		this,
		"选择网格文件",
		QDir::homePath(),
		"网格文件 (*.nas *.cgns *.stl *.vtk *.inp *.msh);"
	);

	// 如果用户选择了文件，更新 QLineEdit
	if (!filePath.isEmpty()) 
	{
		pathLineEdit->setText(filePath);
	}
}

void GridImportWidget::setFilePath(const QString & path)
{
	pathLineEdit->setText(path);
}
