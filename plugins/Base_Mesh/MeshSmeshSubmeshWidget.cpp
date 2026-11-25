#include <CompactDoubleSpinBox.h>
#include "MeshSmeshSubmeshWidget.h"
#include <ServiceRegistry.h>
#include <services/PreProcessingService.h>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QDoubleSpinBox* sizeEditor(QWidget* owner, const char* name, double value=0.) {
    auto* field=new CompactDoubleSpinBox(owner); field->setObjectName(name);
    field->setDecimals(8); field->setRange(0,1.e12); field->setSpecialValueText(QObject::tr("继承父级"));
    field->setValue(value); field->setKeyboardTracking(false); return field;
}
QComboBox* inheritedToggle(QWidget* owner, const char* name) {
    auto* box=new QComboBox(owner); box->setObjectName(name);
    box->addItem(QObject::tr("继承父级"),-1); box->addItem(QObject::tr("关闭"),0); box->addItem(QObject::tr("启用"),1); return box;
}
}

MeshSmeshSubmeshWidget::MeshSmeshSubmeshWidget(int dimension, QWidget* parent)
    : BasePage("MeshSmeshSubmeshWidget"), m_dimension(dimension)
{
    Q_UNUSED(parent);
    const auto kind=dimension==3?tr("体"):dimension==2?tr("面"):tr("边");
    auto* layout=new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    m_name=new QLineEdit(kind+tr("网格设置"),this);
    m_name->setObjectName("submeshName");
    auto* nameRow=new QFormLayout;
    nameRow->addRow(tr("名称"),m_name);
    layout->addLayout(nameRow);
    auto* group=new QGroupBox(tr("网格参数"),this);
    auto* form=new QFormLayout(group);
    form->setSpacing(7);
    m_max=sizeEditor(this,"submeshMaxSize"); form->addRow(tr("最大尺寸"),m_max);
    m_minMode=new QComboBox(this); m_minMode->setObjectName("submeshMinimumMode");
    m_minMode->addItem(tr("继承父级"),-1); m_minMode->addItem(tr("自动"),0); m_minMode->addItem(tr("指定尺寸"),1);
    m_min=sizeEditor(this,"submeshMinSize",.1); m_min->setSpecialValueText(QString()); m_min->setMinimum(1.e-8);
    if (dimension>=2) { form->addRow(tr("最小尺寸"),m_minMode); form->addRow(tr("最小尺寸值"),m_min); }
    else { m_minMode->hide(); m_min->hide(); }
    m_min->setEnabled(false);
    connect(m_minMode,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{m_min->setEnabled(m_minMode->currentData().toInt()==1);});
    if(dimension==3) {
        m_scope=new QComboBox(this); m_scope->setObjectName("submeshSizingScope");
        m_scope->addItem(tr("表面及内部"),1); m_scope->addItem(tr("仅内部"),0);
        form->addRow(tr("作用范围"),m_scope);
        m_fineness=new QComboBox(this); m_fineness->setObjectName("submeshFineness"); m_fineness->addItem(tr("继承父级"),-1);
        const QStringList levels{tr("很粗"),tr("粗"),tr("中等"),tr("细"),tr("很细")};
        for(int i=0;i<levels.size();++i)m_fineness->addItem(levels[i],i);
        form->addRow(tr("细化程度"),m_fineness);
        m_optimize=inheritedToggle(this,"submeshOptimize"); form->addRow(tr("网格优化"),m_optimize);
        m_curvature=inheritedToggle(this,"submeshCurvature"); form->addRow(tr("曲率细化"),m_curvature);
    } else if(dimension==2) {
        m_faceCurvature=new QSpinBox(this); m_faceCurvature->setObjectName("submeshCurvatureSegments");
        m_faceCurvature->setRange(0,1000); m_faceCurvature->setSpecialValueText(tr("继承父级"));
        form->addRow(tr("曲率分段数"),m_faceCurvature);
    } else {
        m_segments=new QSpinBox(this); m_segments->setObjectName("submeshEdgeSegments"); m_segments->setRange(0,1000000);
        m_segments->setSpecialValueText(tr("按尺寸继承 / 计算")); form->addRow(tr("固定分段数"),m_segments);
        m_bias=new CompactDoubleSpinBox(this); m_bias->setObjectName("submeshEdgeBias"); m_bias->setRange(.001,1000); m_bias->setValue(1);
        form->addRow(tr("分段比例"),m_bias); m_bias->setEnabled(false);
        connect(m_segments,QOverload<int>::of(&QSpinBox::valueChanged),this,[this](int value){m_bias->setEnabled(value>0);m_max->setEnabled(value==0);});
    }
    layout->addWidget(group);
    auto* entitiesGroup=new QGroupBox(tr("几何范围"),this);
    auto* entitiesLayout=new QVBoxLayout(entitiesGroup);
    entitiesLayout->setContentsMargins(8, 8, 8, 8);
    m_entities=new QListWidget(this);
    m_entities->setObjectName("submeshEntities");
    m_entities->setMinimumHeight(125);
    entitiesLayout->addWidget(m_entities,1);
    layout->addWidget(entitiesGroup,1);
    auto* buttons=new QHBoxLayout;
    auto* refresh=new QPushButton(tr("刷新"),this); auto* all=new QPushButton(tr("全选"),this); auto* clear=new QPushButton(tr("清空选择"),this);
    buttons->addWidget(refresh);buttons->addWidget(all);buttons->addWidget(clear); entitiesLayout->addLayout(buttons);
    connect(refresh,&QPushButton::clicked,this,&MeshSmeshSubmeshWidget::refreshEntities);
    auto toggleAll=[this](bool checked){
        const QSignalBlocker block(m_entities);
        for(int i=0;i<m_entities->count();++i) {
            auto* item=m_entities->item(i);
            if (item->flags() & Qt::ItemIsEnabled)
                item->setCheckState(checked?Qt::Checked:Qt::Unchecked);
        }
    };
    connect(all,&QPushButton::clicked,this,[this,toggleAll]{toggleAll(true);updateSelection();});
    connect(clear,&QPushButton::clicked,this,[this,toggleAll]{toggleAll(false);updateSelection();});
    connect(m_entities,&QListWidget::itemChanged,this,[this]{updateSelection();});
    connect(m_entities,&QListWidget::currentItemChanged,this,[this](QListWidgetItem* item){
        if(auto* pre=ServiceRegistry::instance().getService<PreProcessingService>();pre&&item)pre->highlightEntity(m_dimension,item->data(Qt::UserRole).toInt());
    });
    auto* compute=new QPushButton(tr("应用并生成网格"),this);
    compute->setObjectName("computeSubmesh");
    layout->addWidget(compute);
    connect(compute,&QPushButton::clicked,this,[this]{emit computeRequested(getUuid());});
    connect(m_name,&QLineEdit::textChanged,this,&MeshSmeshSubmeshWidget::configurationChanged);
}

