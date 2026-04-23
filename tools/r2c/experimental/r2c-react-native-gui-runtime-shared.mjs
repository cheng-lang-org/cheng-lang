function chengStr(text) {
  return JSON.stringify(String(text || ''));
}

function chengBool(value) {
  return value ? 'true' : 'false';
}

function chengInt(value) {
  const parsed = Number.parseInt(String(value ?? 0), 10);
  return Number.isFinite(parsed) ? String(parsed) : '0';
}

function normalizedItem(item) {
  return {
    id: String(item?.id || ''),
    sourceNodeId: String(item?.source_node_id || ''),
    kind: String(item?.kind || ''),
    x: Number.parseInt(String(item?.x ?? 0), 10) || 0,
    y: Number.parseInt(String(item?.y ?? 0), 10) || 0,
    width: Number.parseInt(String(item?.width ?? 0), 10) || 0,
    height: Number.parseInt(String(item?.height ?? 0), 10) || 0,
    text: String(item?.text || ''),
    detailText: String(item?.detail_text || ''),
    zIndex: Number.parseInt(String(item?.z_index ?? 0), 10) || 0,
    planRole: String(item?.plan_role || ''),
    layer: String(item?.layer || ''),
    column: Number.parseInt(String(item?.column ?? 0), 10) || 0,
    columnSpan: Number.parseInt(String(item?.column_span ?? 0), 10) || 0,
    interactive: Boolean(item?.interactive),
    synthetic: Boolean(item?.synthetic),
    visualRole: String(item?.visual_role || ''),
    density: String(item?.density || ''),
    prominence: String(item?.prominence || ''),
    accentTone: String(item?.accent_tone || ''),
    sourceModulePath: String(item?.source_module_path || ''),
    sourceComponentName: String(item?.source_component_name || ''),
    sourceLine: Number.parseInt(String(item?.source_line ?? 0), 10) || 0,
    backgroundColor: String(item?.background_color || ''),
    borderColor: String(item?.border_color || ''),
    textColor: String(item?.text_color || ''),
    detailColor: String(item?.detail_color || ''),
    fontSize: Number.parseInt(String(item?.font_size ?? 0), 10) || 0,
    fontWeight: String(item?.font_weight || ''),
    cornerRadius: Number.parseInt(String(item?.corner_radius ?? 0), 10) || 0,
    stretchX: Boolean(item?.stretch_x),
    stretchY: Boolean(item?.stretch_y),
  };
}

function renderRuntimeItemFunction(item, index) {
  const normalized = normalizedItem(item);
  return `fn r2cNativeGuiItem_${index}(): R2cNativeGuiItem =
    var out: R2cNativeGuiItem
    out.id = ${chengStr(normalized.id)}
    out.sourceNodeId = ${chengStr(normalized.sourceNodeId)}
    out.kind = ${chengStr(normalized.kind)}
    out.x = ${chengInt(normalized.x)}
    out.y = ${chengInt(normalized.y)}
    out.width = ${chengInt(normalized.width)}
    out.height = ${chengInt(normalized.height)}
    out.text = ${chengStr(normalized.text)}
    out.detailText = ${chengStr(normalized.detailText)}
    out.zIndex = ${chengInt(normalized.zIndex)}
    out.planRole = ${chengStr(normalized.planRole)}
    out.layer = ${chengStr(normalized.layer)}
    out.column = ${chengInt(normalized.column)}
    out.columnSpan = ${chengInt(normalized.columnSpan)}
    out.interactive = ${chengBool(normalized.interactive)}
    out.synthetic = ${chengBool(normalized.synthetic)}
    out.visualRole = ${chengStr(normalized.visualRole)}
    out.density = ${chengStr(normalized.density)}
    out.prominence = ${chengStr(normalized.prominence)}
    out.accentTone = ${chengStr(normalized.accentTone)}
    out.sourceModulePath = ${chengStr(normalized.sourceModulePath)}
    out.sourceComponentName = ${chengStr(normalized.sourceComponentName)}
    out.sourceLine = ${chengInt(normalized.sourceLine)}
    out.backgroundColor = ${chengStr(normalized.backgroundColor)}
    out.borderColor = ${chengStr(normalized.borderColor)}
    out.textColor = ${chengStr(normalized.textColor)}
    out.detailColor = ${chengStr(normalized.detailColor)}
    out.fontSize = ${chengInt(normalized.fontSize)}
    out.fontWeight = ${chengStr(normalized.fontWeight)}
    out.cornerRadius = ${chengInt(normalized.cornerRadius)}
    out.stretchX = ${chengBool(normalized.stretchX)}
    out.stretchY = ${chengBool(normalized.stretchY)}
    return out`;
}

export function renderNativeGuiRuntimeMainCheng({ runtimeModule }) {
  return `import ${runtimeModule} as runtime

fn main(): int32 =
    return runtime.r2cNativeGuiRuntimeMain()
`;
}

