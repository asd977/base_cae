"""Render the actual generated terrain and its logical contour domains."""
import argparse
import json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
from matplotlib.patches import Patch
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('project',type=Path)
parser.add_argument('--output',type=Path)
args=parser.parse_args()
data=json.loads((args.project/'terrain_preview_data.json').read_text(encoding='utf-8'))
sources=json.loads((args.project/'terrain_sources.json').read_text(encoding='utf-8'))['sources']
xyz=np.asarray(data['points']);triangles=np.asarray(data['triangles']);ids=np.asarray(data['triangleDomains'])
local=xyz.copy();local[:,0]-=500000;local[:,1]-=3000000
plt.rcParams.update({'font.family':'Microsoft YaHei','axes.unicode_minus':False,'font.size':11})
colors=np.array(['#b5dca9','#85c69a','#63b1b5','#d7ba78','#c4916a'])
fig=plt.figure(figsize=(13.5,7.5),facecolor='white')
left=fig.add_axes([.055,.20,.41,.65]);right=fig.add_axes([.50,.19,.46,.65],projection='3d')
left.add_collection(PolyCollection(local[triangles,:2],facecolors=colors[ids-1],edgecolors='none',linewidths=0,antialiased=False))
right.add_collection3d(Poly3DCollection(local[triangles],facecolors=colors[ids-1],edgecolors='none',linewidths=0,antialiased=False,shade=False))
for source in sources:
    p=np.array(source['points'],dtype=float);p[:,0]-=500000;p[:,1]-=3000000
    if len(p)==1:
        left.scatter(p[0,0],p[0,1],s=32,color='#713e2a',zorder=6)
        left.annotate('山顶点 Z=150',(p[0,0],p[0,1]),xytext=(-18,-28),textcoords='offset points',fontsize=10)
        right.scatter(p[:,0],p[:,1],p[:,2],s=20,color='#713e2a')
        continue
    p=np.vstack([p,p[0]])
    left.plot(p[:,0],p[:,1],color='#27526a',lw=1.15)
    right.plot(p[:,0],p[:,1],p[:,2],color='#27526a',lw=.8)
    left.text(p[0,0]+2,p[0,1]+1,str(int(p[0,2])),color='#173647',fontsize=10)
left.set(xlim=(-112,116),ylim=(-93,93),xlabel='X − 500000（米）',ylabel='Y − 3000000（米）')
left.set_aspect('equal');left.spines[['top','right']].set_visible(False)
left.set_title('输入等高线（蓝线）；底色示意分域',loc='left',fontweight='bold',pad=15)
right.set(xlim=(-105,105),ylim=(-85,85),zlim=(98,153),xlabel='X（局部，米）',ylabel='Y（局部，米）',zlabel='高程 Z（米）')
right.set_box_aspect((2,1.6,.72));right.view_init(elev=27,azim=-62)
right.set_title('构建后：沿等高线保留 5 个面域',fontweight='bold',pad=16)
right.xaxis.pane.fill=False;right.yaxis.pane.fill=False;right.zaxis.pane.fill=False
fig.suptitle('等高线约束形状，面域保留边界，计算网格另行划分',x=.055,ha='left',fontsize=18,fontweight='bold',y=.97)
fig.legend([Patch(facecolor=colors[i]) for i in range(5)],
           [f'域 {i+1}：{100+10*i}～{110+10*i} 米' for i in range(5)],
           ncol=5,loc='lower center',bbox_to_anchor=(.5,.11),frameon=False,fontsize=10)
fig.text(.055,.065,'同一条等高线上的 Z 相同；5 个高程面域默认使用相同材料，不能据高程推断土层。',fontsize=11,color='#294b5e')
fig.text(.055,.028,'这是合成山丘示例。构面内部面片与计算单元不是同一层次；图中没有把内部三角边作为分域边界。',fontsize=10,color='#667580')
output=args.output or args.project/'terrain_domains_explained.png'
fig.savefig(output,dpi=160,facecolor='white')
print(output)
