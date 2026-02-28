# ChromaPrint3D 前端 UI 主题规范手册（约定版）

## 1. 文档属性

- 文档性质：规范手册（约定文件）。
- 使用目的：统一前端 UI 主题语言，作为后续设计评审与开发评审的共同依据。
- 明确排除：本手册不定义实施步骤、排期、负责人、开发流程、任务拆分或技术落地路径。

### 1.1 术语与约束级别

- 必须：强制要求，不满足即视为不合规。
- 应当：推荐要求，原则上应满足；有充分理由时可例外，并需在评审中说明。
- 可选：增强项，不影响基础合规性。

## 2. 适用范围

- 适用对象：`web/frontend` 下全部页面与通用组件。
- 适用主题：浅色（Light）与深色（Dark）双主题。
- 约束边界：新增界面与改动界面均须遵循本手册，不得形成页面级私有风格体系。

## 3. 设计目标

- 专业感：整体视觉保持简洁、克制、工具化、可信赖。
- 一致性：跨页面与跨组件视觉语言统一，避免风格漂移。
- 可读性：高密度参数与状态信息在长时使用场景下保持易读、易扫视。
- 可维护性：主题能力可扩展、可复用、可审查，避免样式资产碎片化。

## 4. 非目标

- 不涉及页面信息架构重设。
- 不涉及交互流程重构。
- 不涉及业务逻辑调整。
- 不涉及工程流程安排与实施计划。

## 5. 主题体系总则

- 双主题同构（必须）：Light 与 Dark 使用同一语义层，不允许语义缺失或语义错位。
- 语义优先（必须）：样式表达以语义槽位为准，不以页面场景命名替代语义。
- Token 唯一来源（必须）：颜色、字体、间距、圆角、阴影、动效应由统一约定驱动。
- 禁止硬编码（必须）：非实验场景不得在业务组件中直接写死视觉常量。
- 一致覆盖（应当）：同语义 UI 元素在不同页面中应保持同等视觉含义与状态反馈。

## 6. 双主题语义约定

### 6.1 语义分层

- 品牌层：主品牌色、强调色及其状态色。
- 中性层：文本、图标、边框、分隔、禁用态等非品牌语义。
- 背景层：页面底、容器底、悬浮层、高亮层。
- 状态层：成功、警告、错误、信息及其强调/弱化状态。
- 交互层：默认、悬停、激活、选中、焦点、禁用。

### 6.2 同构要求

- Light 与 Dark 必须具备完全一致的语义槽位集合。
- 任一语义槽位不得仅在单主题存在。
- 主题切换不得改变语义含义，仅允许改变视觉映射。

### 6.3 语义命名要求

- 命名必须表达“角色”而非“颜色值”，例如“主文本”“弱边框”“容器背景”。
- 同一语义仅允许一个规范名称，不允许同义重复命名。

## 7. 色彩约定

- 颜色分层（必须）：品牌色、中性色、状态色、背景层、边框层、交互层应可独立识别。
- 状态完备（必须）：默认、悬停、激活、禁用、选中、焦点状态均需有对应语义。
- 对比可读（必须）：正文与关键控件在双主题下均需满足可读性对比，不允许“视觉上大致可见”。
- 风格控制（应当）：浅色主题保持克制清爽，深色主题保持沉稳层次，不使用强噪声高饱和方案。
- 风险避免（应当）：避免将“颜色本身”作为唯一信息通道，状态识别应具备文本或形态辅助。

## 8. 排版约定

- 字号层级（必须）：标题、分组标题、正文、说明、辅助信息应有固定层级。
- 字重约定（必须）：强调信息使用有限档位字重，不得滥用粗体造成视觉拥挤。
- 行高约定（必须）：中文参数说明与长文本应连续可读，不出现密集断行压迫。
- 对齐约定（必须）：表单标签、输入区、说明文本应遵循统一对齐规则。
- 信息节奏（应当）：标题、段落、表单组间距应形成稳定阅读节奏。

## 9. 空间与形态约定