export function renderNativeGuiRuntimeCheng({
  execIoModule,
  nativeLayoutPlan,
  previewFields,
  theme,
}) {
  const items = Array.isArray(nativeLayoutPlan?.items) ? nativeLayoutPlan.items : [];
  const itemFunctions = items.map((item, index) => renderRuntimeItemFunction(item, index));
  const itemChunkSize = 32;
  const itemChunks = [];
  for (let start = 0; start < items.length; start += itemChunkSize) {
    itemChunks.push(items.slice(start, start + itemChunkSize));
  }
  const itemChunkFunctions = itemChunks.map((chunk, chunkIndex) => {
    const startIndex = chunkIndex * itemChunkSize;
    const chunkBody = chunk.map((_, offset) => {
      const itemIndex = startIndex + offset;
      return `    let item_${itemIndex} = r2cNativeGuiItem_${itemIndex}()\n    add(out, item_${itemIndex})`;
    }).join('\n');
    return `fn r2cNativeGuiAppendItemsChunk_${chunkIndex}(out: var R2cNativeGuiItem[]) =
${chunkBody}
    return`;
  });
  const itemChunkCalls = itemChunks
    .map((_, chunkIndex) => `    r2cNativeGuiAppendItemsChunk_${chunkIndex}(out)`)
    .join('\n');
  const title = String(previewFields?.window_title || 'cheng_gui_preview');
  const routeState = String(previewFields?.route_state || 'home_default');
  const entryModule = String(previewFields?.entry_module || 'entry_module');
  const baseWindowWidth = Number.parseInt(String(nativeLayoutPlan?.window_width ?? 390), 10) || 390;
  const baseWindowHeight = Number.parseInt(String(nativeLayoutPlan?.window_height ?? 844), 10) || 844;
  const baseContentHeight = Number.parseInt(String(nativeLayoutPlan?.content_height ?? baseWindowHeight), 10) || baseWindowHeight;
  const baseScrollHeight = Number.parseInt(String(nativeLayoutPlan?.scroll_height ?? 0), 10) || 0;
  const backgroundTop = String(theme?.background_top || '#f2f7fd');
  const backgroundBottom = String(theme?.background_bottom || '#e6eefb');
  const panelBackground = String(theme?.panel_background || '#ffffff');
  const panelShadow = String(theme?.panel_shadow || '#cddaf0');
  const borderColor = String(theme?.border_color || '#d5e2f0');
  const accentColor = String(theme?.accent_color || '#3b82f6');
  const accentSoft = String(theme?.accent_soft || '#dbeafe');
  const textPrimary = String(theme?.text_primary || '#102a43');
  const textMuted = String(theme?.text_muted || '#6b7f95');
  const focusColor = '#f59e0b';

  const head = `import ${execIoModule} as exec_io
import std/json as json
import std/strings as strings
import cheng/core/tooling/path as chengpath

@importc("driver_c_read_flag_value_bridge")
fn r2cNativeGuiReadFlagValueBridge(key: str, outValue: var str): bool

@importc("driver_c_read_int32_flag_or_default_bridge")
fn r2cNativeGuiReadInt32FlagOrDefaultBridge(key: str, defaultValue: int32): int32

@importc("cheng_quote_json_text_bridge")
fn r2cNativeGuiQuoteJsonTextBridge(text: str): str

type
    R2cNativeGuiItem =
        id: str
        sourceNodeId: str
        kind: str
        x: int32
        y: int32
        width: int32
        height: int32
        text: str
        detailText: str
        zIndex: int32
        planRole: str
        layer: str
        column: int32
        columnSpan: int32
        interactive: bool
        synthetic: bool
        visualRole: str
        density: str
        prominence: str
        accentTone: str
        sourceModulePath: str
        sourceComponentName: str
        sourceLine: int32
        backgroundColor: str
        borderColor: str
        textColor: str
        detailColor: str
        fontSize: int32
        fontWeight: str
        cornerRadius: int32
        stretchX: bool
        stretchY: bool

    R2cNativeGuiState =
        windowTitle: str
        routeState: str
        entryModule: str
        hostAbiFirstBatchReady: bool
        hostAbiFeatureHitsCsv: str
        hostAbiMissingFeaturesCsv: str
        storageHostReady: bool
        fetchHostReady: bool
        customEventHostReady: bool
        resizeObserverHostReady: bool
        windowWidth: int32
        windowHeight: int32
        contentHeight: int32
        scrollHeight: int32
        scrollOffsetY: int32
        clickCount: int32
        resizeCount: int32
        scrollCount: int32
        keyCount: int32
        textCount: int32
        focusCount: int32
        hasLastClick: bool
        lastClickX: int32
        lastClickY: int32
        selectedItemId: str
        selectedSourceNodeId: str
        selectedSourceModulePath: str
        selectedSourceComponentName: str
        selectedSourceLine: int32
        selectedItemInteractive: bool
        selectedItemKind: str
        selectedItemPlanRole: str
        selectedItemVisualRole: str
        selectedItemX: int32
        selectedItemY: int32
        selectedItemWidth: int32
        selectedItemHeight: int32
        focusedItemId: str
        typedText: str
        lastKey: str
        visibleLayoutItemCount: int32

var r2cNativeGuiCatalogLoadAttempted: bool
var r2cNativeGuiCatalogItemsReady: bool
var r2cNativeGuiCatalogItemsCache: R2cNativeGuiItem[]

fn r2cNativeGuiItemZero(): R2cNativeGuiItem =
    var out: R2cNativeGuiItem
    out.id = ""
    out.sourceNodeId = ""
    out.kind = ""
    out.x = 0
    out.y = 0
    out.width = 0
    out.height = 0
    out.text = ""
    out.detailText = ""
    out.zIndex = 0
    out.planRole = ""
    out.layer = ""
    out.column = 0
    out.columnSpan = 0
    out.interactive = false
    out.synthetic = false
    out.visualRole = ""
    out.density = ""
    out.prominence = ""
    out.accentTone = ""
    out.sourceModulePath = ""
    out.sourceComponentName = ""
    out.sourceLine = 0
    out.backgroundColor = ""
    out.borderColor = ""
    out.textColor = ""
    out.detailColor = ""
    out.fontSize = 0
    out.fontWeight = ""
    out.cornerRadius = 0
    out.stretchX = false
    out.stretchY = false
    return out

fn r2cNativeGuiStateZero(): R2cNativeGuiState =
    var out: R2cNativeGuiState
    out.windowTitle = ""
    out.routeState = ""
    out.entryModule = ""
    out.hostAbiFirstBatchReady = false
    out.hostAbiFeatureHitsCsv = ""
    out.hostAbiMissingFeaturesCsv = ""
    out.storageHostReady = false
    out.fetchHostReady = false
    out.customEventHostReady = false
    out.resizeObserverHostReady = false
    out.windowWidth = 0
    out.windowHeight = 0
    out.contentHeight = 0
    out.scrollHeight = 0
    out.scrollOffsetY = 0
    out.clickCount = 0
    out.resizeCount = 0
    out.scrollCount = 0
    out.keyCount = 0
    out.textCount = 0
    out.focusCount = 0
    out.hasLastClick = false
    out.lastClickX = 0
    out.lastClickY = 0
    out.selectedItemId = ""
    out.selectedSourceNodeId = ""
    out.selectedSourceModulePath = ""
    out.selectedSourceComponentName = ""
    out.selectedSourceLine = 0
    out.selectedItemInteractive = false
    out.selectedItemKind = ""
    out.selectedItemPlanRole = ""
    out.selectedItemVisualRole = ""
    out.selectedItemX = 0
    out.selectedItemY = 0
    out.selectedItemWidth = 0
    out.selectedItemHeight = 0
    out.focusedItemId = ""
    out.typedText = ""
    out.lastKey = ""
    out.visibleLayoutItemCount = 0
    return out`;

  const itemsBlock = `${itemChunkFunctions.join('\n\n')}

fn r2cNativeGuiDefaultItems(): R2cNativeGuiItem[] =
    var out: R2cNativeGuiItem[]
${itemChunkCalls}
    return out`;

  const tail = `fn r2cNativeGuiBaseWindowWidth(): int32 =
    return ${chengInt(baseWindowWidth)}

fn r2cNativeGuiBaseWindowHeight(): int32 =
    return ${chengInt(baseWindowHeight)}

fn r2cNativeGuiBaseContentHeight(): int32 =
    return ${chengInt(baseContentHeight)}

fn r2cNativeGuiBaseScrollHeight(): int32 =
    return ${chengInt(baseScrollHeight)}

fn r2cNativeGuiBaseTitle(): str =
    return ${chengStr(title)}

fn r2cNativeGuiBaseRouteState(): str =
    return ${chengStr(routeState)}

fn r2cNativeGuiBaseEntryModule(): str =
    return ${chengStr(entryModule)}

fn r2cNativeGuiHostAbiFeatureHitsCsv(): str =
    return ${chengStr(Array.isArray(theme?.host_abi_feature_hits) ? theme.host_abi_feature_hits.join(',') : '')}

fn r2cNativeGuiHostAbiMissingFeaturesCsv(): str =
    return ${chengStr(Array.isArray(theme?.host_abi_missing_features) ? theme.host_abi_missing_features.join(',') : '')}

fn r2cNativeGuiHostAbiFirstBatchReady(): bool =
    return ${chengBool(Boolean(theme?.host_abi_first_batch_ready))}

fn r2cNativeGuiStorageHostReady(): bool =
    return ${chengBool(Boolean(theme?.storage_host_ready))}

fn r2cNativeGuiFetchHostReady(): bool =
    return ${chengBool(Boolean(theme?.fetch_host_ready))}

fn r2cNativeGuiCustomEventHostReady(): bool =
    return ${chengBool(Boolean(theme?.custom_event_host_ready))}

fn r2cNativeGuiResizeObserverHostReady(): bool =
    return ${chengBool(Boolean(theme?.resize_observer_host_ready))}

fn r2cNativeGuiReadFlag(key: str, defaultValue: str): str =
    var out: str
    if r2cNativeGuiReadFlagValueBridge(key, out):
        return out
    return defaultValue

fn r2cNativeGuiJsonNodeStr(node: json.JsonNode, fallback: str): str =
    if node.kind == json.JString:
        return json.GetStr(node)
    return fallback

fn r2cNativeGuiJsonNodeInt32(node: json.JsonNode, fallback: int32): int32 =
    if node.kind == json.JInt:
        return int32(json.GetInt(node))
    return fallback

fn r2cNativeGuiJsonNodeBool(node: json.JsonNode, fallback: bool): bool =
    if node.kind == json.JBool:
        return json.GetBool(node)
    return fallback

fn r2cNativeGuiJsonFieldStr(node: json.JsonNode, key: str, fallback: str): str =
    return r2cNativeGuiJsonNodeStr(node[key], fallback)

fn r2cNativeGuiJsonFieldInt32(node: json.JsonNode, key: str, fallback: int32): int32 =
    return r2cNativeGuiJsonNodeInt32(node[key], fallback)

fn r2cNativeGuiJsonFieldBool(node: json.JsonNode, key: str, fallback: bool): bool =
    return r2cNativeGuiJsonNodeBool(node[key], fallback)

fn r2cNativeGuiItemFromCatalogNode(node: json.JsonNode): R2cNativeGuiItem =
    var out = r2cNativeGuiItemZero()
    out.id = r2cNativeGuiJsonFieldStr(node, "id", "")
    out.sourceNodeId = r2cNativeGuiJsonFieldStr(node, "source_node_id", "")
    out.kind = r2cNativeGuiJsonFieldStr(node, "kind", "")
    out.x = r2cNativeGuiJsonFieldInt32(node, "x", 0)
    out.y = r2cNativeGuiJsonFieldInt32(node, "y", 0)
    out.width = r2cNativeGuiJsonFieldInt32(node, "width", 0)
    out.height = r2cNativeGuiJsonFieldInt32(node, "height", 0)
    out.text = r2cNativeGuiJsonFieldStr(node, "text", "")
    out.detailText = r2cNativeGuiJsonFieldStr(node, "detail_text", "")
    out.zIndex = r2cNativeGuiJsonFieldInt32(node, "z_index", 0)
    out.planRole = r2cNativeGuiJsonFieldStr(node, "plan_role", "")
    out.layer = r2cNativeGuiJsonFieldStr(node, "layer", "")
    out.column = r2cNativeGuiJsonFieldInt32(node, "column", 0)
    out.columnSpan = r2cNativeGuiJsonFieldInt32(node, "column_span", 0)
    out.interactive = r2cNativeGuiJsonFieldBool(node, "interactive", false)
    out.synthetic = r2cNativeGuiJsonFieldBool(node, "synthetic", false)
    out.visualRole = r2cNativeGuiJsonFieldStr(node, "visual_role", "")
    out.density = r2cNativeGuiJsonFieldStr(node, "density", "")
    out.prominence = r2cNativeGuiJsonFieldStr(node, "prominence", "")
    out.accentTone = r2cNativeGuiJsonFieldStr(node, "accent_tone", "")
    out.sourceModulePath = r2cNativeGuiJsonFieldStr(node, "source_module_path", "")
    out.sourceComponentName = r2cNativeGuiJsonFieldStr(node, "source_component_name", "")
    out.sourceLine = r2cNativeGuiJsonFieldInt32(node, "source_line", 0)
    out.backgroundColor = r2cNativeGuiJsonFieldStr(node, "background_color", "")
    out.borderColor = r2cNativeGuiJsonFieldStr(node, "border_color", "")
    out.textColor = r2cNativeGuiJsonFieldStr(node, "text_color", "")
    out.detailColor = r2cNativeGuiJsonFieldStr(node, "detail_color", "")
    out.fontSize = r2cNativeGuiJsonFieldInt32(node, "font_size", 0)
    out.fontWeight = r2cNativeGuiJsonFieldStr(node, "font_weight", "")
    out.cornerRadius = r2cNativeGuiJsonFieldInt32(node, "corner_radius", 0)
    out.stretchX = r2cNativeGuiJsonFieldBool(node, "stretch_x", false)
    out.stretchY = r2cNativeGuiJsonFieldBool(node, "stretch_y", false)
    return out

fn r2cNativeGuiTryLoadCatalogPathFromContract(outPath: var str): bool =
    outPath = ""
    let contractPath = r2cNativeGuiReadFlag("--runtime-contract", "")
    if len(contractPath) <= 0:
        return false
    let contractText = chengpath.ReadTextFile("", contractPath)
    if len(contractText) <= 0:
        return false
    let parseRes = json.ParseJsonSafe(contractText)
    if !parseRes.success:
        return false
    let root = parseRes.value
    if r2cNativeGuiJsonFieldStr(root, "format", "") != "native_gui_runtime_contract_v1":
        return false
    outPath = r2cNativeGuiJsonFieldStr(root, "typed_item_catalog_path", "")
    if len(outPath) <= 0:
        let typedCatalogNode = root["typed_item_catalog"]
        if typedCatalogNode.kind == json.JObject:
            outPath = r2cNativeGuiJsonFieldStr(typedCatalogNode, "path", "")
    if len(outPath) <= 0:
        return false
    return true

fn r2cNativeGuiTryLoadCatalogItems(out: var R2cNativeGuiItem[]): bool =
    out = []
    var catalogPath: str
    if !r2cNativeGuiTryLoadCatalogPathFromContract(catalogPath):
        return false
    let catalogText = chengpath.ReadTextFile("", catalogPath)
    if len(catalogText) <= 0:
        return false
    let parseRes = json.ParseJsonSafe(catalogText)
    if !parseRes.success:
        return false
    let root = parseRes.value
    if r2cNativeGuiJsonFieldStr(root, "format", "") != "native_layout_item_catalog_v1":
        return false
    let itemsNode = root["items"]
    if itemsNode.kind != json.JArray:
        return false
    let expectedCount = r2cNativeGuiJsonFieldInt32(root, "item_count", int32(itemsNode.a.len))
    for i in 0..<itemsNode.a.len:
        let item = r2cNativeGuiItemFromCatalogNode(itemsNode.a[i])
        if len(item.id) <= 0:
            return false
        add(out, item)
    if expectedCount > 0 && int32(out.len) != expectedCount:
        return false
    return out.len > 0

fn r2cNativeGuiCachedCatalogItems(out: var R2cNativeGuiItem[]): bool =
    out = []
    if r2cNativeGuiCatalogLoadAttempted:
        if !r2cNativeGuiCatalogItemsReady:
            return false
        for i in 0..<r2cNativeGuiCatalogItemsCache.len:
            add(out, r2cNativeGuiCatalogItemsCache[i])
        return true
    r2cNativeGuiCatalogLoadAttempted = true
    var loaded: R2cNativeGuiItem[]
    if !r2cNativeGuiTryLoadCatalogItems(loaded):
        r2cNativeGuiCatalogItemsReady = false
        return false
    r2cNativeGuiCatalogItemsCache = []
    for i in 0..<loaded.len:
        add(r2cNativeGuiCatalogItemsCache, loaded[i])
        add(out, loaded[i])
    r2cNativeGuiCatalogItemsReady = true
    return true

fn r2cNativeGuiItems(): R2cNativeGuiItem[] =
    var out: R2cNativeGuiItem[]
    if r2cNativeGuiCachedCatalogItems(out):
        return out
    return r2cNativeGuiDefaultItems()

fn r2cNativeGuiThemeBackgroundTop(): str =
    return ${chengStr(backgroundTop)}

fn r2cNativeGuiThemeBackgroundBottom(): str =
    return ${chengStr(backgroundBottom)}

fn r2cNativeGuiThemePanelBackground(): str =
    return ${chengStr(panelBackground)}

fn r2cNativeGuiThemePanelShadow(): str =
    return ${chengStr(panelShadow)}

fn r2cNativeGuiThemeBorderColor(): str =
    return ${chengStr(borderColor)}

fn r2cNativeGuiThemeAccentColor(): str =
    return ${chengStr(accentColor)}

fn r2cNativeGuiThemeAccentSoft(): str =
    return ${chengStr(accentSoft)}

fn r2cNativeGuiThemeFocusColor(): str =
    return ${chengStr(focusColor)}

fn r2cNativeGuiThemeTextPrimary(): str =
    return ${chengStr(textPrimary)}

fn r2cNativeGuiThemeTextMuted(): str =
    return ${chengStr(textMuted)}

fn r2cNativeGuiMaxInt32(a: int32, b: int32): int32 =
    if a > b:
        return a
    return b

fn r2cNativeGuiClampInt32(value: int32, low: int32, high: int32): int32 =
    if value < low:
        return low
    if value > high:
        return high
    return value

fn r2cNativeGuiItemRenderWidth(state: R2cNativeGuiState, item: R2cNativeGuiItem): int32 =
    if !item.stretchX:
        return item.width
    let trailing = r2cNativeGuiMaxInt32(0, r2cNativeGuiBaseWindowWidth() - item.x - item.width)
    return r2cNativeGuiClampInt32(state.windowWidth - item.x - trailing, 0, 20000)

fn r2cNativeGuiItemRenderHeight(state: R2cNativeGuiState, item: R2cNativeGuiItem): int32 =
    if !item.stretchY:
        return item.height
    let bottomGap = r2cNativeGuiMaxInt32(0, r2cNativeGuiBaseWindowHeight() - item.y - item.height)
    return r2cNativeGuiClampInt32(state.windowHeight - item.y - bottomGap, 0, 20000)

fn r2cNativeGuiItemViewportY(state: R2cNativeGuiState, item: R2cNativeGuiItem): int32 =
    if item.layer == "content":
        return item.y - state.scrollOffsetY
    return item.y

fn r2cNativeGuiItemVisible(state: R2cNativeGuiState, item: R2cNativeGuiItem): bool =
    if item.id == "root":
        return false
    let height = r2cNativeGuiItemRenderHeight(state, item)
    let viewportY = r2cNativeGuiItemViewportY(state, item)
    let top = viewportY
    let bottom = viewportY + height
    if bottom <= 0:
        return false
    if top >= state.windowHeight:
        return false
    return true

fn r2cNativeGuiMeasureContentHeight(state: R2cNativeGuiState): int32 =
    var maxBottom = r2cNativeGuiBaseContentHeight()
    let items = r2cNativeGuiItems()
    for i in 0..<items.len:
        let item = items[i]
        let bottom = item.y + r2cNativeGuiItemRenderHeight(state, item)
        if bottom > maxBottom:
            maxBottom = bottom
    if maxBottom < state.windowHeight:
        return state.windowHeight
    return maxBottom

fn r2cNativeGuiVisibleItemCount(state: R2cNativeGuiState): int32 =
    var count: int32
    let items = r2cNativeGuiItems()
    for i in 0..<items.len:
        if r2cNativeGuiItemVisible(state, items[i]):
            count = count + 1
    return count

fn r2cNativeGuiFindItemIndex(itemId: str): int32 =
    let itemIdLen: int32 = len(itemId)
    if itemIdLen <= 0:
        return -1
    let items = r2cNativeGuiItems()
    for i in 0..<items.len:
        if items[i].id == itemId:
            return i
    return -1

fn r2cNativeGuiClearSelection(state: var R2cNativeGuiState) =
    state.selectedItemId = ""
    state.selectedSourceNodeId = ""
    state.selectedSourceModulePath = ""
    state.selectedSourceComponentName = ""
    state.selectedSourceLine = 0
    state.selectedItemInteractive = false
    state.selectedItemKind = ""
    state.selectedItemPlanRole = ""
    state.selectedItemVisualRole = ""
    state.selectedItemX = 0
    state.selectedItemY = 0
    state.selectedItemWidth = 0
    state.selectedItemHeight = 0
    return

fn r2cNativeGuiSetSelectedFromItem(state: var R2cNativeGuiState, item: R2cNativeGuiItem) =
    state.selectedItemId = item.id
    state.selectedSourceNodeId = item.sourceNodeId
    state.selectedSourceModulePath = item.sourceModulePath
    state.selectedSourceComponentName = item.sourceComponentName
    state.selectedSourceLine = item.sourceLine
    state.selectedItemInteractive = item.interactive
    state.selectedItemKind = item.kind
    state.selectedItemPlanRole = item.planRole
    state.selectedItemVisualRole = item.visualRole
    state.selectedItemX = item.x
    state.selectedItemY = item.y
    state.selectedItemWidth = r2cNativeGuiItemRenderWidth(state, item)
    state.selectedItemHeight = r2cNativeGuiItemRenderHeight(state, item)
    return

fn r2cNativeGuiItemSourceBacked(item: R2cNativeGuiItem): bool =
    if len(item.sourceModulePath) > 0:
        return true
    if len(item.sourceComponentName) > 0:
        return true
    if item.sourceLine > 0:
        return true
    return false

fn r2cNativeGuiFindDefaultSelectedIndex(state: R2cNativeGuiState): int32 =
    let items = r2cNativeGuiItems()
    for i in 0..<items.len:
        let item = items[i]
        if item.id == "root":
            continue
        if !r2cNativeGuiItemSourceBacked(item):
            continue
        if !r2cNativeGuiItemVisible(state, item):
            continue
        return i
    for i in 0..<items.len:
        let item = items[i]
        if item.id == "root":
            continue
        if r2cNativeGuiItemSourceBacked(item):
            return i
    return -1

fn r2cNativeGuiRefreshMetrics(state: var R2cNativeGuiState) =
    state.contentHeight = r2cNativeGuiMeasureContentHeight(state)
    let overflow = state.contentHeight - state.windowHeight
    if overflow > 0:
        state.scrollHeight = overflow
    else:
        state.scrollHeight = 0
    state.scrollOffsetY = r2cNativeGuiClampInt32(state.scrollOffsetY, 0, state.scrollHeight)
    let items = r2cNativeGuiItems()
    let selectedIndex = r2cNativeGuiFindItemIndex(state.selectedItemId)
    if selectedIndex < 0:
        let defaultSelectedIndex = r2cNativeGuiFindDefaultSelectedIndex(state)
        if defaultSelectedIndex >= 0:
            let defaultSelectedItem = items[defaultSelectedIndex]
            r2cNativeGuiSetSelectedFromItem(state, defaultSelectedItem)
        else:
            r2cNativeGuiClearSelection(state)
    else:
        let selectedItem = items[selectedIndex]
        r2cNativeGuiSetSelectedFromItem(state, selectedItem)
    let focusedIndex = r2cNativeGuiFindItemIndex(state.focusedItemId)
    if focusedIndex < 0:
        state.focusedItemId = ""
    else:
        let focusedItem = items[focusedIndex]
        if !focusedItem.interactive:
            state.focusedItemId = ""
    state.visibleLayoutItemCount = r2cNativeGuiVisibleItemCount(state)
    return

fn r2cNativeGuiReadStateFlags(): R2cNativeGuiState =
    var out: R2cNativeGuiState
    out.windowTitle = r2cNativeGuiBaseTitle()
    out.routeState = r2cNativeGuiBaseRouteState()
    out.entryModule = r2cNativeGuiBaseEntryModule()
    out.hostAbiFirstBatchReady = r2cNativeGuiHostAbiFirstBatchReady()
    out.hostAbiFeatureHitsCsv = r2cNativeGuiHostAbiFeatureHitsCsv()
    out.hostAbiMissingFeaturesCsv = r2cNativeGuiHostAbiMissingFeaturesCsv()
    out.storageHostReady = r2cNativeGuiStorageHostReady()
    out.fetchHostReady = r2cNativeGuiFetchHostReady()
    out.customEventHostReady = r2cNativeGuiCustomEventHostReady()
    out.resizeObserverHostReady = r2cNativeGuiResizeObserverHostReady()
    out.windowWidth = r2cNativeGuiReadInt32FlagOrDefaultBridge("--state-window-width", r2cNativeGuiBaseWindowWidth())
    out.windowHeight = r2cNativeGuiReadInt32FlagOrDefaultBridge("--state-window-height", r2cNativeGuiBaseWindowHeight())
    out.contentHeight = r2cNativeGuiBaseContentHeight()
    out.scrollHeight = r2cNativeGuiBaseScrollHeight()
    out.scrollOffsetY = r2cNativeGuiReadInt32FlagOrDefaultBridge("--state-scroll-offset-y", 0)
    out.clickCount = r2cNativeGuiReadInt32FlagOrDefaultBridge("--state-click-count", 0)
    out.resizeCount = r2cNativeGuiReadInt32FlagOrDefaultBridge("--state-resize-count", 0)
    out.scrollCount = r2cNativeGuiReadInt32FlagOrDefaultBridge("--state-scroll-count", 0)
    out.keyCount = r2cNativeGuiReadInt32FlagOrDefaultBridge("--state-key-count", 0)
    out.textCount = r2cNativeGuiReadInt32FlagOrDefaultBridge("--state-text-count", 0)
    out.focusCount = r2cNativeGuiReadInt32FlagOrDefaultBridge("--state-focus-count", 0)
    let hasLastClickText: str = r2cNativeGuiReadFlag("--state-has-last-click", "false")
    if hasLastClickText == "true" || hasLastClickText == "1":
        out.hasLastClick = true
    else:
        out.hasLastClick = false
    out.lastClickX = r2cNativeGuiReadInt32FlagOrDefaultBridge("--state-last-click-x", 0)
    out.lastClickY = r2cNativeGuiReadInt32FlagOrDefaultBridge("--state-last-click-y", 0)
    out.selectedItemId = r2cNativeGuiReadFlag("--state-selected-item-id", "")
    out.focusedItemId = r2cNativeGuiReadFlag("--state-focused-item-id", "")
    out.typedText = r2cNativeGuiReadFlag("--state-typed-text", "")
    out.lastKey = r2cNativeGuiReadFlag("--state-last-key", "")
    r2cNativeGuiRefreshMetrics(out)
    return out

fn r2cNativeGuiHitItem(state: R2cNativeGuiState, x: int32, y: int32, outItem: var R2cNativeGuiItem): bool =
    outItem = r2cNativeGuiItemZero()
    let items = r2cNativeGuiItems()
    var idx: int32 = items.len - 1
    while idx >= 0:
        let item = items[idx]
        if item.id != "root":
            let width = r2cNativeGuiItemRenderWidth(state, item)
            let height = r2cNativeGuiItemRenderHeight(state, item)
            let viewportY = r2cNativeGuiItemViewportY(state, item)
            let insideX = x >= item.x && x <= (item.x + width)
            let insideY = y >= viewportY && y <= (viewportY + height)
            if insideX && insideY:
                outItem = item
                return true
        idx = idx - 1
    return false

fn r2cNativeGuiApplyClickAt(state: var R2cNativeGuiState, clickX: int32, clickY: int32) =
    state.clickCount = state.clickCount + 1
    state.hasLastClick = true
    state.lastClickX = clickX
    state.lastClickY = clickY
    var hitItem = r2cNativeGuiItemZero()
    if r2cNativeGuiHitItem(state, clickX, clickY, hitItem):
        r2cNativeGuiSetSelectedFromItem(state, hitItem)
        if hitItem.interactive:
            if state.focusedItemId != hitItem.id:
                state.focusCount = state.focusCount + 1
            state.focusedItemId = hitItem.id
    else:
        r2cNativeGuiClearSelection(state)
        state.focusedItemId = ""
    return

fn r2cNativeGuiApplyResizeTo(state: var R2cNativeGuiState, nextWidth: int32, nextHeight: int32) =
    if nextWidth != state.windowWidth || nextHeight != state.windowHeight:
        state.resizeCount = state.resizeCount + 1
    state.windowWidth = nextWidth
    state.windowHeight = nextHeight
    return

fn r2cNativeGuiJsonBool(value: bool): str =
    if value:
        return "true"
    return "false"

fn r2cNativeGuiEmitBoolJson(value: bool) =
    if value:
        exec_io.r2cWriteStdout("true")
        return
    exec_io.r2cWriteStdout("false")
    return

fn r2cNativeGuiEmitIntJson(value: int32) =
    exec_io.r2cWriteStdout(intToStr(value))
    return

fn r2cNativeGuiQuotedJson(text: str): str =
    return r2cNativeGuiQuoteJsonTextBridge(text)

fn r2cNativeGuiEmitQuotedJson(text: str) =
    exec_io.r2cWriteStdout(r2cNativeGuiQuotedJson(text))
    return

fn r2cNativeGuiEmitSelectionJson(state: R2cNativeGuiState) =
    let ready: bool = len(state.selectedItemId) > 0
    exec_io.r2cWriteStdout("{\\"ready\\": ")
    r2cNativeGuiEmitBoolJson(ready)
    exec_io.r2cWriteStdout(", \\"id\\": ")
    r2cNativeGuiEmitQuotedJson(state.selectedItemId)
    exec_io.r2cWriteStdout(", \\"source_node_id\\": ")
    r2cNativeGuiEmitQuotedJson(state.selectedSourceNodeId)
    exec_io.r2cWriteStdout(", \\"source_module_path\\": ")
    r2cNativeGuiEmitQuotedJson(state.selectedSourceModulePath)
    exec_io.r2cWriteStdout(", \\"source_component_name\\": ")
    r2cNativeGuiEmitQuotedJson(state.selectedSourceComponentName)
    exec_io.r2cWriteStdout(", \\"source_line\\": ")
    r2cNativeGuiEmitIntJson(state.selectedSourceLine)
    exec_io.r2cWriteStdout(", \\"interactive\\": ")
    r2cNativeGuiEmitBoolJson(state.selectedItemInteractive)
    exec_io.r2cWriteStdout(", \\"kind\\": ")
    r2cNativeGuiEmitQuotedJson(state.selectedItemKind)
    exec_io.r2cWriteStdout(", \\"plan_role\\": ")
    r2cNativeGuiEmitQuotedJson(state.selectedItemPlanRole)
    exec_io.r2cWriteStdout(", \\"visual_role\\": ")
    r2cNativeGuiEmitQuotedJson(state.selectedItemVisualRole)
    exec_io.r2cWriteStdout(", \\"x\\": ")
    r2cNativeGuiEmitIntJson(state.selectedItemX)
    exec_io.r2cWriteStdout(", \\"y\\": ")
    r2cNativeGuiEmitIntJson(state.selectedItemY)
    exec_io.r2cWriteStdout(", \\"width\\": ")
    r2cNativeGuiEmitIntJson(state.selectedItemWidth)
    exec_io.r2cWriteStdout(", \\"height\\": ")
    r2cNativeGuiEmitIntJson(state.selectedItemHeight)
    exec_io.r2cWriteStdout("}")
    return

fn r2cNativeGuiEmitStateJson(state: R2cNativeGuiState) =
    exec_io.r2cWriteStdout("{\\"window_title\\": ")
    r2cNativeGuiEmitQuotedJson(state.windowTitle)
    exec_io.r2cWriteStdout(", \\"route_state\\": ")
    r2cNativeGuiEmitQuotedJson(state.routeState)
    exec_io.r2cWriteStdout(", \\"entry_module\\": ")
    r2cNativeGuiEmitQuotedJson(state.entryModule)
    exec_io.r2cWriteStdout(", \\"host_abi_first_batch_ready\\": ")
    r2cNativeGuiEmitBoolJson(state.hostAbiFirstBatchReady)
    exec_io.r2cWriteStdout(", \\"host_abi_feature_hits_csv\\": ")
    r2cNativeGuiEmitQuotedJson(state.hostAbiFeatureHitsCsv)
    exec_io.r2cWriteStdout(", \\"host_abi_missing_features_csv\\": ")
    r2cNativeGuiEmitQuotedJson(state.hostAbiMissingFeaturesCsv)
    exec_io.r2cWriteStdout(", \\"storage_host_ready\\": ")
    r2cNativeGuiEmitBoolJson(state.storageHostReady)
    exec_io.r2cWriteStdout(", \\"fetch_host_ready\\": ")
    r2cNativeGuiEmitBoolJson(state.fetchHostReady)
    exec_io.r2cWriteStdout(", \\"custom_event_host_ready\\": ")
    r2cNativeGuiEmitBoolJson(state.customEventHostReady)
    exec_io.r2cWriteStdout(", \\"resize_observer_host_ready\\": ")
    r2cNativeGuiEmitBoolJson(state.resizeObserverHostReady)
    exec_io.r2cWriteStdout(", \\"window_width\\": ")
    r2cNativeGuiEmitIntJson(state.windowWidth)
    exec_io.r2cWriteStdout(", \\"window_height\\": ")
    r2cNativeGuiEmitIntJson(state.windowHeight)
    exec_io.r2cWriteStdout(", \\"content_height\\": ")
    r2cNativeGuiEmitIntJson(state.contentHeight)
    exec_io.r2cWriteStdout(", \\"scroll_height\\": ")
    r2cNativeGuiEmitIntJson(state.scrollHeight)
    exec_io.r2cWriteStdout(", \\"scroll_offset_y\\": ")
    r2cNativeGuiEmitIntJson(state.scrollOffsetY)
    exec_io.r2cWriteStdout(", \\"click_count\\": ")
    r2cNativeGuiEmitIntJson(state.clickCount)
    exec_io.r2cWriteStdout(", \\"resize_count\\": ")
    r2cNativeGuiEmitIntJson(state.resizeCount)
    exec_io.r2cWriteStdout(", \\"scroll_count\\": ")
    r2cNativeGuiEmitIntJson(state.scrollCount)
    exec_io.r2cWriteStdout(", \\"key_count\\": ")
    r2cNativeGuiEmitIntJson(state.keyCount)
    exec_io.r2cWriteStdout(", \\"text_count\\": ")
    r2cNativeGuiEmitIntJson(state.textCount)
    exec_io.r2cWriteStdout(", \\"focus_count\\": ")
    r2cNativeGuiEmitIntJson(state.focusCount)
    exec_io.r2cWriteStdout(", \\"has_last_click\\": ")
    r2cNativeGuiEmitBoolJson(state.hasLastClick)
    exec_io.r2cWriteStdout(", \\"last_click_x\\": ")
    r2cNativeGuiEmitIntJson(state.lastClickX)
    exec_io.r2cWriteStdout(", \\"last_click_y\\": ")
    r2cNativeGuiEmitIntJson(state.lastClickY)
    exec_io.r2cWriteStdout(", \\"selected_item_id\\": ")
    r2cNativeGuiEmitQuotedJson(state.selectedItemId)
    exec_io.r2cWriteStdout(", \\"selected_source_node_id\\": ")
    r2cNativeGuiEmitQuotedJson(state.selectedSourceNodeId)
    exec_io.r2cWriteStdout(", \\"selected_source_module_path\\": ")
    r2cNativeGuiEmitQuotedJson(state.selectedSourceModulePath)
    exec_io.r2cWriteStdout(", \\"selected_source_component_name\\": ")
    r2cNativeGuiEmitQuotedJson(state.selectedSourceComponentName)
    exec_io.r2cWriteStdout(", \\"selected_source_line\\": ")
    r2cNativeGuiEmitIntJson(state.selectedSourceLine)
    exec_io.r2cWriteStdout(", \\"selected_item_interactive\\": ")
    r2cNativeGuiEmitBoolJson(state.selectedItemInteractive)
    exec_io.r2cWriteStdout(", \\"focused_item_id\\": ")
    r2cNativeGuiEmitQuotedJson(state.focusedItemId)
    exec_io.r2cWriteStdout(", \\"typed_text\\": ")
    r2cNativeGuiEmitQuotedJson(state.typedText)
    exec_io.r2cWriteStdout(", \\"last_key\\": ")
    r2cNativeGuiEmitQuotedJson(state.lastKey)
    exec_io.r2cWriteStdout(", \\"visible_layout_item_count\\": ")
    r2cNativeGuiEmitIntJson(state.visibleLayoutItemCount)
    exec_io.r2cWriteStdout(", \\"selected_item\\": ")
    r2cNativeGuiEmitSelectionJson(state)
    exec_io.r2cWriteStdout("}")
    return

fn r2cNativeGuiEmitRenderPlanJson(state: R2cNativeGuiState) =
    let items = r2cNativeGuiItems()
    var commandCount: int32
    exec_io.r2cWriteStdout("{\\"format\\": \\"native_render_plan_v1\\"")
    exec_io.r2cWriteStdout(", \\"ready\\": true")
    exec_io.r2cWriteStdout(", \\"window_title\\": ")
    r2cNativeGuiEmitQuotedJson(state.windowTitle)
    exec_io.r2cWriteStdout(", \\"route_state\\": ")
    r2cNativeGuiEmitQuotedJson(state.routeState)
    exec_io.r2cWriteStdout(", \\"entry_module\\": ")
    r2cNativeGuiEmitQuotedJson(state.entryModule)
    exec_io.r2cWriteStdout(", \\"host_abi_first_batch_ready\\": ")
    r2cNativeGuiEmitBoolJson(state.hostAbiFirstBatchReady)
    exec_io.r2cWriteStdout(", \\"host_abi_feature_hits_csv\\": ")
    r2cNativeGuiEmitQuotedJson(state.hostAbiFeatureHitsCsv)
    exec_io.r2cWriteStdout(", \\"host_abi_missing_features_csv\\": ")
    r2cNativeGuiEmitQuotedJson(state.hostAbiMissingFeaturesCsv)
    exec_io.r2cWriteStdout(", \\"window_width\\": ")
    r2cNativeGuiEmitIntJson(state.windowWidth)
    exec_io.r2cWriteStdout(", \\"window_height\\": ")
    r2cNativeGuiEmitIntJson(state.windowHeight)
    exec_io.r2cWriteStdout(", \\"content_height\\": ")
    r2cNativeGuiEmitIntJson(state.contentHeight)
    exec_io.r2cWriteStdout(", \\"scroll_height\\": ")
    r2cNativeGuiEmitIntJson(state.scrollHeight)
    exec_io.r2cWriteStdout(", \\"scroll_offset_y\\": ")
    r2cNativeGuiEmitIntJson(state.scrollOffsetY)
    exec_io.r2cWriteStdout(", \\"visible_layout_item_count\\": ")
    r2cNativeGuiEmitIntJson(state.visibleLayoutItemCount)
    exec_io.r2cWriteStdout(", \\"selected_item_id\\": ")
    r2cNativeGuiEmitQuotedJson(state.selectedItemId)
    exec_io.r2cWriteStdout(", \\"focused_item_id\\": ")
    r2cNativeGuiEmitQuotedJson(state.focusedItemId)
    exec_io.r2cWriteStdout(", \\"typed_text\\": ")
    r2cNativeGuiEmitQuotedJson(state.typedText)
    exec_io.r2cWriteStdout(", \\"commands\\": [")
    exec_io.r2cWriteStdout("{\\"type\\": \\"background_gradient\\", \\"x\\": 0, \\"y\\": 0, \\"width\\": ")
    r2cNativeGuiEmitIntJson(state.windowWidth)
    exec_io.r2cWriteStdout(", \\"height\\": ")
    r2cNativeGuiEmitIntJson(state.windowHeight)
    exec_io.r2cWriteStdout(", \\"color_top\\": ")
    r2cNativeGuiEmitQuotedJson(r2cNativeGuiThemeBackgroundTop())
    exec_io.r2cWriteStdout(", \\"color_bottom\\": ")
    r2cNativeGuiEmitQuotedJson(r2cNativeGuiThemeBackgroundBottom())
    exec_io.r2cWriteStdout(", \\"z_index\\": 0}")
    commandCount = commandCount + 1
    exec_io.r2cWriteStdout(", {\\"type\\": \\"rounded_panel\\", \\"x\\": 10, \\"y\\": 10, \\"width\\": ")
    r2cNativeGuiEmitIntJson(r2cNativeGuiMaxInt32(0, state.windowWidth - 20))
    exec_io.r2cWriteStdout(", \\"height\\": ")
    r2cNativeGuiEmitIntJson(r2cNativeGuiMaxInt32(0, state.windowHeight - 20))
    exec_io.r2cWriteStdout(", \\"corner_radius\\": 24, \\"background_color\\": ")
    r2cNativeGuiEmitQuotedJson(r2cNativeGuiThemePanelBackground())
    exec_io.r2cWriteStdout(", \\"shadow_color\\": ")
    r2cNativeGuiEmitQuotedJson(r2cNativeGuiThemePanelShadow())
    exec_io.r2cWriteStdout(", \\"border_color\\": ")
    r2cNativeGuiEmitQuotedJson(r2cNativeGuiThemeBorderColor())
    exec_io.r2cWriteStdout(", \\"z_index\\": 1}")
    commandCount = commandCount + 1
    for i in 0..<items.len:
        let item = items[i]
        if r2cNativeGuiItemVisible(state, item):
            let viewportY = r2cNativeGuiItemViewportY(state, item)
            let width = r2cNativeGuiItemRenderWidth(state, item)
            let height = r2cNativeGuiItemRenderHeight(state, item)
            let selected = item.id == state.selectedItemId
            var fillColor = item.backgroundColor
            var border = item.borderColor
            var lineWidth: int32 = 1
            if selected:
                fillColor = r2cNativeGuiThemeAccentSoft()
                border = r2cNativeGuiThemeAccentColor()
                lineWidth = 2
            var detailText = item.detailText
            if item.id == state.focusedItemId:
                if len(state.typedText) > 0:
                    if len(detailText) > 0:
                        detailText = detailText + "  input: " + state.typedText
                    else:
                        detailText = "input: " + state.typedText
            exec_io.r2cWriteStdout(", {\\"type\\": \\"item_card\\", \\"item_id\\": ")
            r2cNativeGuiEmitQuotedJson(item.id)
            exec_io.r2cWriteStdout(", \\"kind\\": ")
            r2cNativeGuiEmitQuotedJson(item.kind)
            exec_io.r2cWriteStdout(", \\"x\\": ")
            r2cNativeGuiEmitIntJson(item.x)
            exec_io.r2cWriteStdout(", \\"y\\": ")
            r2cNativeGuiEmitIntJson(viewportY)
            exec_io.r2cWriteStdout(", \\"width\\": ")
            r2cNativeGuiEmitIntJson(width)
            exec_io.r2cWriteStdout(", \\"height\\": ")
            r2cNativeGuiEmitIntJson(height)
            exec_io.r2cWriteStdout(", \\"z_index\\": ")
            r2cNativeGuiEmitIntJson(item.zIndex)
            exec_io.r2cWriteStdout(", \\"corner_radius\\": ")
            r2cNativeGuiEmitIntJson(item.cornerRadius)
            exec_io.r2cWriteStdout(", \\"line_width\\": ")
            r2cNativeGuiEmitIntJson(lineWidth)
            exec_io.r2cWriteStdout(", \\"background_color\\": ")
            r2cNativeGuiEmitQuotedJson(fillColor)
            exec_io.r2cWriteStdout(", \\"border_color\\": ")
            r2cNativeGuiEmitQuotedJson(border)
            exec_io.r2cWriteStdout(", \\"text_color\\": ")
            r2cNativeGuiEmitQuotedJson(item.textColor)
            exec_io.r2cWriteStdout(", \\"detail_color\\": ")
            r2cNativeGuiEmitQuotedJson(item.detailColor)
            exec_io.r2cWriteStdout(", \\"text\\": ")
            r2cNativeGuiEmitQuotedJson(item.text)
            exec_io.r2cWriteStdout(", \\"detail_text\\": ")
            r2cNativeGuiEmitQuotedJson(detailText)
            exec_io.r2cWriteStdout(", \\"font_size\\": ")
            r2cNativeGuiEmitIntJson(item.fontSize)
            exec_io.r2cWriteStdout(", \\"font_weight\\": ")
            r2cNativeGuiEmitQuotedJson(item.fontWeight)
            exec_io.r2cWriteStdout(", \\"selected\\": ")
            r2cNativeGuiEmitBoolJson(selected)
            exec_io.r2cWriteStdout(", \\"focused\\": ")
            r2cNativeGuiEmitBoolJson(item.id == state.focusedItemId)
            exec_io.r2cWriteStdout("}")
            commandCount = commandCount + 1
            if item.id == state.focusedItemId:
                exec_io.r2cWriteStdout(", {\\"type\\": \\"focus_ring\\", \\"item_id\\": ")
                r2cNativeGuiEmitQuotedJson(item.id)
                exec_io.r2cWriteStdout(", \\"x\\": ")
                r2cNativeGuiEmitIntJson(item.x)
                exec_io.r2cWriteStdout(", \\"y\\": ")
                r2cNativeGuiEmitIntJson(viewportY)
                exec_io.r2cWriteStdout(", \\"width\\": ")
                r2cNativeGuiEmitIntJson(width)
                exec_io.r2cWriteStdout(", \\"height\\": ")
                r2cNativeGuiEmitIntJson(height)
                exec_io.r2cWriteStdout(", \\"corner_radius\\": ")
                r2cNativeGuiEmitIntJson(item.cornerRadius + 4)
                exec_io.r2cWriteStdout(", \\"border_color\\": ")
                r2cNativeGuiEmitQuotedJson(r2cNativeGuiThemeFocusColor())
                exec_io.r2cWriteStdout(", \\"line_width\\": 2, \\"z_index\\": ")
                r2cNativeGuiEmitIntJson(item.zIndex + 2)
                exec_io.r2cWriteStdout("}")
                commandCount = commandCount + 1
    if state.hasLastClick:
        exec_io.r2cWriteStdout(", {\\"type\\": \\"interaction_marker\\", \\"x\\": ")
        r2cNativeGuiEmitIntJson(state.lastClickX)
        exec_io.r2cWriteStdout(", \\"y\\": ")
        r2cNativeGuiEmitIntJson(state.lastClickY)
        exec_io.r2cWriteStdout(", \\"accent_color\\": ")
        r2cNativeGuiEmitQuotedJson(r2cNativeGuiThemeAccentColor())
        exec_io.r2cWriteStdout(", \\"z_index\\": 999}")
        commandCount = commandCount + 1
    exec_io.r2cWriteStdout("]")
    exec_io.r2cWriteStdout(", \\"command_count\\": ")
    r2cNativeGuiEmitIntJson(commandCount)
    exec_io.r2cWriteStdout("}")
    return

fn r2cNativeGuiEmitRuntimeDocJson(state: R2cNativeGuiState) =
    exec_io.r2cWriteStdout("{\\"format\\": \\"native_gui_runtime_v1\\"")
    exec_io.r2cWriteStdout(", \\"ready\\": true")
    exec_io.r2cWriteStdout(", \\"reason\\": \\"ok\\"")
    exec_io.r2cWriteStdout(", \\"state\\": ")
    r2cNativeGuiEmitStateJson(state)
    exec_io.r2cWriteStdout(", \\"render_plan\\": ")
    r2cNativeGuiEmitRenderPlanJson(state)
    exec_io.r2cWriteStdout("}")
    return

fn r2cNativeGuiRuntimeMain(): int32 =
    let eventType: str = r2cNativeGuiReadFlag("--event-type", "none")
    var state = r2cNativeGuiReadStateFlags()
    if eventType == "resize":
        let nextWidth = r2cNativeGuiClampInt32(r2cNativeGuiReadInt32FlagOrDefaultBridge("--event-width", state.windowWidth), 220, 20000)
        let nextHeight = r2cNativeGuiClampInt32(r2cNativeGuiReadInt32FlagOrDefaultBridge("--event-height", state.windowHeight), 220, 20000)
        r2cNativeGuiApplyResizeTo(state, nextWidth, nextHeight)
    elif eventType == "scroll":
        var nextOffset = state.scrollOffsetY
        let scrollToRaw = r2cNativeGuiReadInt32FlagOrDefaultBridge("--event-scroll-to", -2147483647)
        if scrollToRaw != -2147483647:
            nextOffset = scrollToRaw
        else:
            let deltaY = r2cNativeGuiReadInt32FlagOrDefaultBridge("--event-delta-y", 0)
            nextOffset = state.scrollOffsetY - deltaY
        nextOffset = r2cNativeGuiClampInt32(nextOffset, 0, state.scrollHeight)
        if nextOffset != state.scrollOffsetY:
            state.scrollCount = state.scrollCount + 1
        state.scrollOffsetY = nextOffset
    elif eventType == "click":
        let clickX = r2cNativeGuiClampInt32(r2cNativeGuiReadInt32FlagOrDefaultBridge("--event-x", 0), 0, state.windowWidth)
        let clickY = r2cNativeGuiClampInt32(r2cNativeGuiReadInt32FlagOrDefaultBridge("--event-y", 0), 0, state.windowHeight)
        r2cNativeGuiApplyClickAt(state, clickX, clickY)
    elif eventType == "focus":
        var targetId: str = r2cNativeGuiReadFlag("--event-focus-item-id", "")
        if len(targetId) <= 0:
            if state.selectedItemInteractive:
                targetId = state.selectedItemId
        if targetId != state.focusedItemId:
            if len(targetId) <= 0:
                state.focusedItemId = ""
                state.focusCount = state.focusCount + 1
            else:
                let targetIndex = r2cNativeGuiFindItemIndex(targetId)
                if targetIndex >= 0:
                    let items = r2cNativeGuiItems()
                    let targetItem = items[targetIndex]
                    if targetItem.interactive:
                        state.focusedItemId = targetId
                        state.focusCount = state.focusCount + 1
                    else:
                        state.focusedItemId = ""
                else:
                    state.focusedItemId = ""
    elif eventType == "key":
        let key: str = r2cNativeGuiReadFlag("--event-key", "")
        if len(key) > 0:
            state.lastKey = key
            state.keyCount = state.keyCount + 1
            if key == "Escape":
                state.focusedItemId = ""
            elif key == "Tab":
                if state.selectedItemInteractive:
                    if state.focusedItemId != state.selectedItemId:
                        state.focusedItemId = state.selectedItemId
                        state.focusCount = state.focusCount + 1
            elif key == "Backspace":
                if len(state.focusedItemId) > 0:
                    if len(state.typedText) > 0:
                        state.typedText = sliceBytes(state.typedText, 0, len(state.typedText) - 1)
    elif eventType == "text":
        let text: str = r2cNativeGuiReadFlag("--event-text", "")
        if len(text) > 0:
            if len(state.focusedItemId) > 0:
                state.typedText = state.typedText + text
                state.textCount = state.textCount + 1
    r2cNativeGuiRefreshMetrics(state)
    r2cNativeGuiEmitRuntimeDocJson(state)
    exec_io.r2cWriteStdout("\\n")
    return 0`;

  return [head, ...itemFunctions, itemsBlock, tail].join('\n\n');
}

