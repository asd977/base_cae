"""Render the actual generated TIN, not a separately reconstructed illustration."""
from pathlib import Path
import argparse
import json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties
from matplotlib.colors import Normalize

parser = argparse.ArgumentParser()
parser.add_argument('project', type=Path)
args = parser.parse_args()
project = args.project
data = json.loads((project / 'terrain_preview_data.json').read_text(encoding='utf-8'))
sources = json.loads((project / 'terrain_sources.json').read_text(encoding='utf-8'))['sources']
points = np.asarray(data['points'])
triangles = np.asarray(data['triangles'])
font_path = Path('C:/Windows/Fonts/msyh.ttc')
font = FontProperties(fname=str(font_path)) if font_path.exists() else FontProperties()
plt.rcParams['axes.unicode_minus'] = False
fig = plt.figure(figsize=(13, 6.3), layout='constrained')
left = fig.add_subplot(121)
right = fig.add_subplot(122, projection='3d')
cmap = plt.get_cmap('terrain')
norm = Normalize(100, 150)
for source in sources:
    xyz = np.asarray(source['points'])
    if source['closed']:
        xyz = np.vstack([xyz, xyz[0]])
    x, y, z = xyz[:, 0]-500000, xyz[:, 1]-3000000, xyz[:, 2]
    if source['role'] == 3:
        left.scatter(x, y, marker='^', c='#202f46', s=36, zorder=5)
        left.annotate('150', (x[0], y[0]), xytext=(5, 5), textcoords='offset points')
    else:
        left.plot(x, y, color='#202f46' if source['role'] == 1 else cmap(norm(z[0])), lw=1.7)
        left.annotate(str(int(z[0])), (x[0], y[0]), xytext=(3, 4), textcoords='offset points')
left.set_aspect('equal')
left.set_title('输入：等高线、范围边界与山顶点', fontproperties=font, fontsize=13, loc='left')
left.set_xlabel('东向偏移（米）', fontproperties=font)
left.set_ylabel('北向偏移（米）', fontproperties=font)
left.grid(alpha=.15)
surface = right.plot_trisurf(points[:, 0]-500000, points[:, 1]-3000000, points[:, 2], triangles=triangles,
                           cmap=cmap, norm=norm, edgecolor=(.15, .23, .28, .22), linewidth=.2, antialiased=True)
right.set_title('输出：实际生成的三角地形面', fontproperties=font, fontsize=13, loc='left')
right.set_xlabel('东向（米）', fontproperties=font)
right.set_ylabel('北向（米）', fontproperties=font)
right.set_zlabel('')
right.set_box_aspect((200, 160, 50))
right.view_init(elev=29, azim=-55)
bar = fig.colorbar(surface, ax=right, shrink=.5, pad=.15)
bar.set_label('高程（米）', fontproperties=font)
fig.suptitle('等高线地形示例 · 545 顶点 / 944 三角形 · 高程 100–150 米', fontproperties=font, fontsize=16)
fig.savefig(project / 'terrain_example.png', dpi=160)