- 间距体系（必须）：使用统一尺度递进，不允许同层级出现无规则间距。
- 圆角体系（必须）：卡片、输入、按钮、标签等使用有限档位圆角。
- 阴影体系（应当）：仅保留必要层级阴影，避免多阴影叠加导致视觉噪声。
- 边框体系（必须）：边框强弱需对应信息层级，不得出现边界表达冲突。
- 密度一致（应当）：同级页面块的视觉密度应保持一致，避免局部过疏或过密。

## 10. 组件一致性约定

### 10.1 覆盖组件

- 按钮、输入框、选择器、开关、表单项、卡片、标签、告警、上传区、结果预览区。

### 10.2 一致性要求

- 同语义组件在不同页面必须保持统一尺寸体系与状态反馈逻辑。
- 高风险操作与主操作按钮必须具备稳定、可预期的视觉识别方式。
- 表单错误态、警告态、成功态必须采用统一语义表达，不允许页面自定义“类状态”。
- 加载、空态、失败态、完成态应统一视觉语义，避免用户学习成本叠加。

## 11. 可读性约定

- 信息优先级（必须）：主操作、主结果、辅助说明层级明确且稳定。
- 长表单可读（必须）：连续参数块应具备分组感，避免整页同噪声密度。
- 密集信息可扫视（必须）：关键值、单位、状态词应具备稳定视觉锚点。
- 长时使用友好（应当）：避免过高对比刺激与低对比疲劳并存。
- 文案承载（应当）：说明文本应简洁明确，避免同层级信息重复表达。

## 12. 可维护性约定

- 命名规范（必须）：主题语义命名统一、可扩展、可检索。
- 分层规范（必须）：基础语义、组件语义、页面特例边界清晰。
- 变更规范（必须）：主题调整优先通过统一约定收敛，不接受长期页面级修补。
- 兼容规范（应当）：新增主题能力不应破坏既有语义槽位稳定性。
- 审查规范（必须）：主题相关变更需依据本手册进行合规检查。

## 13. 合规检查清单

| 编号 | 检查项 | 判定标准 |
|---|---|---|
| C01 | 双主题同构 | Light/Dark 语义槽位完整一致，无缺失语义 |
| C02 | 语义优先 | 不存在以页面场景替代语义命名的情况 |
| C03 | 禁止硬编码 | 业务组件内无固定视觉常量（实验代码除外） |
| C04 | 色彩状态完备 | 默认/悬停/激活/禁用/选中/焦点语义齐全 |
| C05 | 对比可读 | 正文与关键控件可稳定辨识，无低可读区域 |
| C06 | 排版层级统一 | 标题、正文、说明、辅助信息层级一致 |
| C07 | 间距与圆角统一 | 同层级组件无无规则间距与形态漂移 |
| C08 | 组件跨页一致 | 同语义组件跨页面表现一致 |
| C09 | 参数区可扫视 | 高密度表单具备明确分组和视觉锚点 |
| C10 | 文档边界纯净 | 手册内容不混入实施流程、排期与执行路径 |

## 14. 合规结论规则

- 基础合规：C01-C10 全部通过。
- 条件合规：存在“应当”类例外时，需在评审记录中给出合理说明。
- 不合规：任一“必须”条款不满足。

## 15. 语义 Token 基线（v1）

> 本章节给出主题语义基线值，作为视觉统一的约定参考。命名遵循“语义优先”，不以具体页面命名。

### 15.1 Light 主题语义 Token

