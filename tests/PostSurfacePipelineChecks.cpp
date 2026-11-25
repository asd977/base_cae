#include "../plugins/Base_PostProcessing/PostSurfacePipeline.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <vtkAppendFilter.h>
#include <vtkSphereSource.h>
#include <vtkDoubleArray.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkXMLUnstructuredGridWriter.h>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static void drain(PostSurfacePipeline& pipeline)
{
    QElapsedTimer timer;timer.start();
    do {QCoreApplication::processEvents();QThread::msleep(1);}
    while(pipeline.pending() && timer.elapsed()<30000);
    check(!pipeline.pending(),"pipeline drained");
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    try {
        auto sphere=vtkSmartPointer<vtkSphereSource>::New();
        sphere->SetThetaResolution(320);sphere->SetPhiResolution(240);sphere->Update();
        auto surface=sphere->GetOutput();surface->GetPointData()->SetNormals(nullptr);
        auto values=vtkSmartPointer<vtkDoubleArray>::New();values->SetName("S");
        for(vtkIdType i=0;i<surface->GetNumberOfPoints();++i) {
            double p[3];surface->GetPoint(i,p);values->InsertNextValue(p[0]+2*p[1]);
        }
        surface->GetPointData()->AddArray(values);
        auto cells=vtkSmartPointer<vtkDoubleArray>::New();cells->SetName("C");
        cells->SetNumberOfTuples(surface->GetNumberOfCells());cells->FillComponent(0,7);
        surface->GetCellData()->AddArray(cells);
        auto append=vtkSmartPointer<vtkAppendFilter>::New();append->AddInputData(surface);append->Update();
        auto volume=vtkSmartPointer<vtkUnstructuredGrid>::New();volume->ShallowCopy(append->GetOutput());
        const auto sourceTime=volume->GetMTime();
        PostSurfacePipeline pipeline;int completions=0;
        PostPreparedSurface prepared;
        pipeline.ready=[&](const PostPreparedSurface& result){++completions;prepared=result;};
        const PostSurfaceOptions smooth{true,true,30};
        pipeline.request(volume,smooth);pipeline.request(volume,smooth);drain(pipeline);
        check(completions==1 && prepared.surface,"duplicate requests coalesce and prepare full surface");
        check(prepared.surface->GetPointData()->GetNormals(),"full surface normals");
        check(prepared.surface->GetNumberOfCells()==volume->GetNumberOfCells(),"full triangle geometry is never reduced");
        auto* pointIds=prepared.surface->GetPointData()->GetArray("vtkOriginalPointIds");
        auto* scalar=prepared.surface->GetPointData()->GetArray("S");
        check(pointIds && scalar && prepared.surface->GetCellData()->GetArray("C"),"full surface retains point and cell arrays");
        for(vtkIdType i=0;i<scalar->GetNumberOfTuples();++i) {
            const auto original=static_cast<vtkIdType>(pointIds->GetComponent(i,0));
            check(scalar->GetComponent(i,0)==values->GetValue(original),"full surface preserves selected point values");
        }
        check(sourceTime==volume->GetMTime() && !volume->GetPointData()->GetNormals(),"source data immutable");
        const auto full=prepared.surface;
        check(pipeline.find(volume,smooth)->surface==full,"cache returns exact native output");
        pipeline.request(volume,smooth);drain(pipeline);check(completions==1,"cache avoids repeat work");
        pipeline.request(volume,{true,false,45});QCoreApplication::processEvents();pipeline.cancel();drain(pipeline);
        check(completions==1 && !pipeline.find(volume,{true,false,45}),"cancelled work never commits or enters cache");
        volume->Modified();check(!pipeline.find(volume,smooth),"modified source invalidates cached display");
        pipeline.request(volume,smooth);drain(pipeline);check(completions==2,"modified source prepares again");
        QTemporaryDir directory;check(directory.isValid(),"temporary result directory");
        const auto path=directory.filePath("result.vtu").toUtf8();
        auto writer=vtkSmartPointer<vtkXMLUnstructuredGridWriter>::New();writer->SetInputData(volume);writer->SetFileName(path);
        check(writer->Write(),"write file fixture");
        pipeline.requestFile("missing.vtu",{});pipeline.requestFile(QString::fromUtf8(path),{});drain(pipeline);
        check(completions==3 && prepared.fileName==QString::fromUtf8(path) && prepared.error.isEmpty(),"latest file wins");
        pipeline.requestFile(directory.filePath("missing.vtu"),{});drain(pipeline);
        check(completions==4 && !prepared.error.isEmpty(),"read errors delivered without stale data");
        pipeline.request(volume,{});drain(pipeline);
        const auto faceted=prepared.surface;
        check(completions==5 && faceted && !faceted->GetPointData()->GetNormals(),"prepare complete faceted surface");
        pipeline.request(volume,{false,false,75});drain(pipeline);
        check(completions==5 && pipeline.find(volume,{false,false,75})->surface==faceted,
              "inactive normal options do not rebuild identical faceted geometry");
        {PostSurfacePipeline closing;closing.request(volume,smooth);QCoreApplication::processEvents();}
        std::cout<<"Native background preparation, full-resolution scalar preservation, immutable source, cache invalidation, cancellation and destruction passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
