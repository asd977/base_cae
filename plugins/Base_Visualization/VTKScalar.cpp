#include "VTKScalar.h"
#include "vtkTecplotReader.h"
#include "vtkMultiBlockDataSet.h"
#include <vtkPointData.h>
#include <vtkMaskPoints.h>
#include <vtkUnstructuredGrid.h>
#include <iostream>
#include <vtkXMLUnstructuredGridReader.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include<vtkPolyData.h>
#include<vtkPoints.h>
#include<vtkPointData.h>
#include<vtkTriangle.h>
#include <vtkCellArray.h>
#include <vtkAutoInit.h>
#include<vector>
#include<vtkNew.h>
#include<vtkPolyDataMapper.h>
#include<vtkActor.h>
#include<vtkProperty.h>
#include<vtkColor.h>
#include<vtkNamedColors.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkDataSetMapper.h>
#include <vtkMath.h>
#include<vtkProp.h>
#include<vtkStripper.h>
#include<vtkIdList.h>
#include<vector>
#include <vtkCutter.h>
#include <vtkPlane.h>
#include <vtkPolyDataNormals.h>
#include<vtkCubeAxesActor.h>
#include<vtkMaskPoints.h>
#include<vtkArrowSource.h>
#include<vtkGlyph3D.h>
#include<vtkUnstructuredGrid.h>
#include<vtkDataArray.h>
#include <vtkScalarBarActor.h>
#include <vtkColorTransferFunction.h>
#include<vtkFloatArray.h>
#include<vtkLookupTable.h>
#include<vtkTextProperty.h>
#include<vtkAxesActor.h>
#include<vtkOrientationMarkerWidget.h>
#include<vtkOutlineFilter.h>
#include<vtkMaskPoints.h>
#include<vtkGlyph3D.h>
#include<vtkArrowSource.h>
#include <vtkLineSource.h>
#include <vtkStreamTracer.h>
#include<vtkArrayCalculator.h>
#include<vtkPointSource.h>
#include <vtkOpenFOAMReader.h>
#include <vtkSmartPointer.h>
#include <vtkAppendPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkUnstructuredGrid.h>
#include <vtkProperty.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkDataSetMapper.h>
#include <vtkScalarBarActor.h>
#include <vtkLookupTable.h>
#include <vtkTextProperty.h>
#include <vtkLineSource.h>
#include <vtkStreamTracer.h>
#include <vtkPolyData.h>
#include <vtkArrayCalculator.h>
#include "vtkGlyph3DMapper.h"
//#include "vtkTecEconReader.h"
#include "vtkCellData.h"
#include <vtkXMLUnstructuredGridWriter.h>

VTKScalar::VTKScalar ()
{
    mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    //mapper = vtkSmartPointer<vtkGlyph3DMapper>::New();
}