| 语义 | Token | 值 |
|---|---|---|
| 品牌主色 | `color.brand.primary` | `#0B7FCF` |
| 品牌主色悬停 | `color.brand.primaryHover` | `#0A73BC` |
| 品牌主色激活 | `color.brand.primaryActive` | `#0868AA` |
| 页面背景 | `color.bg.page` | `#F6F8FB` |
| 一级容器背景 | `color.bg.surface` | `#FFFFFF` |
| 二级容器背景 | `color.bg.surfaceMuted` | `#F9FBFE` |
| 悬浮层背景 | `color.bg.elevated` | `#FFFFFF` |
| 最高层背景 | `color.bg.overlay` | `#FFFFFF` |
| 主文本 | `color.text.primary` | `#1B2430` |
| 次文本 | `color.text.secondary` | `#4A5667` |
| 弱文本 | `color.text.tertiary` | `#6F7C8E` |
| 禁用文本 | `color.text.disabled` | `#98A3B3` |
| 强边框 | `color.border.strong` | `#CAD3DF` |
| 默认边框 | `color.border.default` | `#D8E0EA` |
| 弱边框 | `color.border.muted` | `#E6ECF3` |
| 分割线 | `color.border.divider` | `#E9EEF5` |
| 焦点环 | `color.focus.ring` | `#5AAEEA` |
| 信息态 | `color.state.info` | `#2C8FD6` |
| 成功态 | `color.state.success` | `#2C9A63` |
| 警告态 | `color.state.warning` | `#C58A18` |
| 错误态 | `color.state.error` | `#C74747` |

### 15.2 Dark 主题语义 Token（柔和深灰）

| 语义 | Token | 值 |
|---|---|---|
| 品牌主色 | `color.brand.primary` | `#3AA6F2` |
| 品牌主色悬停 | `color.brand.primaryHover` | `#56B4F4` |
| 品牌主色激活 | `color.brand.primaryActive` | `#2A98E6` |
| 页面背景 | `color.bg.page` | `#171B21` |
| 一级容器背景 | `color.bg.surface` | `#1E242C` |
| 二级容器背景 | `color.bg.surfaceMuted` | `#252D38` |
| 悬浮层背景 | `color.bg.elevated` | `#293240` |
| 最高层背景 | `color.bg.overlay` | `#303A49` |
| 主文本 | `color.text.primary` | `#E7EDF5` |
| 次文本 | `color.text.secondary` | `#C2CDDA` |
| 弱文本 | `color.text.tertiary` | `#97A6B8` |
| 禁用文本 | `color.text.disabled` | `#6F7D8F` |
| 强边框 | `color.border.strong` | `#465366` |
| 默认边框 | `color.border.default` | `#3C485A` |
| 弱边框 | `color.border.muted` | `#323D4D` |
| 分割线 | `color.border.divider` | `#303A49` |
| 焦点环 | `color.focus.ring` | `#6BC0F6` |
| 信息态 | `color.state.info` | `#56B4F4` |
| 成功态 | `color.state.success` | `#43B980` |
| 警告态 | `color.state.warning` | `#E0A63A` |
| 错误态 | `color.state.error` | `#E36A6A` |

### 15.3 交互态透明度约定

- `opacity.disabled`: `0.45`
- `opacity.hoverOverlay`: `0.06`
- `opacity.activeOverlay`: `0.12`
- `opacity.focusRing`: `0.32`

## 16. 组件 Token 基线（v1）

> 本章节定义组件层级基线值，匹配“桌面端、密度均衡、边框主导、中等圆角”的目标画像。

### 16.1 全局尺寸与形态

| 项目 | Token | 值 |
|---|---|---|
| 基础控件高度 | `size.control.md` | `40px` |
| 小控件高度 | `size.control.sm` | `32px` |
| 大控件高度 | `size.control.lg` | `48px` |
| 默认圆角 | `radius.md` | `8px` |
| 小圆角 | `radius.sm` | `6px` |
| 大圆角 | `radius.lg` | `12px` |
| 默认边框 | `border.width.default` | `1px` |
| 强调边框 | `border.width.strong` | `1.5px` |
| 全局间距基元 | `space.base` | `8px` |
| 默认动效时长 | `motion.duration.base` | `180ms` |
| 快速动效时长 | `motion.duration.fast` | `120ms` |
| 动效曲线 | `motion.easing.standard` | `cubic-bezier(0.2, 0, 0, 1)` |

### 16.2 按钮

| 项目 | Token | 值 |
|---|---|---|
| 高度（默认） | `button.height` | `40px` |
| 水平内边距 | `button.paddingX` | `16px` |
| 字重 | `button.fontWeight` | `600` |
| 圆角 | `button.radius` | `8px` |
| 主按钮边框 | `button.primary.borderWidth` | `1px` |
| 次按钮边框 | `button.secondary.borderWidth` | `1px` |
| 危险按钮边框 | `button.danger.borderWidth` | `1px` |
| 主按钮悬停亮度调整 | `button.primary.hoverDelta` | `+6%` |

