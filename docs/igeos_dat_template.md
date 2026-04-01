# IGEOS.dat 地应力回归流程控制文件模板

以下内容根据你给出的 `4.4` 说明整理，可直接作为 `IGEOS.dat` 的编写参考。

## 1) 文件头与默认路径（固定顺序）

```txt
'C:\\JIANKE\\PREP\\'        1
'ROOF.DAT'                      2
'FA_LEFT.DAT'                   3
'FA_RIGHT.DAT'                  4
'FA_FRONT.DAT'                  5
'FA_BACK.DAT'                   6
'0.95'                          7
'0'                             8
'0'                             9
'FTLOAD.DAT'                   10
'0'                            11
'   0.00000'                   12
```

> 说明：第 1~12 行通常保持和示例一致的结构，路径与文件名按工程实际替换。

## 2) 回归开关与测点信息

```txt
   1     NCHANGE
   1     1     1    MACHAN    MACHAN  II
   2    NUM_GEO
XP       YP      ZP       SGX      SGY      SGZ      TYZ      TZX      TXY      II
120.000  80.000  1650.000  8.50E+06 9.10E+06 1.20E+07 0.00E+00 0.00E+00 0.00E+00   1
260.000 140.000  1600.000  8.00E+06 8.80E+06 1.15E+07 0.00E+00 0.00E+00 0.00E+00   2
```

- 三维问题（`NDIM=3`）每个测点格式：
  `XP YP ZP SGX SGY SGZ TYZ TZX TXY II`
- 二维问题（`NDIM=2`）每个测点格式：
  `XP YP SGX SGY TXY II`
- `NUM_GEO` 为实测地应力点总数。

## 3) 侧立面回归参数

```txt
   4    NFA_GEO
FNAME            IDIR       HEIGHT        GAMA            PGROUT           II
'FA_LEFT.DAT'      1      1680.0000     2650.0000      0.000000E+00       1
'FA_RIGHT.DAT'     1      1680.0000     2650.0000      0.000000E+00       2
'FA_FRONT.DAT'     2      1680.0000     2650.0000      0.000000E+00       3
'FA_BACK.DAT'      2      1680.0000     2650.0000      0.000000E+00       4
```

- `NFA_GEO`：参与地应力回归计算的侧立面数。
- `IDIR`：该面的法向轴号（常见：1→X，2→Y，3→Z，具体以程序约定为准）。
- `HEIGHT`：侧压力起算高程。
- `GAMA`：容重（kg/m³）。
- `PGROUT`：矩形荷载（Pa）。
- `II`：该面记录编号。

## 4) 可直接使用的完整示例（3D）

```txt
'C:\\JIANKE\\PREP\\'        1
'ROOF.DAT'                      2
'FA_LEFT.DAT'                   3
'FA_RIGHT.DAT'                  4
'FA_FRONT.DAT'                  5
'FA_BACK.DAT'                   6
'0.95'                          7
'0'                             8
'0'                             9
'FTLOAD.DAT'                   10
'0'                            11
'   0.00000'                   12
   1     NCHANGE
   1     1     1    MACHAN    MACHAN  II
   2    NUM_GEO
XP       YP      ZP       SGX      SGY      SGZ      TYZ      TZX      TXY      II
120.000  80.000  1650.000  8.50E+06 9.10E+06 1.20E+07 0.00E+00 0.00E+00 0.00E+00   1
260.000 140.000  1600.000  8.00E+06 8.80E+06 1.15E+07 0.00E+00 0.00E+00 0.00E+00   2
   4    NFA_GEO
FNAME            IDIR       HEIGHT        GAMA            PGROUT           II
'FA_LEFT.DAT'      1      1680.0000     2650.0000      0.000000E+00       1
'FA_RIGHT.DAT'     1      1680.0000     2650.0000      0.000000E+00       2
'FA_FRONT.DAT'     2      1680.0000     2650.0000      0.000000E+00       3
'FA_BACK.DAT'      2      1680.0000     2650.0000      0.000000E+00       4
```
