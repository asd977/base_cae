#include "PostTestVisual.h"
#include "../plugins/Base_PostProcessing/BlockSettingsWidget.h"
#include "../plugins/Base_PostProcessing/SliceSettingsWidget.h"
#include "../plugins/Base_PostProcessing/IsosurfaceSettingsWidget.h"
#include "../plugins/Base_PostProcessing/IsosurfaceSegmentationWidget.h"
#include "../plugins/Base_PostProcessing/PostVisualUtils.h"
#include "../plugins/Base_PostProcessing/PostResultSelectionUtils.h"
#include <PublicVariables.h>
#include <PublicConstants.h>
#include <vtkPoints.h>
#include <vtkActor.h>
#include <vtkMapper.h>
#include <vtkAlgorithm.h>
#include <vtkDoubleArray.h>
#include <vtkPointData.h>
#include <vtkXMLUnstructuredGridWriter.h>
#include <iostream>
#include <array>
#include <type_traits>
#include <stdexcept>

static void check(bool ok,const QString& message)
{if(!ok)throw std::runtime_error(message.toStdString());}
static void writeFixture(const QString& path,double offset)
{
    auto grid=vtkSmartPointer<vtkUnstructuredGrid>::New();auto points=vtkSmartPointer<vtkPoints>::New();
    for(const auto& p:std::initializer_list<std::array<double,3>>{
            {0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}})
        points->InsertNextPoint(p.data());
    grid->SetPoints(points);
    const vtkIdType cells[6][4]={{0,1,2,6},{0,2,3,6},{0,3,7,6},{0,7,4,6},{0,4,5,6},{0,5,1,6}};
    for(const auto& cell:cells)grid->InsertNextCell(VTK_TETRA,4,cell);
    for(int field=0;field<2;++field) {
        auto values=vtkSmartPointer<vtkDoubleArray>::New();values->SetName(field?"SYY(MPa)":"SXX(MPa)");
        for(vtkIdType i=0;i<points->GetNumberOfPoints();++i) {
            double p[3];points->GetPoint(i,p);values->InsertNextValue(offset+(field?3-.5*(p[0]+p[1]+p[2]):p[0]+p[1]+p[2]));
        }
        grid->GetPointData()->AddArray(values);
    }
    auto writer=vtkSmartPointer<vtkXMLUnstructuredGridWriter>::New();writer->SetInputData(grid);
    writer->SetFileName(path.toUtf8());check(writer->Write(),"write refresh fixture");
}
template<class F>static void oneFrame(TestVisual& visual,const QString& action,F perform)
{
    const int before=visual.totalRenderRequests();perform();
    const int frames=visual.totalRenderRequests()-before;
    std::cout<<action.toStdString()<<": render requests="<<frames<<'\n';
    check(frames==1,action+QStringLiteral(" must draw once, got %1").arg(frames));
}
template<class Page>static void pageChecks(const QString& name,const QString& first,const QString& second)
{
    postSceneStyle()=PostSceneStyle{};postContourStyle().enabled=false;postFieldDisplayStyles().clear();
    PostResultSelectionUtils::sharedResultSelection().fieldName="SYY(MPa)";
    TestVisual visual;Page page(&visual);page.setProperty("postDisplayActive",true);
    auto* steps=page.template findChild<QComboBox*>("stepComboBox");check(steps,name+" result selector");
    {const QSignalBlocker block(steps);steps->clear();steps->addItem("first",first);steps->addItem("second",second);}
    oneFrame(visual,name+" initial result",[&]{
        check(QMetaObject::invokeMethod(&page,"on_stepComboBox_currentIndexChanged",Q_ARG(int,0)),"load result slot");
    });
    int clipExecutions=0;vtkAlgorithm* clip=nullptr;
    if constexpr(std::is_same_v<Page,BlockSettingsWidget>) {
        auto* actor=vtkActor::SafeDownCast(visual.getProp(postActorId(page.getUuid(),"block")));
        check(actor && actor->GetMapper(),"clip actor");clip=actor->GetMapper()->GetInputAlgorithm();
        auto callback=vtkSmartPointer<vtkCallbackCommand>::New();callback->SetClientData(&clipExecutions);
        callback->SetCallback([](vtkObject*,unsigned long,void* count,void*){++*static_cast<int*>(count);});
        clip->AddObserver(vtkCommand::EndEvent,callback);
    }
    oneFrame(visual,name+" result selection",[&]{steps->setCurrentIndex(1);});
    if(clip) {
        std::cout<<"Block result selection: native clip executions="<<clipExecutions<<'\n';
        check(clipExecutions==1,"changing result runs native clipping once");
    }
    clipExecutions=0;
    auto* fields=page.template findChild<QComboBox*>("scalarComboBox");check(fields && fields->count()==2,name+" scalar fields");
    oneFrame(visual,name+" field selection",[&]{fields->setCurrentIndex((fields->currentIndex()+1)%fields->count());});
    if(clip)check(clipExecutions==0,"changing color field does not rerun clipping");
    if(auto* position=page.template findChild<QDoubleSpinBox*>("slicePositionSpin"))
        oneFrame(visual,name+" position",[&]{position->setValue(.6);});
    if(auto* position=page.template findChild<QDoubleSpinBox*>("blockPositionSpin"))
        oneFrame(visual,name+" position",[&]{position->setValue(.6);});
    if(auto* percent=page.template findChild<QRadioButton*>("percentModeRadioButton")) {
        if(!percent->isChecked())percent->setChecked(true);
        auto* slider=page.template findChild<QSlider*>("isoSlider");check(slider,name+" iso slider");
        int contourExecutions=0;vtkAlgorithm* contour=nullptr;unsigned long contourObserver=0;
        if constexpr(std::is_same_v<Page,IsosurfaceSettingsWidget>) {
            auto* actor=vtkActor::SafeDownCast(visual.getProp(postActorId(page.getUuid(),"single")));
            check(actor && actor->GetMapper(),"single iso actor");contour=actor->GetMapper()->GetInputAlgorithm();
            auto callback=vtkSmartPointer<vtkCallbackCommand>::New();callback->SetClientData(&contourExecutions);
            callback->SetCallback([](vtkObject*,unsigned long,void* count,void*){++*static_cast<int*>(count);});
            contourObserver=contour->AddObserver(vtkCommand::EndEvent,callback);
        }
        oneFrame(visual,name+" iso threshold",[&]{slider->setValue(slider->value()==45?55:45);});
        if(contour) {
            check(contourExecutions==1,"changed iso threshold runs native contour once");contourExecutions=0;
            oneFrame(visual,name+" unchanged threshold refresh",[&]{
                check(QMetaObject::invokeMethod(&page,"on_isoSlider_valueChanged",Q_ARG(int,slider->value())),"refresh same iso threshold");
            });
            check(contourExecutions==0,"unchanged native contour is not forced to execute again");
            contour->RemoveObserver(contourObserver);
        }
        if(auto* value=page.template findChild<QRadioButton*>("valueModeRadioButton"))
            oneFrame(visual,name+" mode change",[&]{value->setChecked(true);});
    }
    if constexpr(std::is_same_v<Page,BlockSettingsWidget> || std::is_same_v<Page,IsosurfaceSegmentationWidget>) {
        {const QSignalBlocker block(steps);steps->clear();}
        oneFrame(visual,name+" node entry",[&]{page.beforeLoad();});
        check(steps->count()>0,name+" node entry loads result list");
    }
}
int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    try {
        QTemporaryDir project;check(project.isValid(),"temporary refresh project");
        PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey,project.path(),"refresh-test");
        const auto first=project.filePath("first.vtu"),second=project.filePath("second.vtu");
        writeFixture(first,0);writeFixture(second,10);
        const auto writeText=[&](const QString& name,const QByteArray& bytes) {
            QFile file(project.filePath(name));check(file.open(QIODevice::WriteOnly),"write solver fixture");file.write(bytes);
        };
        writeText("IIIN.DAT","8 3 1 8 3\n8 3\n0 0 0 10\n1 0 0 20\n1 1 0 30\n0 1 0 40\n0 0 1 50\n1 0 1 60\n1 1 1 70\n0 1 1 80\n1 10 20 30 40 50 60 70 80 6 2 1\n");
        QByteArray nodal="NUMNP NCOM\nJD V1 V2 V3 SL\n";
        for(int i=1;i<=8;++i)nodal+=QByteArray::number(i*10)+" 11 22 33 "+QByteArray::number(i/10.0)+"\n";
        writeText("NODCMM.TXT",nodal);
        pageChecks<SliceSettingsWidget>("Slice",first,second);
        pageChecks<BlockSettingsWidget>("Block",first,second);
        pageChecks<IsosurfaceSettingsWidget>("Isosurface",first,second);
        pageChecks<IsosurfaceSegmentationWidget>("Segmentation",first,second);
        std::cout<<"PASS: result, field, position and threshold actions draw once\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