### 16.3 输入类控件（Input/Select/Number）

| 项目 | Token | 值 |
|---|---|---|
| 高度（默认） | `input.height` | `40px` |
| 水平内边距 | `input.paddingX` | `12px` |
| 圆角 | `input.radius` | `8px` |
| 默认边框 | `input.borderWidth` | `1px` |
| 聚焦边框 | `input.focus.borderWidth` | `1.5px` |
| 聚焦环 | `input.focus.ring` | `2px` |
| 占位文本透明度 | `input.placeholder.opacity` | `0.72` |

### 16.4 卡片与面板

| 项目 | Token | 值 |
|---|---|---|
| 默认圆角 | `card.radius` | `8px` |
| 默认边框 | `card.borderWidth` | `1px` |
| 关键卡片边框 | `card.critical.borderWidth` | `1.5px` |
| 默认内边距 | `card.padding` | `16px` |
| 分组间距 | `card.sectionGap` | `16px` |
| 标题下间距 | `card.titleGap` | `12px` |

### 16.5 表单与参数区

| 项目 | Token | 值 |
|---|---|---|
| 表单项垂直间距 | `form.itemGapY` | `12px` |
| 标签与控件间距 | `form.labelGap` | `8px` |
| 分组标题字号 | `form.groupTitle.fontSize` | `14px` |
| 说明文字字号 | `form.help.fontSize` | `12px` |
| 说明文字行高 | `form.help.lineHeight` | `1.5` |
| 参数区最小点击目标 | `form.minHitArea` | `32px` |

### 16.6 告警、状态与反馈

| 项目 | Token | 值 |
|---|---|---|
| 状态条圆角 | `alert.radius` | `8px` |
| 状态条边框 | `alert.borderWidth` | `1px` |
| 状态条内边距 | `alert.padding` | `12px` |
| 加载转场时长 | `feedback.loading.transition` | `180ms` |
| 成功/错误反馈停留 | `feedback.toast.duration` | `2800ms` |

### 16.7 阴影（边框主导，轻阴影补充）

| 层级 | Token | 值 |
|---|---|---|
| 基础层 | `shadow.level1` | `0 1px 2px rgba(16,24,40,0.06)` |
| 悬浮层 | `shadow.level2` | `0 4px 12px rgba(16,24,40,0.10)` |
| 模态层 | `shadow.level3` | `0 12px 24px rgba(16,24,40,0.14)` |

## 17. Naive UI Theme Overrides 映射表（v1）

> 用于将本手册 token 语义映射到 Naive UI 主题字段。字段命名以当前 Naive UI 版本为准，若存在同义字段，以语义匹配优先。

### 17.1 全局 `common` 映射

| 手册 Token | Naive UI 字段（建议） | 说明 |
|---|---|---|
| `color.brand.primary` | `common.primaryColor` | 全局主色 |
| `color.brand.primaryHover` | `common.primaryColorHover` | 主色悬停 |
| `color.brand.primaryActive` | `common.primaryColorPressed` | 主色按下/激活 |
| `color.state.info` | `common.infoColor` | 信息态主色 |
| `color.state.success` | `common.successColor` | 成功态主色 |
| `color.state.warning` | `common.warningColor` | 警告态主色 |
| `color.state.error` | `common.errorColor` | 错误态主色 |
| `color.bg.page` | `common.bodyColor` | 页面背景 |
| `color.bg.surface` | `common.cardColor` | 卡片背景 |
| `color.bg.elevated` | `common.popoverColor` | 悬浮层背景 |
| `color.bg.overlay` | `common.modalColor` | 模态层背景 |
| `color.text.primary` | `common.textColorBase` / `common.textColor1` | 主文本 |
| `color.text.secondary` | `common.textColor2` | 次文本 |
| `color.text.tertiary` | `common.textColor3` | 弱文本 |
| `color.text.disabled` | `common.textColorDisabled` | 禁用文本 |
| `color.border.default` | `common.borderColor` | 默认边框 |
| `color.border.divider` | `common.dividerColor` | 分割线 |
| `radius.md` | `common.borderRadius` | 全局默认圆角 |
| `radius.sm` | `common.borderRadiusSmall` | 小圆角 |
| `motion.duration.base` | `common.cubicBezierEaseInOut`（语义对齐） | 动效基线曲线语义 |

