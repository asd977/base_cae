# 网格导入能力统一方案（iin / Flac3D / msh）

## 1. 现状与核心矛盾

当前系统导入链路已经能读取三类网格：

- `iinFile(.dat)`：可用于材料设置、网格编辑；且天然包含 `KETYPE/MATYPE/LAYER` 等求解必需语义。
- `Flac3D(.flac3d/.f3grid)`：已具备分组信息（Group），但尚未打通到统一的边界/材料配置入口。
- `MshFile(.msh)`：已支持批量边界条件（Physical 分组），但材料分配工作流未统一。

问题本质不是“文件能否导入”，而是“导入后语义是否统一到同一套中间模型”。

---

## 2. 目标（建议采用）

把 `iin` 从“主数据格式”降级为“求解导出格式”，将产品内部统一到一个**语义网格模型**：

- 几何/拓扑：点、单元、维度、单元类型
- 体分组（Volume Group）
- 边界分组（Boundary Group：点/线/面）
- 材料分组（Material Region）
- 分层编号（Layer No.，与体分组/域分组一一对应）

UI 功能（批量边界、材料设置、网格编辑）全部只依赖这套语义模型，不直接依赖源文件类型。

---

## 3. 落地原则

1. **导入器负责“翻译语义”**：`iin/flac3d/msh` 都要尽可能产出统一字段。
2. **业务层禁止判断文件类型**：材料/边界模块不能写 `if (ext==...)`。
3. **求解器导出最后一步才转 iin**：避免 iin 语义反向绑架全链路。

---

## 4. 统一数据约定（建议）

在 `vtkUnstructuredGrid` 上固定这些 CellData / FieldData 键：

### 4.1 CellData（每个单元）

- `MATYPE`：材料号（int，默认 1）
- `LAYER`：层号（int，默认 1；填筑层场景下该编号即体分组/域分组编号）
- `KETYPE`：单元类型编码（int）
- `RegionId`：体域编号（int，导入器生成）
- `RegionName`：体域名（string，优先源分组名）

### 4.2 Boundary Map（推荐放 FieldData + 运行时索引）

- `BndGroupNames[]`
- `BndGroupDims[]`（0/1/2）
- `BndGroupPointIds[]`（扁平索引）
- `BndGroupOffsets[]`

### 4.3 Material Group（可选增强）

- `MatGroupName`（string）
- `MatGroupId`（int）

> 如果源文件无材料分组，就先由体分组/Physical 分组映射到材料候选分组。

---

## 5. 三种格式的映射策略

## 5.1 iin

- 直接读取 `MATYPE/LAYER/KETYPE`。
- 将现有网格类型分组、材料类型分组映射到 `RegionName/MatGroup`；
- 填筑层分层编号直接作为体分组（域分组）编号写入 `RegionId`，并同步到 `LAYER`（保持两者一致）。

## 5.2 msh

- `PhysicalNames` 的 dim=3 作为体分组（Region）。
- dim=2/1/0 作为边界分组（Boundary Group）。
- 若无显式材料信息：
  - 先把体分组映射成可选材料分组；
  - 支持在 UI 中“一键按 Region 批量赋 MATYPE”。

## 5.3 Flac3D

- Zone Group -> 体分组（Region）。
- Face/Edge/Node Group（若文件存在）-> 边界分组。
- Zone Group 同时提供材料候选分组（与 msh 一致）。

---

## 6. 功能层重构建议

## 6.1 边界条件模块

边界条件页面仅消费统一 `Boundary Group`，不关心来源 msh/flac3d/iin。

## 6.2 材料模块

新增两个入口：

1. **按 MATYPE 绑定材料**（保留现有能力）
2. **按 Region/Group 批量赋 MATYPE**（新能力，给 msh/flac3d 补齐）

流程：先按组生成 MATYPE，再沿用现有材料绑定能力。

## 6.3 网格编辑模块

编辑后的 `MATYPE/LAYER/Region` 修改统一回写到 VTK 数组；导出 iin 时再编码。

---

## 7. 实施顺序（两周可交付的现实版本）

### Phase-1（先打通可用）

1. `MeshGridIO` 三个导入器全部保证输出统一基础数组：`MATYPE/LAYER/KETYPE/RegionId/RegionName`。
2. 对 `msh/flac3d` 增加“按分组批量赋 MATYPE”工具。
3. 材料管理器复用现有 `MATYPE` 工作流，不改求解导出。

### Phase-2（体验统一）

1. 边界条件模块改为统一读取 `Boundary Group`。
2. UI 显示“分组来源与映射关系”（便于用户核查）。
3. 项目保存中持久化“Group->MATYPE”映射。

### Phase-3（架构收敛）

1. 引入 `MeshSemanticModel`（可选独立类），减少各模块直接操作 VTK 细节。
2. iin 仅作为求解导出格式，不再作为功能判断依据。

---

## 8. 最小验收标准（建议）

- 导入 `msh` 后：
  - 可看到体分组；
  - 可按组一键赋 MATYPE；
  - 材料库可正常绑定并导出。
- 导入 `flac3d` 后：
  - 可看到 zone group；
  - 可按组赋 MATYPE；
  - 可进行边界批量设置（至少面组）。
- 导入 `iin` 后：
  - 现有材料、网格编辑能力不回退。

---

## 9. 给当前问题的直接建议

你现在最该做的不是继续给每种格式单独“补功能”，而是先做一层统一语义：

1. 统一分组模型（体/边界/材料候选/层）。
2. 先补 `msh + flac3d` 的“按组赋 MATYPE”。
3. 材料模块只认 `MATYPE`，不认文件来源。
4. 最后把 iin 固定为导出格式。

这样你后面的“边界条件批量设置 + 网格编辑 + 材料设置”会在三种网格上一次打通，不会越改越散。
