#include "GemoRWUtilInstance.h"
#include <XCAFApp_Application.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TDataStd_Name.hxx>
#include <TCollection_ExtendedString.hxx>

#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_Reader.hxx>

#include <TopExp_Explorer.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopAbs.hxx>
#include <TopoDS.hxx>
#include <QDebug>
#include <XSControl_WorkSession.hxx>
#include <BRepPrimAPI_MakeBox.hxx>

GemoRWUtilInstance * GemoRWUtilInstance::_instance = nullptr;

GemoRWUtilInstance * GemoRWUtilInstance::getInstance()
{
	if (_instance == nullptr)
	{
		_instance = new GemoRWUtilInstance;
	}
	return _instance;
}

GemoRWUtilInstance::~GemoRWUtilInstance()
{

}

bool GemoRWUtilInstance::readOneShapeStepFile(const QString& filePath, TopoDS_Shape* shape)
{
	STEPControl_Reader readerSTEP;
	IFSelect_ReturnStatus statSTEP = readerSTEP.ReadFile(filePath.toUtf8().constData());
	if (statSTEP != IFSelect_RetDone) {
		return false;
	}
	IFSelect_PrintCount modeSTEP = IFSelect_ListByItem;
	readerSTEP.PrintCheckLoad(Standard_False, modeSTEP);
	readerSTEP.TransferRoots();
	*shape = readerSTEP.OneShape();
	return true;
}

bool GemoRWUtilInstance::readOneShapeIgesFile(const QString& filePath, TopoDS_Shape* shape)
{
	IGESControl_Reader ReaderIGES;
	IFSelect_ReturnStatus statIGES = ReaderIGES.ReadFile(filePath.toUtf8().constData());
	if (statIGES != IFSelect_RetDone) {
		return false;
	}
	IFSelect_PrintCount modeIGES = IFSelect_ListByItem;
	ReaderIGES.PrintCheckLoad(Standard_True, modeIGES);
	ReaderIGES.TransferRoots();
	TopoDS_Shape* TopoDS_ShapeIGES = new TopoDS_Shape;
	*shape = ReaderIGES.OneShape();
	return true;
}

bool GemoRWUtilInstance::readOneShapeBrepFile(const QString& filePath, TopoDS_Shape* shape)
{
	std::string filePathStr = filePath.toStdString();
	std::ifstream file(filePathStr);
	if (!file.is_open())
	{
		return false;
	}
	BRep_Builder builder;
	Standard_Boolean result = BRepTools::Read(*shape, filePathStr.c_str(), builder);
	if (!result)
	{
		return false;
	}
	return true;
}

bool GemoRWUtilInstance::writeOneShapeStepFile(const QString & filePath, TopoDS_Shape* shape)
{
	STEPControl_Writer stepWriter;
	IFSelect_ReturnStatus status;
	status = stepWriter.Transfer(*shape, STEPControl_AsIs);
	if (status != IFSelect_RetDone) return false;
	status = stepWriter.Write(filePath.toUtf8());
	if (status != IFSelect_RetDone) return false;
	return true;
}

bool GemoRWUtilInstance::writeOneShapeIgesFile(const QString & filePath, TopoDS_Shape* shape)
{
	// ！！！注意此方法导出的iges文件 自己不能解析 comsol可以解析
    //原因:(因为把一个正方体当作六个面导出了) 
	IGESControl_Writer igesWriter;
	bool addSuccess = igesWriter.AddShape(*shape);
	if (!addSuccess)return false;
	igesWriter.ComputeModel();
	bool writeSuccess = igesWriter.Write(filePath.toUtf8());
	if (!writeSuccess)return false;
	return true;
}

bool GemoRWUtilInstance::writeOneShapeBrepFile(const QString & filePath, TopoDS_Shape* shape)
{
	bool flag = BRepTools::Write(*shape, filePath.toUtf8());
	return flag;
}

bool GemoRWUtilInstance::writeMultipleShapesStepFile(const QString& filePath, const QVector<TopoDS_Shape*>& shapes)
{

    // // 创建一个复合形状
    // TopoDS_Compound compound;
    // BRep_Builder builder;
    // builder.MakeCompound(compound);

    // // 将所有输入形状添加到复合形状中
    // for (const auto& shape : shapes)
    // {
    // 	if (!shape->IsNull())
    // 	{
    // 		builder.Add(compound, *shape);
    // 	}
    // }

    // STEPControl_Writer stepWriter;
    // IFSelect_ReturnStatus status = stepWriter.Transfer(compound, STEPControl_AsIs);
    // if (status != IFSelect_RetDone)
    // {
    // 	return false;
    // }

    // status = stepWriter.Write(filePath.toUtf8().constData());
    // if (status != IFSelect_RetDone) {
    // 	return false;
    // }
    return true;
}

bool GemoRWUtilInstance::writeMultipleShapesIgesFile(const QString& filePath, const QVector<TopoDS_Shape*>& shapes)
{
	// 创建一个复合形状
	TopoDS_Compound compound;
	BRep_Builder builder;
	builder.MakeCompound(compound);

	// 将所有输入形状添加到复合形状中
	for (const auto& shape : shapes) {
		if (!shape->IsNull()) {
			builder.Add(compound, *shape);
		}
	}

	// 使用 IGESControl_Writer 导出复合形状
	IGESControl_Writer igesWriter;
	bool addSuccess = igesWriter.AddShape(compound);
	if (!addSuccess) {
		return false;
	}

	igesWriter.ComputeModel();
	bool writeSuccess = igesWriter.Write(filePath.toUtf8().constData());
	if (!writeSuccess) {
		return false;
	}

	return true;
}

bool GemoRWUtilInstance::writeMultipleShapesBrepFile(const QString& filePath, const QVector<TopoDS_Shape*>& shapes) 
{
	// 创建一个复合形状
	TopoDS_Compound compound;
	BRep_Builder builder;
	builder.MakeCompound(compound);

	// 将所有输入形状添加到复合形状中
	for (const auto& shape : shapes) {
		if (!shape->IsNull()) {
			builder.Add(compound, *shape);
		}
	}

	// 导出复合形状到 BREP 文件
	bool writeSuccess = BRepTools::Write(compound, filePath.toUtf8().constData());
	if (!writeSuccess) {
		return false;
	}

	return true;
}