### 17.2 Button 组件映射

| 手册 Token | Naive UI 字段（建议） | 说明 |
|---|---|---|
| `button.height` | `Button.heightMedium` | 默认按钮高度（40） |
| `size.control.sm` | `Button.heightSmall` | 小按钮高度 |
| `size.control.lg` | `Button.heightLarge` | 大按钮高度 |
| `button.radius` | `Button.borderRadiusMedium` | 默认按钮圆角 |
| `button.fontWeight` | `Button.fontWeight` | 按钮字重 |
| `border.width.default` | `Button.border` / `Button.borderMedium` | 边框宽度 |
| `color.brand.primary*` | `Button.colorPrimary*` | 主按钮色系 |

### 17.3 Input/Select 组件映射

| 手册 Token | Naive UI 字段（建议） | 说明 |
|---|---|---|
| `input.height` | `Input.heightMedium` / `Select.heightMedium` | 默认输入高度（40） |
| `size.control.sm` | `Input.heightSmall` / `Select.heightSmall` | 小尺寸输入高度 |
| `size.control.lg` | `Input.heightLarge` / `Select.heightLarge` | 大尺寸输入高度 |
| `input.radius` | `Input.borderRadius` / `Select.borderRadius` | 输入圆角 |
| `color.bg.surface` | `Input.color` / `Select.color` | 输入背景 |
| `color.border.default` | `Input.border` / `Select.border` | 默认边框色 |
| `color.brand.primary` | `Input.borderFocus` / `Select.borderFocus` | 聚焦边框色 |
| `color.focus.ring` | `Input.boxShadowFocus` / `Select.boxShadowFocus` | 焦点环 |
| `color.text.tertiary` | `Input.placeholderColor` / `Select.placeholderColor` | 占位文本 |
| `color.text.primary` | `Input.textColor` / `Select.textColor` | 输入文本色 |

### 17.4 Card/Alert/Tag 映射

| 手册 Token | Naive UI 字段（建议） | 说明 |
|---|---|---|
| `card.radius` | `Card.borderRadius` | 卡片圆角 |
| `card.borderWidth` + `color.border.default` | `Card.borderColor` / `Card.bordered` | 卡片边框语义 |
| `color.bg.surface` | `Card.color` | 卡片背景 |
| `color.text.primary` | `Card.titleTextColor` | 卡片标题 |
| `alert.radius` | `Alert.borderRadius` | 告警圆角 |
| `alert.borderWidth` | `Alert.border` | 告警边框 |
| `color.state.*` | `Alert.*Color` / `Tag.*Color` | 各状态颜色 |

### 17.5 Tabs/Layout 映射

| 手册 Token | Naive UI 字段（建议） | 说明 |
|---|---|---|
| `color.bg.page` | `Layout.color` | 布局主背景 |
| `color.bg.surface` | `Tabs.paneColor`（如版本支持） | 面板背景 |
| `color.text.primary` | `Tabs.tabTextColorActive` | 激活标签文本 |
| `color.text.secondary` | `Tabs.tabTextColor` | 非激活标签文本 |
| `color.brand.primary` | `Tabs.tabTextColorHover` / `Tabs.barColor` | 标签悬停与指示条 |
| `color.border.divider` | `Tabs.tabBorderColor` | 标签分隔线 |

## 18. 映射一致性检查

- 同一语义 token 在不同组件上应映射到一致视觉含义，不得出现“同 token 异语义”。
- 当 Naive UI 字段粒度不足时，优先保证语义一致，其次保证视觉近似。
- 若组件存在局部覆盖，局部值不得违背本手册第 5-12 章约定。
- 深色主题映射不得直接复用浅色色值，必须使用第 15 章 Dark 基线值。