//读文件
vtkSmartPointer<vtkActor> VTKScalar::readScalarFile(std::vector<std::string>& vec_ps,vtkSmartPointer<vtkUnstructuredGrid> &grid, vtkSmartPointer<vtkScalarBarActor> &scalarBar)
{
    vec_ps.clear();
    std::cout<<"111111111111111111111"<<std::endl;
    std::string inputFilename = "./ceshi.tec";

    // 创建一个vtu文件读取器
    vtkSmartPointer<vtkTecplotReader> reader =
            vtkSmartPointer<vtkTecplotReader>::New();
    reader->SetFileName(inputFilename.c_str());
    reader->Update();
    //std::cout<<"2222222222222222222222222"<<std::endl;


    std::cout<<reader->GetNumberOfDataArrays()<<std::endl;
    int nNumScalar = reader->GetNumberOfDataArrays();

    //int nNumScalar = vtkReader->GetNumberOfScalarsInFile();

    //QStringList listInfo;
    //for(int i =0;i< nNumScalar;i++){
        //const  char* ps = reader->GetScalarsNameInFile(i);
        //listInfo<<QString(ps);
    //}

    for(int i =0;i< nNumScalar;i++){
        std::string ps = reader->GetDataArrayName(i);
        vec_ps.push_back(ps);
        //std::cout<<ps<<std::endl;
    }

    vtkSmartPointer<vtkMultiBlockDataSet> multi = vtkSmartPointer<vtkMultiBlockDataSet>::New();
    //vtkSmartPointer<vtkUnstructuredGrid> data_use = vtkSmartPointer<vtkUnstructuredGrid>::New();
    multi = reader->GetOutput();
    data_use = (vtkUnstructuredGrid*)(multi->GetBlock(0));
    vtkDataArray* scalars = data_use->GetPointData()->GetArray(4);

    double minTemp = scalars->GetRange()[0];
    double maxTemp = scalars->GetRange()[1];
    std::cout<<scalars->GetRange()[0]<<std::endl;
    std::cout<<scalars->GetRange()[1]<<std::endl;
    std::cout<<data_use->GetPointData()->GetArrayName(4)<<std::endl;

    // 创建一个色度图
    vtkSmartPointer<vtkLookupTable> lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetTableRange(minTemp, maxTemp); // 使用数据的最低温和最高温
    lut->SetHueRange(0.67, 0.0); // 0.67 对应蓝色, 0.0 对应红色
    lut->Build();

    double* range = scalars->GetRange();
    std::cout<<"55555555555555555555555555"<<std::endl;

    //glyph->GetUnstructuredGridOutput->GetPointData()->GetArray("u_mag");
    //glyph->GetOutput()->GetPointData()->AddArray(calc->GetUnstructuredGridOutput()->GetPointData()->GetArray("u_mag"));
    //double* range1 = calc->GetUnstructuredGridOutput()->GetPointData()->GetArray("u_mag")->GetRange();
    //std::cout<<range1[0]<<"  "<<range1[1]<<std::endl;
    // 创建一个映射对象和一个表演者来可视化数据

    //mapper->SetInputData(glyph->GetOutput());
    mapper->SetInputData(data_use);
    mapper->SetLookupTable(lut); // 应用色度图
    //mapper->SetScalarModeToUseCellFieldData();
    mapper->SetScalarModeToUsePointFieldData(); // 使用点数据的标量值
    //mapper->SelectColorArray("u_mag"); // 选择"温度"数据数组作为颜色映射的输入
    //mapper->SelectColorArray("ceshi"); // 选择"温度"数据数组作为颜色映射的输入
    mapper->SelectColorArray(data_use->GetPointData()->GetArrayName(4)); // 选择"温度"数据数组作为颜色映射的输入
    mapper->SetUseLookupTableScalarRange(false); // 强制使用LookupTable的范围
    mapper->SetScalarRange(range); // 强制使用LookupTable的范围
    //mapper->SetScaleModeToScaleByMagnitude();
    //mapper->SetScaleModeToScaleByVectorComponents();
    //mapper->SetScaleArray("ceshi");

    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    actor->GetProperty()->SetRepresentationToWireframe();//渲染为线框图
    //actor->GetProperty()->SetRepresentationTopoint();//渲染为线框图
    actor->GetProperty()->SetRepresentationToSurface();//渲染为面

    actor->GetProperty()->EdgeVisibilityOff();
    actor->GetProperty()->SetLineWidth(1);
    actor->GetProperty()->SetOpacity(0.5);
    actor->GetProperty()->SetEdgeColor(0,0,0);
    //actor->GetProperty()->EdgeVisibilityOff();

    std::cout<<"6666666666666666666666666666"<<std::endl;
    //actor->SetLookupTable(m_Actor->GetMapper()->GetLookupTable());

    // 创建一个新的颜色条对象
    scalarBar = vtkSmartPointer<vtkScalarBarActor>::New();
    // 设置颜色条的颜色查阅表
    scalarBar->SetLookupTable(lut);
    scalarBar->SetTitle("CSRock");
    scalarBar->SetNumberOfLabels(5);
    scalarBar->SetPosition(0.9, 0.2);  // 将颜色条向左移动
    scalarBar->SetWidth(0.1); // 将颜色条设为较窄的尺寸
    scalarBar->SetHeight(0.6); // 将颜色条设为较短的尺寸

    vtkSmartPointer<vtkXMLUnstructuredGridWriter> writer =
    vtkSmartPointer<vtkXMLUnstructuredGridWriter>::New();
    writer->SetFileName("ceshi.vtu");
    writer->SetInputData(data_use);
    writer->Write();

    return actor;
}