void MeshSmeshSubmeshWidget::beforeLoad(){refreshEntities();}
void MeshSmeshSubmeshWidget::beforeUnload(){if(auto* pre=ServiceRegistry::instance().getService<PreProcessingService>()){pre->clearEntityHighlight();pre->clearEntityHoverHighlight();}}
void MeshSmeshSubmeshWidget::refreshEntities()
{
    auto* pre=ServiceRegistry::instance().getService<PreProcessingService>(); if(!pre)return;
    const auto available=m_available?m_available(m_dimension):pre->listEntityIds(m_dimension);
    const QSet<int> reserved=(m_dimension==3 && m_reservedVolumes)?m_reservedVolumes():QSet<int>{};
    // Sweep owns its solids. Reconcile old project selections as well as live
    // edits, so the UI and the serialized scope cannot disagree.
    bool changed=false;
    for (int i=m_selected.size()-1;i>=0;--i) {
        if (reserved.contains(m_selected[i])) {
            m_selected.removeAt(i);
            changed=true;
        }
    }
    const QSignalBlocker block(m_entities);
    m_entities->clear();
    for(const auto& text:available){
        const int id=text.toInt();if(id<=0)continue;
        const bool swept=reserved.contains(id);
        auto* item=new QListWidgetItem(tr("%1 %2").arg(m_dimension==3?tr("体"):m_dimension==2?tr("面"):tr("边")).arg(id)
                                       +(swept?tr("（已用于扫掠）"):QString()),m_entities);
        item->setData(Qt::UserRole,id);
        item->setCheckState(!swept && m_selected.contains(id)?Qt::Checked:Qt::Unchecked);
        if (swept) {
            item->setFlags(item->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable));
            item->setToolTip(tr("已分配给扫掠网格，请先从扫掠节点移除。"));
        } else item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    }
    // Geometry IDs which have vanished remain in the selection to fail
    // validation explicitly; only sweep ownership causes automatic release.
    if(changed) emit configurationChanged();
}
void MeshSmeshSubmeshWidget::updateSelection()
{
    m_selected.clear();QList<int> highlight;
    for(int i=0;i<m_entities->count();++i) {
        auto* item=m_entities->item(i);
        if ((item->flags() & Qt::ItemIsEnabled) && item->checkState()==Qt::Checked) {
            const int id=item->data(Qt::UserRole).toInt();m_selected.push_back(id);highlight.push_back(id);
        }
    }
    if(auto* pre=ServiceRegistry::instance().getService<PreProcessingService>())pre->highlightEntities(m_dimension,highlight,false);
    emit configurationChanged();
}
SmeshSubmeshSettings MeshSmeshSubmeshWidget::settings() const
{
    SmeshSubmeshSettings s;s.uuid=getUuid();s.name=m_name->text();s.dimension=m_dimension;s.entityIds=m_selected;
    s.maxSize=m_max->value()>0?m_max->value():-1.;
    s.minSize=m_minMode->currentData().toInt()<0?-1.:m_minMode->currentData().toInt()==0?0.:m_min->value();
    if(m_scope)s.surfaceSizing=m_scope->currentData().toBool();
    if(m_fineness)s.fineness=m_fineness->currentData().toInt();
    if(m_optimize)s.optimize=m_optimize->currentData().toInt();
    if(m_curvature)s.surfaceCurvature=m_curvature->currentData().toInt();
    if(m_segments){s.edgeSegments=m_segments->value();s.edgeBias=m_bias->value();if(s.edgeSegments>0)s.maxSize=-1.;}
    if(m_faceCurvature)s.curvatureSegments=m_faceCurvature->value();return s;
}
QJsonObject MeshSmeshSubmeshWidget::saveJson()
{
    const auto s=settings();QJsonArray ids;for(int id:s.entityIds)ids.append(id);
    return {{"name",s.name},{"dimension",s.dimension},{"entity_ids",ids},{"max_size",s.maxSize},{"min_size",s.minSize},
        {"fineness",s.fineness},{"optimize",s.optimize},{"curvature",s.surfaceCurvature},{"surface_sizing",s.surfaceSizing},
        {"edge_segments",s.edgeSegments},{"edge_bias",s.edgeBias},{"curvature_segments",s.curvatureSegments}};
}
void MeshSmeshSubmeshWidget::loadJson(const QJsonObject& obj)
{
    QString name=obj.value("name").toString(m_name->text());
    const QString legacy=m_dimension==3?tr("体 SubMesh"):m_dimension==2?tr("面 SubMesh"):tr("边 SubMesh");
    if(name==legacy)name=m_dimension==3?tr("体网格设置"):m_dimension==2?tr("面网格设置"):tr("边网格设置");
    m_name->setText(name);
    m_selected.clear();
    for(const auto& value:obj.value("entity_ids").toArray())if(value.toInt()>0&&!m_selected.contains(value.toInt()))m_selected.push_back(value.toInt());
    m_max->setValue(std::max(0.,obj.value("max_size").toDouble(-1.)));
    const double minimum=obj.value("min_size").toDouble(-1.);m_minMode->setCurrentIndex(minimum<0?0:minimum==0?1:2);if(minimum>0)m_min->setValue(minimum);
    auto set=[&](QComboBox* combo,const char* key,int fallback){if(combo)combo->setCurrentIndex(std::max(0,combo->findData(obj.value(key).toInt(fallback))));};
    set(m_fineness,"fineness",-1);set(m_optimize,"optimize",-1);set(m_curvature,"curvature",-1);
    if(m_scope)m_scope->setCurrentIndex(obj.value("surface_sizing").toBool(true)?0:1);
    if(m_segments){m_segments->setValue(obj.value("edge_segments").toInt());m_bias->setValue(obj.value("edge_bias").toDouble(1.));}
    if(m_faceCurvature)m_faceCurvature->setValue(obj.value("curvature_segments").toInt());
}

void MeshSmeshSubmeshWidget::setDisplayName(const QString& name) { m_name->setText(name); }
