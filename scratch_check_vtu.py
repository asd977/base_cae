import json
with open(r'C:\test\rock\12333\工程数据\preprocessing.json', 'r', encoding='utf-8') as f:
    d = json.load(f)
sources = d['geom_items'][0]['state']['sources']
print('Total sources:', len(sources))
single_pts = 0
multi_pts = 0
total_pts = 0
for s in sources:
    pts = s.get('points', [])
    total_pts += len(pts)
    if len(pts) == 1:
        single_pts += 1
    else:
        multi_pts += 1
print(f'Single pt sources: {single_pts}, Multi pt sources: {multi_pts}, Total pts: {total_pts}')
