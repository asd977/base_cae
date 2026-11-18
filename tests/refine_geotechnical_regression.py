"""Repair explicit configuration gaps in a generated suite, preserving a backup."""
import argparse
import datetime
import shutil
from pathlib import Path
from build_geotechnical_regression import read,write,child

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('suite',type=Path);a=parser.parse_args()
    backup=a.suite/'refinements'/datetime.datetime.now().strftime('%Y%m%d_%H%M%S');backup.mkdir(parents=True)
    changes=[];coverage=read(a.suite/'coverage.json')
    for family in ['坝_多工况回归','洞室_多工况回归','边坡_多工况回归']:
        path=a.suite/family/'工程数据/solver.json';shutil.copy2(path,backup/(family+'.solver.json'));o=read(path)
        by_id={n['title'].split('－')[0]:n for n in o['items']}
        for n in o['items']:
            id=n['title'].split('－')[0]
            if id=='D13':
                state=child(n,'FillStorageCycle')['state'];state['values'].update(IFWET='0',IFDROP='0');state['wet_table']=[]
                for row in state['circle_table']:row[8]='1';row[9]='0'
                changes.append({'case':id,'change':'关闭与结冰冲突的浸润、骤降和蓄水期流变，保留有效蓄水阶段'})
            if id in ['D15','D16']:
                state=child(n,'DamDynamicAnalysis')['state'];state['values']['IFMODE']='1'
                state['modal_order']=int(n['state']['base_values'][29])
                changes.append({'case':id,'change':'启用模态后再做时程，阶数与总控保持一致，由真实计算取得频率'})
            if id=='D14':
                source=by_id['D02'];state=child(n,'FillStorageCycle')['state']
                state.update(seepage_source_uuid=source['uuid'],seepage_source_name=source['title'])
                state['circle_table'][-1][4]=str(source['state']['upstream_water_level'])
                for project in coverage['projects']:
                    for case in project['cases']:
                        if case['id']=='D14':case.update(depends_on=['D02'],timeout_seconds=600)
                changes.append({'case':id,'change':'使用D02已收敛的定水头稳态初始场125.5米；定通量场的水位字段不代表施加水头'})
            if id in ['C06','C07','C08']:
                child(n,'CaveSupportComputation')['state']['line_23']='2' if id=='C08' else '1'
                changes.append({'case':id,'change':'固定边界动力采用面力卸载，单元卸载后动力的原生未分配数组问题由P07保留验证'})
            if id=='S03':
                n['state']['base_values'][2]='3';n['state']['strength_reserve']=[{'factor':'1','ifpr':'1'},{'factor':'1.05','ifpr':'1'}]
                changes.append({'case':id,'change':'原生仅NSTEP>1输出带系数文件；计算1.0/1.05两个真实状态，模态读取其中1.0的CZ100CB100结果'})
            if id in ['S02','S06']:
                n['state']['base_values'][5]='100'
                changes.append({'case':id,'change':'提高静力迭代上限至100；逐步检查完成时残差，不把耗尽迭代记为收敛'})
        write(path,o)
    write(a.suite/'coverage.json',coverage)
    write(backup/'changes.json',changes);print(backup)

if __name__=='__main__':main()
