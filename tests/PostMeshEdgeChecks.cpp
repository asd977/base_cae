#include "../plugins/Base_PostProcessing/PostVisualUtils.h"
#include "PostTestVisual.h"
#include <vtkPoints.h>
#include <vtkTriangle.h>
#include <vtkTransform.h>
#include <vtkMatrix4x4.h>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static vtkSmartPointer<vtkPolyData> surface(double shift=0)
{
    auto points=vtkSmartPointer<vtkPoints>::New();points->InsertNextPoint(shift,0,0);points->InsertNextPoint(shift+1,0,0);points->InsertNextPoint(shift,1,0);
    auto cells=vtkSmartPointer<vtkCellArray>::New();vtkIdType ids[]{0,1,2};cells->InsertNextCell(3,ids);
    auto data=vtkSmartPointer<vtkPolyData>::New();data->SetPoints(points);data->SetPolys(cells);return data;
}
int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    try {
        TestVisual visual;postSceneStyle()=PostSceneStyle{};postSceneStyle().representationMode=1;
        check(!postSceneStyle().showContextLines,"reference model defaults to no outline lines");
        postSceneStyle().showContextOutline=false;postSceneStyle().showContextLines=true;
        const auto contextSettings=savePostDisplaySettings();postSceneStyle()=PostSceneStyle{};loadPostDisplaySettings(contextSettings);
        check(!postSceneStyle().showContextOutline && postSceneStyle().showContextLines,"independent model and line settings persist");
        loadPostDisplaySettings(QJsonObject{{"showContextOutline",true}});
        check(postSceneStyle().showContextOutline && !postSceneStyle().showContextLines,"legacy model preference does not turn on lines");
        auto data=surface();const auto time=data->GetMTime();
        auto mapper=vtkSmartPointer<vtkDataSetMapper>::New();mapper->SetInputData(data);
        auto actor=vtkSmartPointer<vtkActor>::New();actor->SetMapper(mapper);
        const QString id="post.test.surface",edgeId=PostMeshEdges::actorId(id);
        check(addActorByService(&visual,id,actor,false),"surface attached");
        check(actor->GetProperty()->GetEdgeVisibility() && !visual.getProp(edgeId),"opaque edges use native single actor fast path");
        postSceneStyle().meshEdgeWidth=3;postSceneStyle().meshEdgeOpacity=.4;applyPostSceneRepresentation(&visual,false);
        auto* edge=vtkActor::SafeDownCast(visual.getProp(edgeId));check(edge,"independent edge actor");
        auto* edgeMapper=vtkDataSetMapper::SafeDownCast(edge->GetMapper());check(edgeMapper,"native edge mapper");
        check(edgeMapper->GetInputConnection(0,0)==mapper->GetInputConnection(0,0) && !edgeMapper->GetScalarVisibility(),"edge mapper shares surface input without scalar colors");
        check(!actor->GetProperty()->GetEdgeVisibility() && actor->GetProperty()->GetOpacity()==1 && edge->GetProperty()->GetOpacity()==.4 && edge->GetProperty()->GetLineWidth()==3,"opacity and width apply only to edges");
        check(!edge->GetPickable() && !edge->GetUseBounds(),"auxiliary edges do not affect picking or camera fit");
        visual.setVisible(id,false,false);check(!edge->GetVisibility(),"hidden surface hides edges");
        visual.setVisible(id,true,false);check(edge->GetVisibility(),"restored surface restores edges");
        actor->SetPosition(5,6,7);check(edge->GetUserMatrix()==actor->GetMatrix(),"edge transform follows source");
        actor->GetProperty()->SetOpacity(.2);check(std::abs(edge->GetProperty()->GetOpacity()-.08)<1e-12,"threshold transparency applies consistently");
        auto other=surface(2);mapper->SetInputData(other);
        check(edgeMapper->GetInputConnection(0,0)==mapper->GetInputConnection(0,0),"changed surface input reused");
        mapper->SetInputData(nullptr);check(!edge->GetVisibility(),"empty surface hides edge actor");
        mapper->SetInputData(other);check(edge->GetVisibility(),"new surface restores edge actor");
        const auto mapperTime=edgeMapper->GetMTime();
        for(int i=1;i<=8;++i) {
            postSceneStyle().meshEdgeOpacity=i/10.;postSceneStyle().meshEdgeWidth=1+i*.5;
            applyPostSceneRepresentation(&visual,false);check(visual.getProp(edgeId)==edge,"appearance edits reuse edge actor");
        }
        check(data->GetMTime()==time && actor->GetProperty()->GetOpacity()==.2,"appearance changes leave geometry and surface opacity unchanged");
        check(edgeMapper->GetMTime()==mapperTime,"width/opacity edits do not dirty the edge data pipeline");
        const auto saved=savePostDisplaySettings();postSceneStyle()=PostSceneStyle{};loadPostDisplaySettings(saved);
        check(postSceneStyle().meshEdgeOpacity==.8 && postSceneStyle().meshEdgeWidth==5,"edge settings persist");
        postSceneStyle().meshEdgeOpacity=0;applyPostSceneRepresentation(&visual,false);
        check(!visual.getProp(edgeId) && !actor->GetProperty()->GetEdgeVisibility(),"zero opacity hides only edges");
        postSceneStyle().meshEdgeOpacity=1;applyPostSceneRepresentation(&visual,false);
        check(!visual.getProp(edgeId) && actor->GetProperty()->GetEdgeVisibility(),"full opacity returns to native fast path");
        postSceneStyle().meshEdgeOpacity=.5;applyPostSceneRepresentation(&visual,false);
        removeActorByService(&visual,id,false);check(!visual.getProp(id) && !visual.getProp(edgeId) && PostMeshEdges::bindings().isEmpty(),"surface removal cleans up edge actor and observers");
        std::cout<<"Mesh edges: independent opacity/width, shared input, visibility, transforms, persistence and cleanup passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
