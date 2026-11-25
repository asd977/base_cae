#include <CompactDoubleSpinBox.h>
#include <CompactNumberDelegate.h>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QTableWidget>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    try {
        CompactDoubleSpinBox spin;
        check(spin.text()=="0","default zero still has padding");
        spin.setLocale(QLocale::c());spin.setDecimals(10);spin.setRange(-1e12,1e12);
        const QList<QPair<double,QString>> cases{{0,"0"},{10,"10"},{1000,"1000"},
            {-4195866,"-4195866"},{510261.375,"510261.375"},{510965.0625,"510965.0625"},
            {4196255.5,"4196255.5"},{0.0000070369,"0.0000070369"},{-0.0000070369,"-0.0000070369"}};
        for(const auto& item:cases) {
            spin.setValue(item.first);const double value=spin.value();
            const auto saved=QJsonDocument(QJsonObject{{"value",value}}).toJson();
            check(spin.text()==item.second,"padded/rounded numeric display");
            spin.interpretText();
            check(spin.value()==value && QJsonDocument(QJsonObject{{"value",spin.value()}}).toJson()==saved,"formatting changed persisted value");
        }
        spin.setDecimals(9);spin.setValue(2144.214599609);
        check(spin.text()=="2144.214599609","nonzero fraction lost precision");
        spin.setPrefix("x = ");spin.setSuffix(" m");spin.setValue(12.5);
        check(spin.text()=="x = 12.5 m","prefix or suffix changed");
        spin.findChild<QLineEdit*>()->setText("x = -12.125 m");spin.interpretText();
        check(spin.value()==-12.125,"native editing no longer parses");
        spin.setSingleStep(0.125);spin.stepUp();check(spin.value()==-12,"native stepping changed");
        spin.setPrefix({});spin.setSuffix({});spin.setValue(1000.25);
        for(const QLocale locale:{QLocale(QLocale::German),QLocale(QLocale::Arabic)}) {
            spin.setLocale(locale);spin.setGroupSeparatorShown(true);
            bool ok=false;check(locale.toDouble(spin.text(),&ok)==1000.25 && ok,"localized numeric display no longer round-trips");
            check(!spin.text().endsWith(locale.zeroDigit()),"localized fraction still padded");
        }
        spin.setSpecialValueText("自动");spin.setValue(spin.minimum());check(spin.text()=="自动","special value text lost");

        QTableWidget table(1,2);auto* delegate=new CompactNumberDelegate(&table);table.setItemDelegate(delegate);
        auto* item=new QTableWidgetItem("510261.375000000");table.setItem(0,0,item);
        check(delegate->displayText(item->data(Qt::DisplayRole),QLocale::c())=="510261.375","table fraction not compact");
        check(item->text()=="510261.375000000" && item->data(Qt::EditRole).toString()==item->text(),"table model/edit data modified");
        check(delegate->displayText("0.0000070369",QLocale::c())=="0.0000070369","table small value changed");
        check(delegate->displayText("1000",QLocale::c())=="1000","integer zero removed");
        check(delegate->displayText("1.2000e-10",QLocale::c())=="1.2000e-10","scientific exponent modified");
        check(delegate->displayText("材料 1.0000",QLocale::c())=="材料 1.0000","text label modified");
        std::cout<<"Numeric display: coordinates, tiny values, precision, locale, units, editing, stepping and unchanged persistence passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
