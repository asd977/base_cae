#include "GeomManagerInstance.h"
#include "GeometryImportWidget.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <PublicVariables.h>
#include <PublicConstants.h>
#include <services/VisualWidgetService.h>
#include <ServiceRegistry.h>
#include <STEPControl_Reader.hxx>
#include <TCollection_ExtendedString.hxx>

#include <STEPCAFControl_Writer.hxx>

GeometryImportWidget::GeometryImportWidget(QWidget *parent) : BasePage(PublicConstants::kPagePreprocessImportGeometry)
{
	// 创建 UI 组件
	pathLabel = new QLabel("几何文件路径:", this);
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

	// 连接信号和槽
	connect(browseButton, &QPushButton::clicked, this, &GeometryImportWidget::onBrowseButtonClicked);
	connect(importButton, &QPushButton::clicked, this, &GeometryImportWidget::onImportButtonClicked);

	// 设置布局
	setLayout(mainLayout);
}

QJsonObject GeometryImportWidget::saveJson()
{
	QJsonObject state;
	state["path"] = getFilePath();
	return state;
}

void GeometryImportWidget::loadJson(const QJsonObject & state)
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
    if (filePath.endsWith(".step", Qt::CaseInsensitive) || filePath.endsWith(".stp", Qt::CaseInsensitive))
    {

        STEPControl_Reader readerSTEP;
        IFSelect_ReturnStatus statSTEP = readerSTEP.ReadFile(filePath.toUtf8().constData());
        if (statSTEP != IFSelect_RetDone) {
            return;
        }
        IFSelect_PrintCount modeSTEP = IFSelect_ListByItem;
        readerSTEP.PrintCheckLoad(Standard_False, modeSTEP);
        readerSTEP.TransferRoots();
        *shape = readerSTEP.OneShape();
    }
    else if (filePath.endsWith(".iges", Qt::CaseInsensitive) || filePath.endsWith(".igs", Qt::CaseInsensitive))
    {
//        isSuccess = GemoRWUtilInstance::getInstance()->readOneShapeIgesFile(filePath, shape);
    }
    else if (filePath.endsWith(".brep", Qt::CaseInsensitive))
    {
//        isSuccess = GemoRWUtilInstance::getInstance()->readOneShapeBrepFile(filePath, shape);
    }

    GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = new GeomShapeObject(m_uuid, shape, getDisplayName());
    mgr->appendGeomShapeObject(object);

    VisualWidgetService *vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry", m_uuid + "_face");
    vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry", m_uuid + "_face");


	emit fileImported(filePath, m_uuid);
}


void GeometryImportWidget::onBrowseButtonClicked()
{
	// 打开文件选择对话框
	QString filePath = QFileDialog::getOpenFileName(
		this,
		"选择几何文件",
		QDir::homePath(),
		"几何文件 (*.stp *.step *.igs *.iges *.brep);"
	);

	// 如果用户选择了文件，更新 QLineEdit
	if (!filePath.isEmpty()) 
	{
		pathLineEdit->setText(filePath);
	}
}

void GeometryImportWidget::setFilePath(const QString & path)
{
	pathLineEdit->setText(path);
}
