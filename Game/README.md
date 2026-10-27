# 长夜守灯 · Unreal Engine 工程

基于 Unreal Engine 5.8 的 Windows 单机战术守线 Demo。默认启动九关活动地图 `/Game/Maps/L_Campaign`。

## 工程结构

| 目录 | 内容 |
| --- | --- |
| Source/LongNight | 战斗、任务、协奏、存档、教学与 UMG 界面实现 |
| Data/Campaign | 九关玩法参数、阵容与验证配置 |
| Content | 地图、角色、材质、动画、音效和运行数据 |
| Config | 渲染、启动地图、输入和打包配置 |
| Tools | 构建、场景回归与玩法验证工具 |
| Tests | 独立规则测试 |

## 打开与构建

安装 Unreal Engine 5.8 和带 C++ 游戏开发组件的 Visual Studio，右键 `LongNight.uproject` 生成工程文件，编译 `LongNightEditor` 后打开项目。发布版本使用 UE 5.8.3 构建。

PowerShell 构建示例（EngineRoot 填写本机引擎目录）：

```powershell
./Tools/Build.ps1 -EngineRoot 'D:/Epic/UE_5.8'
```

输出位于 `Builds/NineStages/Windows`。进入游戏后可选择正常体验或评审模式；正常体验按进度解锁，评审模式可直接选择关卡和波次，二者分别保存。

## 玩法验证

构建完成后执行 `Tools/VerifyCampaignPackage.ps1 -Mode All -FormationPlan review_route` 检查九关流程；`-Mode State`、`Save`、`UI`、`Variants` 分别验证状态规则、存档、界面及险境变体。`Tools/VerifyCampaignScene.ps1` 验证进入关卡及连续切关后的场景网格、碰撞和渲染。

设计正文及原始排版文档见仓库主页和 `../documents`。工程配置记录 Demo 的实际运行参数。