export function renderNativeGuiRuntimeReduceKvCheng(options) {
  const jsonSource = renderNativeGuiRuntimeCheng(options);
  const emitBlockStart = jsonSource.indexOf('fn r2cNativeGuiJsonBool(value: bool): str =');
  const mainBlockStart = jsonSource.indexOf('fn r2cNativeGuiRuntimeMain(): int32 =');
  if (emitBlockStart < 0 || mainBlockStart < 0 || mainBlockStart <= emitBlockStart) {
    throw new Error('native_gui_runtime_reduce_kv_patch_points_missing');
  }
  const prefix = jsonSource.slice(0, emitBlockStart).trimEnd();
  const mainBlock = jsonSource
    .slice(mainBlockStart)
    .replace('    r2cNativeGuiEmitRuntimeDocJson(state)', '    r2cNativeGuiEmitRuntimeReduceKv(state)');
  const reduceEmitBlock = `fn r2cNativeGuiEmitBoolValue(value: bool) =
    if value:
        exec_io.r2cWriteStdout("true")
        return
    exec_io.r2cWriteStdout("false")
    return

fn r2cNativeGuiEmitIntValue(value: int32) =
    exec_io.r2cWriteStdout(intToStr(value))
    return

fn r2cNativeGuiEmitRuntimeReduceKv(state: R2cNativeGuiState) =
    exec_io.r2cWriteStdout("format=native_gui_runtime_reduce_kv_v1\\n")
    exec_io.r2cWriteStdout("ready=true\\n")
    exec_io.r2cWriteStdout("reason=ok\\n")
    exec_io.r2cWriteStdout("window_width=")
    r2cNativeGuiEmitIntValue(state.windowWidth)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("window_height=")
    r2cNativeGuiEmitIntValue(state.windowHeight)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("content_height=")
    r2cNativeGuiEmitIntValue(state.contentHeight)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("scroll_height=")
    r2cNativeGuiEmitIntValue(state.scrollHeight)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("scroll_offset_y=")
    r2cNativeGuiEmitIntValue(state.scrollOffsetY)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("click_count=")
    r2cNativeGuiEmitIntValue(state.clickCount)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("resize_count=")
    r2cNativeGuiEmitIntValue(state.resizeCount)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("scroll_count=")
    r2cNativeGuiEmitIntValue(state.scrollCount)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("key_count=")
    r2cNativeGuiEmitIntValue(state.keyCount)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("text_count=")
    r2cNativeGuiEmitIntValue(state.textCount)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("focus_count=")
    r2cNativeGuiEmitIntValue(state.focusCount)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("has_last_click=")
    r2cNativeGuiEmitBoolValue(state.hasLastClick)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("last_click_x=")
    r2cNativeGuiEmitIntValue(state.lastClickX)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("last_click_y=")
    r2cNativeGuiEmitIntValue(state.lastClickY)
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("selected_index=")
    r2cNativeGuiEmitIntValue(r2cNativeGuiFindItemIndex(state.selectedItemId))
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("focused_index=")
    r2cNativeGuiEmitIntValue(r2cNativeGuiFindItemIndex(state.focusedItemId))
    exec_io.r2cWriteStdout("\\n")
    exec_io.r2cWriteStdout("visible_layout_item_count=")
    r2cNativeGuiEmitIntValue(state.visibleLayoutItemCount)
    exec_io.r2cWriteStdout("\\n")
    return`;
  return `${prefix}\n\n${reduceEmitBlock}\n\n${mainBlock}`;
}