void VTKScalar::change(int i)
{
   std::cout<<"okokokokokokokokokokokoko"<<std::endl;
   data_use->GetPointData()->SetActiveScalars(data_use->GetPointData()->GetArrayName(i));
   vtkDataArray* scalars = data_use->GetPointData()->GetArray(i);
   double* range = scalars->GetRange();
   std::cout<<range[0]<<std::endl;
   std::cout<<range[1]<<std::endl;
   mapper->SetScalarRange(range);
   mapper->SelectColorArray(data_use->GetPointData()->GetArrayName(i)); // 选择"温度"数据数组作为颜色映射的输入
   //m_scalarBarWgt->GetScalarBarActor()->SetLookupTable(m_Actor->GetMapper()->GetLookupTable());
}

//读文件
vtkSmartPointer<vtkActor> VTKScalar::readScalarFile2(vtkSmartPointer<vtkUnstructuredGrid> &grid, vtkSmartPointer<vtkScalarBarActor> &scalarBar)
{
    std::string inputFilename = "scalar1.vtu";

    // 创建一个vtu文件读取器
    vtkSmartPointer<vtkXMLUnstructuredGridReader> reader =
            vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();
    reader->SetFileName(inputFilename.c_str());
    reader->Update();

    grid = reader->GetOutput();

    // 标量值存储在名为"温度“的数据数组中
    vtkDataArray* temperatures = grid->GetPointData()->GetArray("温度");
    double minTemp = temperatures->GetRange()[0];
    double maxTemp = temperatures->GetRange()[1];
    qDebug()<< temperatures->GetRange()[0] <<" "<<temperatures->GetRange()[1];
    // &#x6e29;&#x5ea6;

    // 创建一个色度图
    vtkSmartPointer<vtkLookupTable> lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetTableRange(minTemp, maxTemp); // 使用数据的最低温和最高温
    lut->SetHueRange(0.67, 0.0); // 0.67 对应蓝色, 0.0 对应红色
    lut->Build();

    // 创建一个映射对象和一个表演者来可视化数据
    vtkSmartPointer<vtkDataSetMapper> mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputConnection(reader->GetOutputPort());
    mapper->SetLookupTable(lut); // 应用色度图
    mapper->SetScalarModeToUsePointFieldData(); // 使用点数据的标量值
    mapper->SelectColorArray("温度"); // 选择"温度"数据数组作为颜色映射的输入
    mapper->SetUseLookupTableScalarRange(true); // 强制使用LookupTable的范围

    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);


    // 创建一个新的颜色条对象
    scalarBar = vtkSmartPointer<vtkScalarBarActor>::New();
    // 设置颜色条的颜色查阅表
    scalarBar->SetLookupTable(lut);
    scalarBar->SetTitle("Temperature");
    scalarBar->SetNumberOfLabels(5);
    scalarBar->SetPosition(0.9, 0.2);  // 将颜色条向左移动
    scalarBar->SetWidth(0.1); // 将颜色条设为较窄的尺寸
    scalarBar->SetHeight(0.6); // 将颜色条设为较短的尺寸

    return actor;

//    double bounds[6];
//    grid->GetBounds(bounds);
//    double length = bounds[1] - bounds[0]; // Xmax - Xmin
//    double width = bounds[3] - bounds[2]; // Ymax - Ymin
//    double height = bounds[5] - bounds[4]; // Zmax - Zmin

//    std::cout << "Length: " << length << std::endl;
//    std::cout << "Width: " << width << std::endl;
//    std::cout << "Height: " << height << std::endl;
}
