#include <SettingsLayout.h>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void settle(QApplication& app){for(int i=0;i<6;++i)app.processEvents();}
int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    try {
        QScrollArea scroll;scroll.setWidgetResizable(true);
        auto* stack=new CaeSettingsLayout::ActivePageStack;scroll.setWidget(stack);
        auto* page=new QWidget;auto* form=new QFormLayout(page);
        auto* number=new QDoubleSpinBox;number->setRange(-1e12,1e12);number->setDecimals(6);number->setValue(-123456.123456);
        auto* label=new QLabel(QStringLiteral("弹性模量 (GPa)"));form->addRow(label,number);
        auto* combo=new QComboBox;combo->addItem(QString(500,QChar('A')));form->addRow(QStringLiteral("材料模型"),combo);
        auto* group=new QGroupBox;auto* grid=new QGridLayout(group);
        auto* directionLabel=new QLabel(QStringLiteral("X 方向"));auto* direction=new QComboBox;direction->addItem(QStringLiteral("固定"));
        grid->addWidget(directionLabel,0,0);grid->addWidget(direction,0,1);grid->setColumnStretch(0,1);
        auto* prescribed=new QDoubleSpinBox;grid->addWidget(prescribed,0,2);prescribed->hide();form->addRow(group);
        CaeSettingsLayout::prepare(page);
        int changes=0;QObject::connect(number,&QDoubleSpinBox::valueChanged,[&changes](double){++changes;});
        stack->addWidget(page);stack->setCurrentWidget(page);
        scroll.resize(380,480);scroll.show();settle(app);
        const int activeWidth=stack->minimumSizeHint().width();
        auto* inactive=new QWidget;inactive->setMinimumWidth(2000);stack->addWidget(inactive);settle(app);
        check(stack->minimumSizeHint().width()==activeWidth,"inactive pages changed active width");
        check(scroll.horizontalScrollBar()->maximum()==0,"readable form needs horizontal scroll at normal width");
        check(label->width()>=label->fontMetrics().horizontalAdvance(QStringLiteral("材料模型")),"Chinese label squeezed vertically");
        check(directionLabel->width()==directionLabel->sizeHint().width(),"grid label consumes remaining field space");
        const int directionWidth=direction->width();
        prescribed->show();settle(app);
        check(!direction->geometry().intersects(prescribed->geometry()),"new grid field overlaps existing field");
        prescribed->hide();settle(app);
        check(direction->width()>=directionWidth && grid->columnStretch(2)==0,"hidden field keeps an empty expanding column");
        auto* editor=number->findChild<QLineEdit*>();
        check(editor && editor->width()>=editor->fontMetrics().horizontalAdvance(editor->text()),"numeric value clipped");
        const int oldFloor=number->minimumWidth();QFont font=page->font();font.setPointSize(font.pointSize()+5);page->setFont(font);settle(app);
        check(number->minimumWidth()>oldFloor,"font change did not update readable width");
        scroll.resize(120,480);settle(app);
        check(scroll.horizontalScrollBar()->maximum()>0,"extreme narrow layout lost scroll access");
        check(label->y()+label->height()/2==number->y()+number->height()/2,"narrow form stacked its label above the field");
        check(editor->width()>=editor->fontMetrics().horizontalAdvance(editor->text()),"narrow layout clips numeric value");
        auto* extra=new QDoubleSpinBox;extra->setDecimals(6);extra->setRange(-1e10,1e10);extra->setValue(12345.6789);
        form->addRow(QStringLiteral("新增参数"),extra);settle(app);
        check(extra->minimumWidth()>=oldFloor,"new row did not inherit readable sizing");
        auto* parameter=new QLineEdit("17");parameter->setValidator(new QDoubleValidator(parameter));form->addRow(QStringLiteral("参数"),parameter);
        settle(app);const int compactWidth=parameter->minimumWidth();
        parameter->setText("-1234567.0000070369");settle(app);
        check(parameter->minimumWidth()>compactWidth && parameter->width()>=parameter->fontMetrics().horizontalAdvance(parameter->text()),"numeric line editor clips longer values");
        check(parameter->text()=="-1234567.0000070369","numeric editor sizing changed precision");
        CaeSettingsLayout::prepare(page);CaeSettingsLayout::prepare(page);settle(app);
        check(changes==0 && number->value()==-123456.123456 && combo->currentIndex()==0,"layout modified field values or emitted edits");
        std::cout<<"Settings layout: active page, labels, font scaling, dynamic rows, scroll access and unchanged values passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
