#pragma once
#include "StressBoundaryPage.h"
#include "SolverQuietBoundary.h"
#include <ServiceRegistry.h>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QHideEvent>
#include <QJsonArray>
#include <vtkCell.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <algorithm>

// Each analysis owns its selection. The native file is generated, never the UI state.
class QuietBoundarySelector : public QWidget
{
 Q_OBJECT
public:
 explicit QuietBoundarySelector(QWidget* parent=nullptr):QWidget(parent)
 {
  setObjectName("quietBoundarySelector");
  auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
  page=new StressBoundaryPage("QuietBoundarySelection",QStringLiteral("粘弹性边界"),"FA_DYNA.DAT",this);layout->addWidget(page);
  status=new QLabel(this);status->setWordWrap(true);layout->addWidget(status);
  connect(page,&StressBoundaryPage::selectionCountChanged,this,[this](int){status->setText(summary());});
  status->setText(summary());
 }
 void start(){if(!isEnabled())return;page->beforeLoad();active=true;}
 void stop(){if(active){page->beforeUnload();active=false;}}
 QString summary(){return QStringLiteral("已选择 %1 %2；可在模型中框选，或按边界分组添加。")
     .arg(page->selectionCount()).arg(page->saveJson()["pick_mode"].toString()=="face"?QStringLiteral("个网格面"):QStringLiteral("条网格边"));}
 QJsonObject save(){
  auto state=page->saveJson();
  for(const auto key:{"face_solid","face_line","edge_solid","edge_line"}){
   QVector<qint64> values;for(const auto& value:state[key].toArray())values.append(value.toVariant().toLongLong());
   std::sort(values.begin(),values.end());QJsonArray ordered;for(auto value:values)ordered.append(value);state[key]=ordered;
  }
  return state;
 }
 void load(const QJsonObject& state,bool legacyEnabled)
 {
  stop();page->loadJson(state);
  if(state.isEmpty()&&legacyEnabled)restoreLegacySelection();
  status->setText(summary());
 }
 Q_INVOKABLE bool generate()
 {
  auto* mesh=ServiceRegistry::instance().getService<MeshModuleControllerService>();
  if(!mesh)return false;
  const auto state=save();const bool faces=state["pick_mode"].toString()=="face";
  QStringList rows;int width=0;
  for(const auto kind:{GridKind::Solid,GridKind::Line})
  {
   vtkUnstructuredGrid* grid=kind==GridKind::Solid?mesh->getVtkUnstructuredGridSolid():mesh->getVtkUnstructuredGridLine();
   const auto keys=state[faces?(kind==GridKind::Solid?"face_solid":"face_line"):(kind==GridKind::Solid?"edge_solid":"edge_line")].toArray();
   if(keys.isEmpty())continue;
   if(!grid)return false;
   auto* nodeIds=grid->GetPointData()->GetArray("node_id");auto* elemIds=grid->GetCellData()->GetArray("elem_id");
   for(const auto& key:keys)
   {
    const vtkIdType packed=key.toVariant().toLongLong(),ci=packed>>4;const int local=int(packed&15);
    if(ci<0||ci>=grid->GetNumberOfCells())return false;
    auto* cell=grid->GetCell(ci);if(local>=(faces?cell->GetNumberOfFaces():cell->GetNumberOfEdges()))return false;
    auto* boundary=faces?cell->GetFace(local):cell->GetEdge(local);if(!boundary)return false;
    const int count=boundary->GetNumberOfPoints();
    // Keep the node order of the established linear boundary exporter.
    if((faces&&count!=3&&count!=4)||(!faces&&count!=2))return false;
    width=faces?4:2;
    QString row=QString("%1 %2 0").arg(rows.size()+1).arg(elemIds?qint64(elemIds->GetTuple1(ci)):ci+1);
    for(int i=0;i<width;++i){const auto pid=boundary->GetPointId(qMin(i,count-1));row+=QString(" %1").arg(nodeIds?qint64(nodeIds->GetTuple1(pid)):pid+1);}
    rows.append(row+" 0\n");
   }
  }
  if(rows.isEmpty())return false;
  const QByteArray content=(QString("%1 %2 NJX MAKOD\nKJX KE1 KE2 JD... JAT\n").arg(rows.size()).arg(width+2)+rows.join("")).toLatin1();
  const QString project=PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
  if(project.isEmpty())return false;
  QStringList outputs{QDir(SolverFilePaths::projectInputDirectory(project)).filePath("FA_DYNA.DAT"),SolverQuietBoundary::destinationPath()};outputs.removeDuplicates();
  for(const auto& output:outputs){QDir().mkpath(QFileInfo(output).absolutePath());QSaveFile file(output);if(!file.open(QIODevice::WriteOnly)||file.write(content)!=content.size()||!file.commit())return false;}
  return true;
 }
 QLabel* statusLabel()const{return status;}
protected:
 void hideEvent(QHideEvent* event)override{stop();QWidget::hideEvent(event);}
private:
 static QString signature(QVector<qint64> ids){std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());QStringList s;for(auto id:ids)s.append(QString::number(id));return s.join(',');}
 void restoreLegacySelection()
 {
  auto* mesh=ServiceRegistry::instance().getService<MeshModuleControllerService>();if(!mesh)return;
  const QString project=PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
  QFile file(QDir(SolverFilePaths::projectInputDirectory(project)).filePath("FA_DYNA.DAT"));if(!file.open(QIODevice::ReadOnly|QIODevice::Text))return;
  QTextStream input(&file);const auto header=input.readLine().simplified().split(' ');if(header.size()<2)return;const int width=header[1].toInt()-2;if(width<2||width>8)return;input.readLine();
  QHash<qint64,QSet<QString>> requested;
  while(!input.atEnd()){const auto row=input.readLine().simplified().split(' ');if(row.size()<width+4)continue;QVector<qint64> ids;for(int i=0;i<width;++i)if(row[3+i].toLongLong()>0)ids.append(row[3+i].toLongLong());requested[row[1].toLongLong()].insert(signature(ids));}
  auto state=page->saveJson();const bool faces=state["pick_mode"].toString()=="face";
  for(const auto kind:{GridKind::Solid,GridKind::Line}){
   vtkUnstructuredGrid* grid=kind==GridKind::Solid?mesh->getVtkUnstructuredGridSolid():mesh->getVtkUnstructuredGridLine();if(!grid)continue;
   auto* nodes=grid->GetPointData()->GetArray("node_id");auto* elems=grid->GetCellData()->GetArray("elem_id");QJsonArray selected;
   for(vtkIdType ci=0;ci<grid->GetNumberOfCells();++ci){const auto wanted=requested.value(elems?qint64(elems->GetTuple1(ci)):ci+1);if(wanted.isEmpty())continue;auto* cell=grid->GetCell(ci);
    for(int j=0;j<(faces?cell->GetNumberOfFaces():cell->GetNumberOfEdges());++j){auto* b=faces?cell->GetFace(j):cell->GetEdge(j);QVector<qint64> ids;for(int n=0;n<b->GetNumberOfPoints();++n){auto pid=b->GetPointId(n);ids.append(nodes?qint64(nodes->GetTuple1(pid)):pid+1);}if(wanted.contains(signature(ids)))selected.append(qint64((ci<<4)|j));}}
   state[faces?(kind==GridKind::Solid?"face_solid":"face_line"):(kind==GridKind::Solid?"edge_solid":"edge_line")]=selected;
  }
  page->loadJson(state);
 }
 StressBoundaryPage* page=nullptr;QLabel* status=nullptr;bool active=false;
};
