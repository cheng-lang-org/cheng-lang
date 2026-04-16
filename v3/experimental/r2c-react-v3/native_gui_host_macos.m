#import <Cocoa/Cocoa.h>

@interface R2CSessionView : NSView
@property(nonatomic, strong) NSDictionary *sessionDoc;
@property(nonatomic, strong) NSDictionary *theme;
@property(nonatomic, strong) NSDictionary *renderPlanDoc;
@property(nonatomic, strong) NSDictionary *runtimeStateDoc;
@property(nonatomic, strong) NSArray *renderCommands;
@property(nonatomic, assign) CGFloat sessionWidth;
@property(nonatomic, assign) CGFloat sessionHeight;
@property(nonatomic, assign) CGFloat contentHeight;
@property(nonatomic, assign) CGFloat scrollOffsetY;
@property(nonatomic, assign) NSUInteger clickCount;
@property(nonatomic, assign) NSUInteger resizeCount;
@property(nonatomic, assign) NSUInteger scrollCount;
@property(nonatomic, assign) NSUInteger keyCount;
@property(nonatomic, assign) NSUInteger textCount;
@property(nonatomic, assign) NSUInteger focusCount;
@property(nonatomic, assign) CGFloat lastClickX;
@property(nonatomic, assign) CGFloat lastClickY;
@property(nonatomic, assign) BOOL hasLastClick;
@property(nonatomic, copy) NSString *selectedItemId;
@property(nonatomic, copy) NSString *selectedSourceNodeId;
@property(nonatomic, copy) NSString *selectedSourceModulePath;
@property(nonatomic, copy) NSString *selectedSourceComponentName;
@property(nonatomic, assign) NSInteger selectedSourceLine;
@property(nonatomic, assign) BOOL selectedItemInteractive;
@property(nonatomic, copy) NSString *selectedItemKind;
@property(nonatomic, copy) NSString *selectedItemPlanRole;
@property(nonatomic, copy) NSString *selectedItemVisualRole;
@property(nonatomic, assign) CGFloat selectedItemX;
@property(nonatomic, assign) CGFloat selectedItemY;
@property(nonatomic, assign) CGFloat selectedItemWidth;
@property(nonatomic, assign) CGFloat selectedItemHeight;
@property(nonatomic, copy) NSString *focusedItemId;
@property(nonatomic, copy) NSString *typedText;
@property(nonatomic, copy) NSString *lastKey;
@property(nonatomic, copy) NSString *runtimeExePath;
@property(nonatomic, assign) BOOL runtimeReady;
@property(nonatomic, copy) NSString *runtimeError;
@property(nonatomic, copy) NSString *repoRoot;
@property(nonatomic, assign) BOOL openSourceOnClick;
@property(nonatomic, assign) BOOL sourceJumpAttempted;
@property(nonatomic, assign) BOOL sourceJumpSucceeded;
@property(nonatomic, copy) NSString *sourceJumpCommand;
@property(nonatomic, copy) NSString *sourceJumpTargetPath;
@property(nonatomic, copy) NSString *sourceJumpError;
@property(nonatomic, assign) NSInteger sourceJumpTargetLine;
@end

static BOOL R2CScriptedScenarioActive = NO;

static CGFloat R2CClamp(CGFloat value, CGFloat low, CGFloat high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static CGFloat R2CSessionY(CGFloat sessionHeight, CGFloat itemY, CGFloat itemHeight) {
    return sessionHeight - itemY - itemHeight;
}

static NSString *R2CString(id value) {
    if (value == nil || value == [NSNull null]) return @"";
    if ([value isKindOfClass:[NSString class]]) return (NSString *)value;
    return [value description];
}

static CGFloat R2CCGFloat(id value, CGFloat fallback) {
    if (value == nil || value == [NSNull null]) return fallback;
    if ([value respondsToSelector:@selector(doubleValue)]) return (CGFloat)[value doubleValue];
    return fallback;
}

static NSInteger R2CInteger(id value, NSInteger fallback) {
    if (value == nil || value == [NSNull null]) return fallback;
    if ([value respondsToSelector:@selector(integerValue)]) return [value integerValue];
    return fallback;
}

static BOOL R2CBool(id value, BOOL fallback) {
    if (value == nil || value == [NSNull null]) return fallback;
    if ([value respondsToSelector:@selector(boolValue)]) return [value boolValue];
    return fallback;
}

static BOOL R2CParseBoolText(NSString *text, BOOL fallback) {
    NSString *trimmed = [[R2CString(text) stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] lowercaseString];
    if ([trimmed isEqualToString:@"true"] || [trimmed isEqualToString:@"1"]) return YES;
    if ([trimmed isEqualToString:@"false"] || [trimmed isEqualToString:@"0"]) return NO;
    return fallback;
}

static BOOL R2CParseCGFloatText(NSString *text, CGFloat *outValue) {
    NSString *trimmed = [R2CString(text) stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (trimmed.length == 0) return NO;
    NSScanner *scanner = [NSScanner scannerWithString:trimmed];
    double value = 0.0;
    if (![scanner scanDouble:&value]) return NO;
    if (![scanner isAtEnd]) return NO;
    if (outValue != NULL) *outValue = (CGFloat)value;
    return YES;
}

static BOOL R2CParsePairSpec(NSString *text, NSString *separator, CGFloat *outA, CGFloat *outB) {
    NSString *trimmed = [R2CString(text) stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (trimmed.length == 0) return NO;
    NSArray<NSString *> *parts = [trimmed componentsSeparatedByString:separator];
    if ([parts count] != 2) return NO;
    CGFloat a = 0.0;
    CGFloat b = 0.0;
    if (!R2CParseCGFloatText(parts[0], &a)) return NO;
    if (!R2CParseCGFloatText(parts[1], &b)) return NO;
    if (outA != NULL) *outA = a;
    if (outB != NULL) *outB = b;
    return YES;
}

static NSString *R2CTrimmedText(NSString *text) {
    return [R2CString(text) stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

static NSString *R2CJoinedPreview(NSArray *items, NSUInteger limit) {
    if (![items isKindOfClass:[NSArray class]] || [items count] == 0) return @"";
    NSMutableArray<NSString *> *parts = [NSMutableArray array];
    NSUInteger count = 0;
    for (id rawItem in items) {
        NSString *item = R2CTrimmedText(rawItem);
        if ([item length] == 0) continue;
        [parts addObject:item];
        count += 1;
        if (count >= limit) break;
    }
    if ([parts count] == 0) return @"";
    NSString *joined = [parts componentsJoinedByString:@", "];
    if ([items count] > [parts count]) {
        return [joined stringByAppendingString:@", ..."];
    }
    return joined;
}

static NSString *R2CShellQuote(NSString *text) {
    NSString *raw = R2CString(text);
    NSString *escaped = [raw stringByReplacingOccurrencesOfString:@"'" withString:@"'\\''"];
    return [NSString stringWithFormat:@"'%@'", escaped];
}

static NSColor *R2CColorFromHexString(NSString *text, NSColor *fallback) {
    NSString *trimmed = [[R2CString(text) stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] uppercaseString];
    if (trimmed.length == 0) return fallback;
    if ([trimmed hasPrefix:@"#"]) trimmed = [trimmed substringFromIndex:1];
    if (trimmed.length != 6 && trimmed.length != 8) return fallback;
    unsigned long long raw = 0;
    NSScanner *scanner = [NSScanner scannerWithString:trimmed];
    if (![scanner scanHexLongLong:&raw]) return fallback;
    CGFloat red = 0.0;
    CGFloat green = 0.0;
    CGFloat blue = 0.0;
    CGFloat alpha = 1.0;
    if (trimmed.length == 8) {
        red = (CGFloat)((raw >> 24) & 0xFF) / 255.0;
        green = (CGFloat)((raw >> 16) & 0xFF) / 255.0;
        blue = (CGFloat)((raw >> 8) & 0xFF) / 255.0;
        alpha = (CGFloat)(raw & 0xFF) / 255.0;
    } else {
        red = (CGFloat)((raw >> 16) & 0xFF) / 255.0;
        green = (CGFloat)((raw >> 8) & 0xFF) / 255.0;
        blue = (CGFloat)(raw & 0xFF) / 255.0;
    }
    return [NSColor colorWithCalibratedRed:red green:green blue:blue alpha:alpha];
}

static NSFontWeight R2CFontWeightFromText(NSString *text, NSFontWeight fallback) {
    NSString *weight = [[R2CString(text) stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] lowercaseString];
    if ([weight isEqualToString:@"medium"]) return NSFontWeightMedium;
    if ([weight isEqualToString:@"semibold"]) return NSFontWeightSemibold;
    if ([weight isEqualToString:@"bold"]) return NSFontWeightBold;
    if ([weight isEqualToString:@"light"]) return NSFontWeightLight;
    if ([weight isEqualToString:@"thin"]) return NSFontWeightThin;
    if ([weight isEqualToString:@"black"]) return NSFontWeightBlack;
    if ([weight isEqualToString:@"regular"]) return NSFontWeightRegular;
    return fallback;
}

@implementation R2CSessionView

- (BOOL)isFlipped {
    return NO;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (NSColor *)themeColor:(NSString *)key fallback:(NSColor *)fallback {
    return R2CColorFromHexString(self.theme[key], fallback);
}

- (CGFloat)maxScrollOffset {
    return MAX(0.0, self.contentHeight - self.sessionHeight);
}

- (NSRect)frameForRenderCommand:(NSDictionary *)command {
    CGFloat x = R2CCGFloat(command[@"x"], 0.0);
    CGFloat y = R2CCGFloat(command[@"y"], 0.0);
    CGFloat width = R2CCGFloat(command[@"width"], 0.0);
    CGFloat height = R2CCGFloat(command[@"height"], 0.0);
    return NSMakeRect(x,
                      R2CSessionY(self.sessionHeight, y, height),
                      width,
                      height);
}

- (NSDictionary *)layoutSurfaceDoc {
    id doc = self.sessionDoc[@"layout_surface"];
    return [doc isKindOfClass:[NSDictionary class]] ? (NSDictionary *)doc : nil;
}

- (NSDictionary *)nativeLayoutPlanDoc {
    id doc = self.sessionDoc[@"native_layout_plan"];
    return [doc isKindOfClass:[NSDictionary class]] ? (NSDictionary *)doc : nil;
}

- (NSArray *)nativeLayoutItems {
    NSDictionary *planDoc = [self nativeLayoutPlanDoc];
    id items = planDoc[@"items"];
    return [items isKindOfClass:[NSArray class]] ? (NSArray *)items : @[];
}

- (NSString *)themeText:(NSString *)key fallback:(NSString *)fallback {
    NSString *value = R2CString(self.theme[key]);
    if ([value length] > 0) return value;
    return R2CString(fallback);
}

- (CGFloat)baseWindowWidth {
    NSDictionary *planDoc = [self nativeLayoutPlanDoc];
    CGFloat fallback = R2CCGFloat(self.sessionDoc[@"window"][@"width"], 390.0);
    return R2CCGFloat(planDoc[@"window_width"], fallback);
}

- (CGFloat)baseWindowHeight {
    NSDictionary *planDoc = [self nativeLayoutPlanDoc];
    CGFloat fallback = R2CCGFloat(self.sessionDoc[@"window"][@"height"], 844.0);
    return R2CCGFloat(planDoc[@"window_height"], fallback);
}

- (CGFloat)baseContentHeight {
    NSDictionary *planDoc = [self nativeLayoutPlanDoc];
    return R2CCGFloat(planDoc[@"content_height"], [self baseWindowHeight]);
}

- (NSDictionary *)layoutItemForId:(NSString *)itemId {
    NSString *targetId = R2CString(itemId);
    if ([targetId length] <= 0) return nil;
    for (id rawItem in [self nativeLayoutItems]) {
        if (![rawItem isKindOfClass:[NSDictionary class]]) continue;
        NSDictionary *item = (NSDictionary *)rawItem;
        if ([R2CString(item[@"id"]) isEqualToString:targetId]) return item;
    }
    return nil;
}

- (NSDictionary *)layoutItemAtIndex:(NSInteger)index {
    NSArray *items = [self nativeLayoutItems];
    if (index < 0 || index >= (NSInteger)[items count]) return nil;
    id rawItem = items[(NSUInteger)index];
    return [rawItem isKindOfClass:[NSDictionary class]] ? (NSDictionary *)rawItem : nil;
}

- (NSInteger)layoutItemIndexForId:(NSString *)itemId {
    NSString *targetId = R2CString(itemId);
    if ([targetId length] <= 0) return -1;
    NSArray *items = [self nativeLayoutItems];
    for (NSUInteger i = 0; i < [items count]; i += 1) {
        id rawItem = items[i];
        if (![rawItem isKindOfClass:[NSDictionary class]]) continue;
        if ([R2CString(((NSDictionary *)rawItem)[@"id"]) isEqualToString:targetId]) return (NSInteger)i;
    }
    return -1;
}

- (CGFloat)itemRenderWidthForState:(NSDictionary *)stateDoc item:(NSDictionary *)item {
    CGFloat width = R2CCGFloat(item[@"width"], 0.0);
    if (!R2CBool(item[@"stretch_x"], NO)) return width;
    CGFloat itemX = R2CCGFloat(item[@"x"], 0.0);
    CGFloat trailing = MAX(0.0, [self baseWindowWidth] - itemX - width);
    CGFloat windowWidth = R2CCGFloat(stateDoc[@"window_width"], self.sessionWidth);
    return R2CClamp(windowWidth - itemX - trailing, 0.0, 20000.0);
}

- (CGFloat)itemRenderHeightForState:(NSDictionary *)stateDoc item:(NSDictionary *)item {
    CGFloat height = R2CCGFloat(item[@"height"], 0.0);
    if (!R2CBool(item[@"stretch_y"], NO)) return height;
    CGFloat itemY = R2CCGFloat(item[@"y"], 0.0);
    CGFloat bottomGap = MAX(0.0, [self baseWindowHeight] - itemY - height);
    CGFloat windowHeight = R2CCGFloat(stateDoc[@"window_height"], self.sessionHeight);
    return R2CClamp(windowHeight - itemY - bottomGap, 0.0, 20000.0);
}

- (CGFloat)itemViewportYForState:(NSDictionary *)stateDoc item:(NSDictionary *)item {
    CGFloat itemY = R2CCGFloat(item[@"y"], 0.0);
    if ([R2CString(item[@"layer"]) isEqualToString:@"content"]) {
        return itemY - R2CCGFloat(stateDoc[@"scroll_offset_y"], self.scrollOffsetY);
    }
    return itemY;
}

- (BOOL)itemVisibleForState:(NSDictionary *)stateDoc item:(NSDictionary *)item {
    if ([R2CString(item[@"id"]) isEqualToString:@"root"]) return NO;
    CGFloat viewportY = [self itemViewportYForState:stateDoc item:item];
    CGFloat height = [self itemRenderHeightForState:stateDoc item:item];
    CGFloat top = viewportY;
    CGFloat bottom = viewportY + height;
    CGFloat windowHeight = R2CCGFloat(stateDoc[@"window_height"], self.sessionHeight);
    if (bottom <= 0.0) return NO;
    if (top >= windowHeight) return NO;
    return YES;
}

- (NSDictionary *)selectionDocForItem:(NSDictionary *)item state:(NSDictionary *)stateDoc {
    if (![item isKindOfClass:[NSDictionary class]]) {
        return @{
            @"ready": @NO,
            @"id": @"",
            @"source_node_id": @"",
            @"source_module_path": @"",
            @"source_component_name": @"",
            @"source_line": @0,
            @"interactive": @NO,
            @"kind": @"",
            @"plan_role": @"",
            @"visual_role": @"",
            @"x": @0,
            @"y": @0,
            @"width": @0,
            @"height": @0,
        };
    }
    return @{
        @"ready": @([R2CString(item[@"id"]) length] > 0),
        @"id": R2CString(item[@"id"]),
        @"source_node_id": R2CString(item[@"source_node_id"]),
        @"source_module_path": R2CString(item[@"source_module_path"]),
        @"source_component_name": R2CString(item[@"source_component_name"]),
        @"source_line": @(R2CInteger(item[@"source_line"], 0)),
        @"interactive": @(R2CBool(item[@"interactive"], NO)),
        @"kind": R2CString(item[@"kind"]),
        @"plan_role": R2CString(item[@"plan_role"]),
        @"visual_role": R2CString(item[@"visual_role"]),
        @"x": @(R2CCGFloat(item[@"x"], 0.0)),
        @"y": @(R2CCGFloat(item[@"y"], 0.0)),
        @"width": @([self itemRenderWidthForState:stateDoc item:item]),
        @"height": @([self itemRenderHeightForState:stateDoc item:item]),
    };
}

- (NSDictionary *)styleLayoutSurfaceDoc {
    id doc = self.sessionDoc[@"style_layout_surface"];
    return [doc isKindOfClass:[NSDictionary class]] ? (NSDictionary *)doc : nil;
}

- (NSDictionary *)selectedLayoutItemDoc {
    id doc = self.runtimeStateDoc[@"selected_item"];
    if ([doc isKindOfClass:[NSDictionary class]] && R2CBool(doc[@"ready"], NO)) {
        return (NSDictionary *)doc;
    }
    return [self layoutItemForId:self.selectedItemId];
}

- (NSDictionary *)selectedSourceNodeDoc {
    NSDictionary *layoutSurface = [self layoutSurfaceDoc];
    NSArray *nodes = [layoutSurface[@"nodes"] isKindOfClass:[NSArray class]] ? layoutSurface[@"nodes"] : nil;
    NSString *nodeId = R2CString(self.selectedSourceNodeId);
    if ([nodeId length] == 0) return nil;
    for (id rawNode in nodes ?: @[]) {
        if (![rawNode isKindOfClass:[NSDictionary class]]) continue;
        NSDictionary *node = (NSDictionary *)rawNode;
        if ([R2CString(node[@"id"]) isEqualToString:nodeId]) return node;
    }
    return nil;
}

- (NSDictionary *)selectedStyleNodeDoc {
    NSDictionary *styleSurface = [self styleLayoutSurfaceDoc];
    NSArray *nodes = [styleSurface[@"nodes"] isKindOfClass:[NSArray class]] ? styleSurface[@"nodes"] : nil;
    NSString *nodeId = R2CString(self.selectedSourceNodeId);
    if ([nodeId length] == 0) return nil;
    for (id rawNode in nodes ?: @[]) {
        if (![rawNode isKindOfClass:[NSDictionary class]]) continue;
        NSDictionary *node = (NSDictionary *)rawNode;
        if ([R2CString(node[@"node_id"]) isEqualToString:nodeId]) return node;
    }
    return nil;
}

- (NSString *)resolvedSelectedSourcePath {
    NSString *modulePath = R2CTrimmedText(self.selectedSourceModulePath);
    if ([modulePath length] == 0) return @"";
    if ([modulePath hasPrefix:@"/"]) return modulePath;
    NSString *repoRoot = R2CTrimmedText(self.repoRoot);
    if ([repoRoot length] == 0) return modulePath;
    return [repoRoot stringByAppendingPathComponent:modulePath];
}

- (void)clearSourceJumpState {
    self.sourceJumpAttempted = NO;
    self.sourceJumpSucceeded = NO;
    self.sourceJumpCommand = @"";
    self.sourceJumpTargetPath = @"";
    self.sourceJumpError = @"";
    self.sourceJumpTargetLine = 0;
}

- (void)attemptSourceJumpForCurrentSelection {
    [self clearSourceJumpState];
    if (!self.openSourceOnClick) return;
    self.sourceJumpAttempted = YES;
    NSString *targetPath = [self resolvedSelectedSourcePath];
    NSInteger targetLine = self.selectedSourceLine > 0 ? self.selectedSourceLine : 1;
    self.sourceJumpTargetPath = targetPath ?: @"";
    self.sourceJumpTargetLine = targetLine;
    if ([targetPath length] == 0) {
        self.sourceJumpError = @"missing_source_path";
        return;
    }
    if (![[NSFileManager defaultManager] fileExistsAtPath:targetPath]) {
        self.sourceJumpError = [NSString stringWithFormat:@"source_file_missing:%@", targetPath];
        return;
    }
    NSMutableArray<NSString *> *args = [NSMutableArray arrayWithObjects:@"--background", nil];
    if (targetLine > 0) {
        [args addObject:@"--line"];
        [args addObject:[NSString stringWithFormat:@"%ld", (long)targetLine]];
    }
    [args addObject:targetPath];

    NSMutableArray<NSString *> *quotedArgs = [NSMutableArray array];
    for (NSString *arg in args) {
        [quotedArgs addObject:R2CShellQuote(arg)];
    }
    self.sourceJumpCommand = [NSString stringWithFormat:@"/usr/bin/xed %@", [quotedArgs componentsJoinedByString:@" "]];

    NSPipe *pipe = [NSPipe pipe];
    NSTask *task = [[NSTask alloc] init];
    task.launchPath = @"/usr/bin/xed";
    task.arguments = args;
    task.standardOutput = pipe;
    task.standardError = pipe;
    @try {
        [task launch];
    } @catch (NSException *exception) {
        self.sourceJumpError = [NSString stringWithFormat:@"xed_launch_exception:%@", R2CString([exception reason])];
        return;
    }
    [task waitUntilExit];
    NSData *outputData = [[pipe fileHandleForReading] readDataToEndOfFile];
    NSString *output = [[NSString alloc] initWithData:outputData encoding:NSUTF8StringEncoding];
    if ([task terminationStatus] == 0) {
        self.sourceJumpSucceeded = YES;
        self.sourceJumpError = @"";
        return;
    }
    NSString *trimmedOutput = R2CTrimmedText(output);
    if ([trimmedOutput length] > 0) {
        self.sourceJumpError = [NSString stringWithFormat:@"xed_exit_%d:%@", [task terminationStatus], trimmedOutput];
    } else {
        self.sourceJumpError = [NSString stringWithFormat:@"xed_exit_%d", [task terminationStatus]];
    }
}

- (NSUInteger)visibleLayoutItemCount {
    return (NSUInteger)R2CInteger(self.runtimeStateDoc[@"visible_layout_item_count"], 0);
}

- (NSString *)argumentValueForFlag:(NSString *)flag inArguments:(NSArray<NSString *> *)arguments {
    NSString *target = R2CString(flag);
    if ([target length] <= 0) return @"";
    for (NSUInteger i = 0; i < [arguments count]; i += 1) {
        NSString *arg = R2CString(arguments[i]);
        if ([arg isEqualToString:target]) {
            if (i + 1 < [arguments count]) return R2CString(arguments[i + 1]);
            return @"";
        }
        NSString *eqPrefix = [target stringByAppendingString:@"="];
        NSString *colonPrefix = [target stringByAppendingString:@":"];
        if ([arg hasPrefix:eqPrefix]) return [arg substringFromIndex:[eqPrefix length]];
        if ([arg hasPrefix:colonPrefix]) return [arg substringFromIndex:[colonPrefix length]];
    }
    return @"";
}

- (NSDictionary *)runtimeReduceFieldsFromText:(NSString *)outputText error:(NSString **)outError {
    NSString *trimmed = R2CTrimmedText(outputText);
    if ([trimmed length] <= 0) {
        if (outError != NULL) *outError = @"runtime_reduce_empty";
        return nil;
    }
    NSMutableDictionary<NSString *, NSString *> *fields = [NSMutableDictionary dictionary];
    [trimmed enumerateLinesUsingBlock:^(NSString *line, BOOL *stop) {
        NSString *text = R2CTrimmedText(line);
        if ([text length] <= 0) return;
        NSRange pivot = [text rangeOfString:@"="];
        if (pivot.location == NSNotFound || pivot.location == 0) return;
        NSString *key = [text substringToIndex:pivot.location];
        NSString *value = [text substringFromIndex:(pivot.location + 1)];
        if (![key length]) return;
        fields[key] = value ?: @"";
        stop[0] = NO;
    }];
    if (![[R2CString(fields[@"format"]) copy] isEqualToString:@"native_gui_runtime_reduce_kv_v1"]) {
        if (outError != NULL) *outError = [NSString stringWithFormat:@"runtime_reduce_format_mismatch:%@", R2CString(fields[@"format"])];
        return nil;
    }
    if (!R2CParseBoolText(fields[@"ready"], NO)) {
        NSString *reason = R2CString(fields[@"reason"]);
        if (outError != NULL) *outError = [NSString stringWithFormat:@"runtime_reduce_unready:%@", [reason length] > 0 ? reason : @"unknown"];
        return nil;
    }
    return @{
        @"window_width": @(R2CInteger(fields[@"window_width"], 0)),
        @"window_height": @(R2CInteger(fields[@"window_height"], 0)),
        @"content_height": @(R2CInteger(fields[@"content_height"], 0)),
        @"scroll_height": @(R2CInteger(fields[@"scroll_height"], 0)),
        @"scroll_offset_y": @(R2CInteger(fields[@"scroll_offset_y"], 0)),
        @"click_count": @(R2CInteger(fields[@"click_count"], 0)),
        @"resize_count": @(R2CInteger(fields[@"resize_count"], 0)),
        @"scroll_count": @(R2CInteger(fields[@"scroll_count"], 0)),
        @"key_count": @(R2CInteger(fields[@"key_count"], 0)),
        @"text_count": @(R2CInteger(fields[@"text_count"], 0)),
        @"focus_count": @(R2CInteger(fields[@"focus_count"], 0)),
        @"has_last_click": @(R2CParseBoolText(fields[@"has_last_click"], NO)),
        @"last_click_x": @(R2CInteger(fields[@"last_click_x"], 0)),
        @"last_click_y": @(R2CInteger(fields[@"last_click_y"], 0)),
        @"selected_index": @(R2CInteger(fields[@"selected_index"], -1)),
        @"focused_index": @(R2CInteger(fields[@"focused_index"], -1)),
        @"visible_layout_item_count": @(R2CInteger(fields[@"visible_layout_item_count"], 0)),
        @"reason": R2CString(fields[@"reason"]),
    };
}

- (NSDictionary *)buildStateDocFromReducedFields:(NSDictionary *)reduced eventType:(NSString *)eventType arguments:(NSArray<NSString *> *)arguments {
    NSString *windowTitle = R2CString(self.runtimeStateDoc[@"window_title"]);
    if ([windowTitle length] <= 0) windowTitle = R2CString(self.sessionDoc[@"window"][@"title"]);
    NSString *routeState = R2CString(self.runtimeStateDoc[@"route_state"]);
    if ([routeState length] <= 0) routeState = R2CString(self.sessionDoc[@"window"][@"route_state"]);
    NSString *entryModule = R2CString(self.runtimeStateDoc[@"entry_module"]);
    if ([entryModule length] <= 0) entryModule = R2CString(self.sessionDoc[@"window"][@"entry_module"]);

    NSString *typedText = R2CString(self.runtimeStateDoc[@"typed_text"]);
    NSString *lastKey = R2CString(self.runtimeStateDoc[@"last_key"]);
    NSString *eventKind = R2CString(eventType);
    if ([eventKind isEqualToString:@"key"]) {
        NSString *nextKey = [self argumentValueForFlag:@"--event-key" inArguments:arguments];
        if ([nextKey length] > 0) {
            lastKey = nextKey;
            if ([nextKey isEqualToString:@"Backspace"]
                && [R2CString(self.focusedItemId) length] > 0
                && [typedText length] > 0) {
                typedText = [typedText substringToIndex:([typedText length] - 1)];
            }
        }
    } else if ([eventKind isEqualToString:@"text"]) {
        NSString *appendText = [self argumentValueForFlag:@"--event-text" inArguments:arguments];
        if ([appendText length] > 0 && [R2CString(self.focusedItemId) length] > 0) {
            typedText = [typedText stringByAppendingString:appendText];
        }
    }

    NSDictionary *selectedItem = [self layoutItemAtIndex:R2CInteger(reduced[@"selected_index"], -1)];
    NSDictionary *focusedItem = [self layoutItemAtIndex:R2CInteger(reduced[@"focused_index"], -1)];
    if (!R2CBool(focusedItem[@"interactive"], NO)) focusedItem = nil;
    NSDictionary *selectionDoc = [self selectionDocForItem:selectedItem state:reduced];

    NSMutableDictionary *stateDoc = [NSMutableDictionary dictionary];
    stateDoc[@"window_title"] = windowTitle ?: @"";
    stateDoc[@"route_state"] = routeState ?: @"";
    stateDoc[@"entry_module"] = entryModule ?: @"";
    stateDoc[@"window_width"] = @((NSInteger)R2CInteger(reduced[@"window_width"], (NSInteger)self.sessionWidth));
    stateDoc[@"window_height"] = @((NSInteger)R2CInteger(reduced[@"window_height"], (NSInteger)self.sessionHeight));
    stateDoc[@"content_height"] = @((NSInteger)R2CInteger(reduced[@"content_height"], (NSInteger)self.contentHeight));
    stateDoc[@"scroll_height"] = @((NSInteger)R2CInteger(reduced[@"scroll_height"], (NSInteger)[self maxScrollOffset]));
    stateDoc[@"scroll_offset_y"] = @((NSInteger)R2CInteger(reduced[@"scroll_offset_y"], (NSInteger)self.scrollOffsetY));
    stateDoc[@"click_count"] = @((NSInteger)R2CInteger(reduced[@"click_count"], (NSInteger)self.clickCount));
    stateDoc[@"resize_count"] = @((NSInteger)R2CInteger(reduced[@"resize_count"], (NSInteger)self.resizeCount));
    stateDoc[@"scroll_count"] = @((NSInteger)R2CInteger(reduced[@"scroll_count"], (NSInteger)self.scrollCount));
    stateDoc[@"key_count"] = @((NSInteger)R2CInteger(reduced[@"key_count"], (NSInteger)self.keyCount));
    stateDoc[@"text_count"] = @((NSInteger)R2CInteger(reduced[@"text_count"], (NSInteger)self.textCount));
    stateDoc[@"focus_count"] = @((NSInteger)R2CInteger(reduced[@"focus_count"], (NSInteger)self.focusCount));
    stateDoc[@"has_last_click"] = @((BOOL)R2CBool(reduced[@"has_last_click"], self.hasLastClick));
    stateDoc[@"last_click_x"] = @((NSInteger)R2CInteger(reduced[@"last_click_x"], (NSInteger)self.lastClickX));
    stateDoc[@"last_click_y"] = @((NSInteger)R2CInteger(reduced[@"last_click_y"], (NSInteger)self.lastClickY));
    stateDoc[@"selected_item_id"] = R2CString(selectionDoc[@"id"]);
    stateDoc[@"selected_source_node_id"] = R2CString(selectionDoc[@"source_node_id"]);
    stateDoc[@"selected_source_module_path"] = R2CString(selectionDoc[@"source_module_path"]);
    stateDoc[@"selected_source_component_name"] = R2CString(selectionDoc[@"source_component_name"]);
    stateDoc[@"selected_source_line"] = selectionDoc[@"source_line"] ?: @0;
    stateDoc[@"selected_item_interactive"] = selectionDoc[@"interactive"] ?: @NO;
    stateDoc[@"focused_item_id"] = focusedItem != nil ? R2CString(focusedItem[@"id"]) : @"";
    stateDoc[@"typed_text"] = typedText ?: @"";
    stateDoc[@"last_key"] = lastKey ?: @"";
    stateDoc[@"visible_layout_item_count"] = @((NSInteger)R2CInteger(reduced[@"visible_layout_item_count"], 0));
    stateDoc[@"selected_item"] = selectionDoc;
    return stateDoc;
}

- (NSDictionary *)buildRenderPlanDocForState:(NSDictionary *)stateDoc {
    NSMutableArray<NSDictionary *> *commands = [NSMutableArray array];
    CGFloat windowWidth = R2CCGFloat(stateDoc[@"window_width"], self.sessionWidth);
    CGFloat windowHeight = R2CCGFloat(stateDoc[@"window_height"], self.sessionHeight);
    CGFloat contentHeight = R2CCGFloat(stateDoc[@"content_height"], self.contentHeight);
    CGFloat scrollHeight = R2CCGFloat(stateDoc[@"scroll_height"], [self maxScrollOffset]);
    CGFloat scrollOffsetY = R2CCGFloat(stateDoc[@"scroll_offset_y"], self.scrollOffsetY);
    NSString *selectedItemId = R2CString(stateDoc[@"selected_item_id"]);
    NSString *focusedItemId = R2CString(stateDoc[@"focused_item_id"]);
    NSString *typedText = R2CString(stateDoc[@"typed_text"]);

    [commands addObject:@{
        @"type": @"background_gradient",
        @"x": @0,
        @"y": @0,
        @"width": @(windowWidth),
        @"height": @(windowHeight),
        @"color_top": [self themeText:@"background_top" fallback:@"#f2f7fd"],
        @"color_bottom": [self themeText:@"background_bottom" fallback:@"#e6eefb"],
        @"z_index": @0,
    }];
    [commands addObject:@{
        @"type": @"rounded_panel",
        @"x": @10,
        @"y": @10,
        @"width": @(MAX(0.0, windowWidth - 20.0)),
        @"height": @(MAX(0.0, windowHeight - 20.0)),
        @"corner_radius": @24,
        @"background_color": [self themeText:@"panel_background" fallback:@"#ffffff"],
        @"shadow_color": [self themeText:@"panel_shadow" fallback:@"#cddaf0"],
        @"border_color": [self themeText:@"border_color" fallback:@"#d5e2f0"],
        @"z_index": @1,
    }];

    for (id rawItem in [self nativeLayoutItems]) {
        if (![rawItem isKindOfClass:[NSDictionary class]]) continue;
        NSDictionary *item = (NSDictionary *)rawItem;
        if (![self itemVisibleForState:stateDoc item:item]) continue;
        CGFloat viewportY = [self itemViewportYForState:stateDoc item:item];
        CGFloat width = [self itemRenderWidthForState:stateDoc item:item];
        CGFloat height = [self itemRenderHeightForState:stateDoc item:item];
        BOOL selected = [[R2CString(item[@"id"]) copy] isEqualToString:selectedItemId];
        BOOL focused = [[R2CString(item[@"id"]) copy] isEqualToString:focusedItemId];
        NSString *detailText = R2CString(item[@"detail_text"]);
        if (focused && [typedText length] > 0) {
            detailText = [detailText length] > 0
                ? [NSString stringWithFormat:@"%@  input: %@", detailText, typedText]
                : [NSString stringWithFormat:@"input: %@", typedText];
        }
        NSMutableDictionary *command = [@{
            @"type": @"item_card",
            @"item_id": R2CString(item[@"id"]),
            @"kind": R2CString(item[@"kind"]),
            @"x": @(R2CCGFloat(item[@"x"], 0.0)),
            @"y": @(viewportY),
            @"width": @(width),
            @"height": @(height),
            @"z_index": @((NSInteger)R2CInteger(item[@"z_index"], 0)),
            @"corner_radius": @((NSInteger)R2CInteger(item[@"corner_radius"], 0)),
            @"line_width": @(selected ? 2 : 1),
            @"background_color": selected ? [self themeText:@"accent_soft" fallback:@"#dbeafe"] : R2CString(item[@"background_color"]),
            @"border_color": selected ? [self themeText:@"accent_color" fallback:@"#3b82f6"] : R2CString(item[@"border_color"]),
            @"text_color": R2CString(item[@"text_color"]),
            @"detail_color": R2CString(item[@"detail_color"]),
            @"text": R2CString(item[@"text"]),
            @"detail_text": detailText ?: @"",
            @"font_size": @((NSInteger)R2CInteger(item[@"font_size"], 0)),
            @"font_weight": R2CString(item[@"font_weight"]),
            @"selected": @(selected),
            @"focused": @(focused),
        } mutableCopy];
        [commands addObject:command];
        if (focused) {
            [commands addObject:@{
                @"type": @"focus_ring",
                @"item_id": R2CString(item[@"id"]),
                @"x": @(R2CCGFloat(item[@"x"], 0.0)),
                @"y": @(viewportY),
                @"width": @(width),
                @"height": @(height),
                @"corner_radius": @((NSInteger)R2CInteger(item[@"corner_radius"], 0) + 4),
                @"border_color": [self themeText:@"focus_color" fallback:@"#f59e0b"],
                @"line_width": @2,
                @"z_index": @((NSInteger)R2CInteger(item[@"z_index"], 0) + 2),
            }];
        }
    }

    if (R2CBool(stateDoc[@"has_last_click"], NO)) {
        [commands addObject:@{
            @"type": @"interaction_marker",
            @"x": @((NSInteger)R2CInteger(stateDoc[@"last_click_x"], 0)),
            @"y": @((NSInteger)R2CInteger(stateDoc[@"last_click_y"], 0)),
            @"accent_color": [self themeText:@"accent_color" fallback:@"#3b82f6"],
            @"z_index": @999,
        }];
    }

    return @{
        @"format": @"native_render_plan_v1",
        @"ready": @YES,
        @"window_title": R2CString(stateDoc[@"window_title"]),
        @"route_state": R2CString(stateDoc[@"route_state"]),
        @"entry_module": R2CString(stateDoc[@"entry_module"]),
        @"window_width": @(windowWidth),
        @"window_height": @(windowHeight),
        @"content_height": @(contentHeight),
        @"scroll_height": @(scrollHeight),
        @"scroll_offset_y": @(scrollOffsetY),
        @"visible_layout_item_count": @((NSInteger)R2CInteger(stateDoc[@"visible_layout_item_count"], 0)),
        @"selected_item_id": selectedItemId ?: @"",
        @"focused_item_id": focusedItemId ?: @"",
        @"typed_text": typedText ?: @"",
        @"commands": commands,
        @"command_count": @((NSInteger)[commands count]),
    };
}

- (BOOL)applyRuntimeStateDoc:(NSDictionary *)stateDoc renderPlan:(NSDictionary *)renderPlan triggerSourceJump:(BOOL)triggerSourceJump {
    if (![stateDoc isKindOfClass:[NSDictionary class]] || ![renderPlan isKindOfClass:[NSDictionary class]]) {
        self.runtimeReady = NO;
        self.runtimeError = @"runtime_state_or_render_plan_invalid";
        return NO;
    }
    self.runtimeReady = YES;
    self.runtimeError = @"";
    [self applyRuntimeStateDoc:stateDoc];
    [self applyRenderPlanDoc:renderPlan];
    [self setNeedsDisplay:YES];
    if (triggerSourceJump) {
        [self attemptSourceJumpForCurrentSelection];
    }
    return YES;
}

- (BOOL)applyRuntimeReduceText:(NSString *)outputText eventType:(NSString *)eventType arguments:(NSArray<NSString *> *)arguments triggerSourceJump:(BOOL)triggerSourceJump {
    NSString *reduceError = nil;
    NSDictionary *reduced = [self runtimeReduceFieldsFromText:outputText error:&reduceError];
    if (reduced == nil) {
        self.runtimeReady = NO;
        self.runtimeError = reduceError ?: @"runtime_reduce_parse_failed";
        return NO;
    }
    NSDictionary *stateDoc = [self buildStateDocFromReducedFields:reduced eventType:eventType arguments:arguments];
    NSDictionary *renderPlan = [self buildRenderPlanDocForState:stateDoc];
    return [self applyRuntimeStateDoc:stateDoc renderPlan:renderPlan triggerSourceJump:triggerSourceJump];
}

- (void)applyRuntimeStateDoc:(NSDictionary *)stateDoc {
    self.runtimeStateDoc = stateDoc ?: @{};
    self.sessionWidth = R2CCGFloat(stateDoc[@"window_width"], self.sessionWidth);
    self.sessionHeight = R2CCGFloat(stateDoc[@"window_height"], self.sessionHeight);
    self.contentHeight = R2CCGFloat(stateDoc[@"content_height"], self.contentHeight);
    self.scrollOffsetY = R2CCGFloat(stateDoc[@"scroll_offset_y"], self.scrollOffsetY);
    self.clickCount = (NSUInteger)R2CInteger(stateDoc[@"click_count"], (NSInteger)self.clickCount);
    self.resizeCount = (NSUInteger)R2CInteger(stateDoc[@"resize_count"], (NSInteger)self.resizeCount);
    self.scrollCount = (NSUInteger)R2CInteger(stateDoc[@"scroll_count"], (NSInteger)self.scrollCount);
    self.keyCount = (NSUInteger)R2CInteger(stateDoc[@"key_count"], (NSInteger)self.keyCount);
    self.textCount = (NSUInteger)R2CInteger(stateDoc[@"text_count"], (NSInteger)self.textCount);
    self.focusCount = (NSUInteger)R2CInteger(stateDoc[@"focus_count"], (NSInteger)self.focusCount);
    self.hasLastClick = R2CBool(stateDoc[@"has_last_click"], self.hasLastClick);
    self.lastClickX = R2CCGFloat(stateDoc[@"last_click_x"], self.lastClickX);
    self.lastClickY = R2CCGFloat(stateDoc[@"last_click_y"], self.lastClickY);
    self.selectedItemId = R2CString(stateDoc[@"selected_item_id"]);
    self.selectedSourceNodeId = R2CString(stateDoc[@"selected_source_node_id"]);
    self.selectedSourceModulePath = R2CString(stateDoc[@"selected_source_module_path"]);
    self.selectedSourceComponentName = R2CString(stateDoc[@"selected_source_component_name"]);
    self.selectedSourceLine = R2CInteger(stateDoc[@"selected_source_line"], self.selectedSourceLine);
    self.selectedItemInteractive = R2CBool(stateDoc[@"selected_item_interactive"], self.selectedItemInteractive);
    self.focusedItemId = R2CString(stateDoc[@"focused_item_id"]);
    self.typedText = R2CString(stateDoc[@"typed_text"]);
    self.lastKey = R2CString(stateDoc[@"last_key"]);
    NSDictionary *selectedItem = [self selectedLayoutItemDoc];
    self.selectedItemKind = R2CString(selectedItem[@"kind"]);
    self.selectedItemPlanRole = R2CString(selectedItem[@"plan_role"]);
    self.selectedItemVisualRole = R2CString(selectedItem[@"visual_role"]);
    self.selectedItemX = R2CCGFloat(selectedItem[@"x"], 0.0);
    self.selectedItemY = R2CCGFloat(selectedItem[@"y"], 0.0);
    self.selectedItemWidth = R2CCGFloat(selectedItem[@"width"], 0.0);
    self.selectedItemHeight = R2CCGFloat(selectedItem[@"height"], 0.0);
    [self setFrameSize:NSMakeSize(self.sessionWidth, self.sessionHeight)];
}

- (void)applyRenderPlanDoc:(NSDictionary *)renderPlan {
    self.renderPlanDoc = renderPlan ?: @{};
    NSArray *commands = [renderPlan[@"commands"] isKindOfClass:[NSArray class]] ? renderPlan[@"commands"] : nil;
    self.renderCommands = commands ?: @[];
    self.contentHeight = R2CCGFloat(renderPlan[@"content_height"], self.contentHeight);
}

- (BOOL)applyRuntimeDoc:(NSDictionary *)runtimeDoc {
    if (![runtimeDoc isKindOfClass:[NSDictionary class]]) {
        self.runtimeReady = NO;
        self.runtimeError = @"runtime_doc_invalid";
        return NO;
    }
    if (![R2CString(runtimeDoc[@"format"]) isEqualToString:@"native_gui_runtime_v1"]) {
        self.runtimeReady = NO;
        self.runtimeError = [NSString stringWithFormat:@"runtime_format_mismatch:%@", R2CString(runtimeDoc[@"format"])];
        return NO;
    }
    NSDictionary *stateDoc = [runtimeDoc[@"state"] isKindOfClass:[NSDictionary class]] ? runtimeDoc[@"state"] : nil;
    NSDictionary *renderPlan = [runtimeDoc[@"render_plan"] isKindOfClass:[NSDictionary class]] ? runtimeDoc[@"render_plan"] : nil;
    if (stateDoc == nil || renderPlan == nil) {
        self.runtimeReady = NO;
        self.runtimeError = @"runtime_doc_missing_state_or_render_plan";
        return NO;
    }
    return [self applyRuntimeStateDoc:stateDoc renderPlan:renderPlan triggerSourceJump:NO];
}

- (NSMutableArray<NSString *> *)runtimeStateArguments {
    NSMutableArray<NSString *> *args = [NSMutableArray array];
    [args addObjectsFromArray:@[@"--state-window-width", [NSString stringWithFormat:@"%.0f", self.sessionWidth]]];
    [args addObjectsFromArray:@[@"--state-window-height", [NSString stringWithFormat:@"%.0f", self.sessionHeight]]];
    [args addObjectsFromArray:@[@"--state-scroll-offset-y", [NSString stringWithFormat:@"%.0f", self.scrollOffsetY]]];
    [args addObjectsFromArray:@[@"--state-click-count", [NSString stringWithFormat:@"%lu", (unsigned long)self.clickCount]]];
    [args addObjectsFromArray:@[@"--state-resize-count", [NSString stringWithFormat:@"%lu", (unsigned long)self.resizeCount]]];
    [args addObjectsFromArray:@[@"--state-scroll-count", [NSString stringWithFormat:@"%lu", (unsigned long)self.scrollCount]]];
    [args addObjectsFromArray:@[@"--state-key-count", [NSString stringWithFormat:@"%lu", (unsigned long)self.keyCount]]];
    [args addObjectsFromArray:@[@"--state-text-count", [NSString stringWithFormat:@"%lu", (unsigned long)self.textCount]]];
    [args addObjectsFromArray:@[@"--state-focus-count", [NSString stringWithFormat:@"%lu", (unsigned long)self.focusCount]]];
    if (self.hasLastClick) {
        [args addObjectsFromArray:@[@"--state-has-last-click", @"true"]];
        [args addObjectsFromArray:@[@"--state-last-click-x", [NSString stringWithFormat:@"%.0f", self.lastClickX]]];
        [args addObjectsFromArray:@[@"--state-last-click-y", [NSString stringWithFormat:@"%.0f", self.lastClickY]]];
    }
    if ([self.selectedItemId length] > 0) [args addObjectsFromArray:@[@"--state-selected-item-id", self.selectedItemId]];
    if ([self.focusedItemId length] > 0) [args addObjectsFromArray:@[@"--state-focused-item-id", self.focusedItemId]];
    if ([self.typedText length] > 0) [args addObjectsFromArray:@[@"--state-typed-text", self.typedText]];
    if ([self.lastKey length] > 0) [args addObjectsFromArray:@[@"--state-last-key", self.lastKey]];
    return args;
}

- (BOOL)runRuntimeEventType:(NSString *)eventType arguments:(NSArray<NSString *> *)arguments triggerSourceJump:(BOOL)triggerSourceJump {
    NSString *runtimeExePath = R2CTrimmedText(self.runtimeExePath);
    if ([runtimeExePath length] == 0) {
        self.runtimeReady = NO;
        self.runtimeError = @"runtime_exe_missing";
        return NO;
    }
    NSMutableArray<NSString *> *args = [self runtimeStateArguments];
    [args addObjectsFromArray:@[@"--event-type", R2CString(eventType)]];
    [args addObjectsFromArray:arguments ?: @[]];
    NSPipe *pipe = [NSPipe pipe];
    NSTask *task = [[NSTask alloc] init];
    task.launchPath = runtimeExePath;
    task.arguments = args;
    task.standardOutput = pipe;
    task.standardError = pipe;
    @try {
        [task launch];
    } @catch (NSException *exception) {
        self.runtimeReady = NO;
        self.runtimeError = [NSString stringWithFormat:@"runtime_launch_exception:%@", R2CString([exception reason])];
        return NO;
    }
    [task waitUntilExit];
    NSData *outputData = [[pipe fileHandleForReading] readDataToEndOfFile];
    NSString *outputText = [[NSString alloc] initWithData:outputData encoding:NSUTF8StringEncoding];
    if ([task terminationStatus] != 0) {
        self.runtimeReady = NO;
        NSString *trimmed = R2CTrimmedText(outputText);
        if ([trimmed length] > 0) {
            self.runtimeError = [NSString stringWithFormat:@"runtime_exit_%d:%@", [task terminationStatus], trimmed];
        } else {
            self.runtimeError = [NSString stringWithFormat:@"runtime_exit_%d", [task terminationStatus]];
        }
        return NO;
    }
    NSData *jsonData = [outputText dataUsingEncoding:NSUTF8StringEncoding];
    NSString *trimmedOutput = R2CTrimmedText(outputText);
    if ([trimmedOutput hasPrefix:@"{"]) {
        NSError *jsonError = nil;
        NSDictionary *runtimeDoc = [NSJSONSerialization JSONObjectWithData:jsonData options:0 error:&jsonError];
        if (![self applyRuntimeDoc:runtimeDoc]) {
            if (jsonError != nil && [R2CString(self.runtimeError) length] == 0) {
                self.runtimeError = [NSString stringWithFormat:@"runtime_json_error:%@", R2CString([jsonError localizedDescription])];
            }
            return NO;
        }
        if (triggerSourceJump) {
            [self attemptSourceJumpForCurrentSelection];
        }
        return YES;
    }
    return [self applyRuntimeReduceText:outputText eventType:eventType arguments:arguments triggerSourceJump:triggerSourceJump];
}

- (void)handleClickAtSessionX:(CGFloat)x y:(CGFloat)y {
    NSArray<NSString *> *args = @[
        @"--event-x", [NSString stringWithFormat:@"%.0f", R2CClamp(x, 0.0, self.sessionWidth)],
        @"--event-y", [NSString stringWithFormat:@"%.0f", R2CClamp(y, 0.0, self.sessionHeight)],
    ];
    if (![self runRuntimeEventType:@"click" arguments:args triggerSourceJump:YES]) {
        [NSApp terminate:nil];
    }
}

- (void)handleScrollDelta:(CGFloat)delta {
    NSArray<NSString *> *args = @[
        @"--event-delta-y", [NSString stringWithFormat:@"%.0f", delta],
    ];
    if (![self runRuntimeEventType:@"scroll" arguments:args triggerSourceJump:NO]) {
        [NSApp terminate:nil];
    }
}

- (void)handleResizeToWidth:(CGFloat)width height:(CGFloat)height {
    NSArray<NSString *> *args = @[
        @"--event-width", [NSString stringWithFormat:@"%.0f", width],
        @"--event-height", [NSString stringWithFormat:@"%.0f", height],
    ];
    if (![self runRuntimeEventType:@"resize" arguments:args triggerSourceJump:NO]) {
        [NSApp terminate:nil];
    }
}

- (void)handleFocusForItemId:(NSString *)itemId {
    NSMutableArray<NSString *> *args = [NSMutableArray array];
    if ([R2CString(itemId) length] > 0) {
        [args addObjectsFromArray:@[@"--event-focus-item-id", R2CString(itemId)]];
    }
    if (![self runRuntimeEventType:@"focus" arguments:args triggerSourceJump:NO]) {
        [NSApp terminate:nil];
    }
}

- (void)handleKeyText:(NSString *)keyText {
    if ([R2CString(keyText) length] <= 0) return;
    NSArray<NSString *> *args = @[
        @"--event-key", R2CString(keyText),
    ];
    if (![self runRuntimeEventType:@"key" arguments:args triggerSourceJump:NO]) {
        [NSApp terminate:nil];
    }
}

- (void)handleTextInput:(NSString *)text {
    if ([R2CString(text) length] <= 0) return;
    NSArray<NSString *> *args = @[
        @"--event-text", R2CString(text),
    ];
    if (![self runRuntimeEventType:@"text" arguments:args triggerSourceJump:NO]) {
        [NSApp terminate:nil];
    }
}

- (NSRect)inspectorPanelFrame {
    CGFloat availableWidth = MAX(220.0, self.bounds.size.width - 32.0);
    CGFloat panelWidth = MIN(320.0, availableWidth);
    CGFloat panelHeight = [R2CString(self.selectedItemId) length] > 0 ? 232.0 : 126.0;
    CGFloat originX = MAX(16.0, self.bounds.size.width - panelWidth - 16.0);
    CGFloat originY = 16.0;
    return NSMakeRect(originX, originY, panelWidth, panelHeight);
}

- (NSArray<NSString *> *)inspectorLines {
    NSMutableArray<NSString *> *lines = [NSMutableArray array];
    NSDictionary *layoutItem = [self selectedLayoutItemDoc];
    NSDictionary *sourceNode = [self selectedSourceNodeDoc];
    NSDictionary *styleNode = [self selectedStyleNodeDoc];
    if (layoutItem == nil) {
        [lines addObject:@"click a source-backed card to inspect"];
        [lines addObject:[NSString stringWithFormat:@"route: %@", R2CString(self.sessionDoc[@"window"][@"route_state"])]];
        [lines addObject:[NSString stringWithFormat:@"scroll: y=%.0f  visible=%lu", self.scrollOffsetY, (unsigned long)[self visibleLayoutItemCount]]];
        if ([R2CString(self.runtimeError) length] > 0) {
            [lines addObject:[NSString stringWithFormat:@"runtime: %@", R2CString(self.runtimeError)]];
        }
        return lines;
    }

    NSString *modulePath = R2CString(self.selectedSourceModulePath);
    NSString *componentName = R2CString(self.selectedSourceComponentName);
    NSInteger sourceLine = self.selectedSourceLine;
    NSString *kind = R2CString(layoutItem[@"kind"]);
    NSString *planRole = R2CString(layoutItem[@"plan_role"]);
    NSString *visualRole = [R2CString(styleNode[@"visual_role"]) length] > 0 ? R2CString(styleNode[@"visual_role"]) : R2CString(layoutItem[@"visual_role"]);
    NSString *badge = R2CString(styleNode[@"badge_text"]);
    NSString *classPreview = R2CString(sourceNode[@"class_preview"]);
    NSString *traits = R2CJoinedPreview(sourceNode[@"style_traits"], 4);
    NSString *frameText = [NSString stringWithFormat:@"frame: x=%.0f y=%.0f w=%.0f h=%.0f",
                           R2CCGFloat(layoutItem[@"x"], 0.0),
                           R2CCGFloat(layoutItem[@"y"], 0.0),
                           R2CCGFloat(layoutItem[@"width"], 0.0),
                           R2CCGFloat(layoutItem[@"height"], 0.0)];
    [lines addObject:[NSString stringWithFormat:@"source: %@", modulePath]];
    [lines addObject:[NSString stringWithFormat:@"component: %@  line: %ld", componentName, (long)sourceLine]];
    [lines addObject:[NSString stringWithFormat:@"node: %@  kind: %@", R2CString(sourceNode[@"label"]), kind]];
    [lines addObject:frameText];
    [lines addObject:[NSString stringWithFormat:@"role: %@ / %@ / %@", planRole, visualRole, badge]];
    [lines addObject:[NSString stringWithFormat:@"interactive: %@", self.selectedItemInteractive ? @"true" : @"false"]];
    if ([R2CString(self.focusedItemId) length] > 0 || [R2CString(self.typedText) length] > 0) {
        [lines addObject:[NSString stringWithFormat:@"focus: %@  text: %@", R2CString(self.focusedItemId), R2CString(self.typedText)]];
    }
    if ([classPreview length] > 0) {
        [lines addObject:[NSString stringWithFormat:@"class: %@", classPreview]];
    } else if ([traits length] > 0) {
        [lines addObject:[NSString stringWithFormat:@"traits: %@", traits]];
    }
    if (self.openSourceOnClick) {
        if (self.sourceJumpSucceeded) {
            [lines addObject:[NSString stringWithFormat:@"jump: ok -> %@:%ld", R2CString(self.sourceJumpTargetPath), (long)self.sourceJumpTargetLine]];
        } else if (self.sourceJumpAttempted) {
            [lines addObject:[NSString stringWithFormat:@"jump: fail -> %@", R2CString(self.sourceJumpError)]];
        } else {
            [lines addObject:@"jump: pending"];
        }
    } else {
        [lines addObject:@"jump: disabled"];
    }
    return lines;
}

- (NSString *)inspectorTitle {
    if ([R2CString(self.selectedItemId) length] > 0) {
        return [NSString stringWithFormat:@"Inspector  %@", R2CString(self.selectedItemId)];
    }
    return @"Inspector";
}

- (NSString *)inspectorLinesJsonText {
    NSArray<NSString *> *lines = [self inspectorLines];
    NSError *jsonError = nil;
    NSData *jsonData = [NSJSONSerialization dataWithJSONObject:lines options:0 error:&jsonError];
    if (jsonData == nil || jsonError != nil) return @"[]";
    NSString *jsonText = [[NSString alloc] initWithData:jsonData encoding:NSUTF8StringEncoding];
    return jsonText ?: @"[]";
}

- (void)drawInspectorPanel {
    NSRect panelRect = [self inspectorPanelFrame];
    NSBezierPath *panel = [NSBezierPath bezierPathWithRoundedRect:panelRect xRadius:18.0 yRadius:18.0];
    [[self themeColor:@"panel_background" fallback:[NSColor colorWithCalibratedWhite:1.0 alpha:0.96]] setFill];
    [panel fill];
    [[self themeColor:@"border_color" fallback:[NSColor colorWithCalibratedRed:0.78 green:0.85 blue:0.93 alpha:0.95]] setStroke];
    [panel setLineWidth:1.0];
    [panel stroke];

    NSString *title = [self inspectorTitle];
    NSDictionary *titleAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:15.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [self themeColor:@"text_primary"
                                                fallback:[NSColor colorWithCalibratedRed:0.15 green:0.20 blue:0.27 alpha:1.0]]
    };
    NSDictionary *lineAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: [self themeColor:@"text_muted"
                                                fallback:[NSColor colorWithCalibratedRed:0.38 green:0.46 blue:0.55 alpha:1.0]]
    };
    NSRect titleRect = NSMakeRect(panelRect.origin.x + 14.0, panelRect.origin.y + panelRect.size.height - 28.0, panelRect.size.width - 28.0, 20.0);
    [title drawInRect:titleRect withAttributes:titleAttrs];

    NSMutableParagraphStyle *lineStyle = [[NSMutableParagraphStyle alloc] init];
    lineStyle.lineBreakMode = NSLineBreakByTruncatingTail;
    NSMutableDictionary *finalLineAttrs = [lineAttrs mutableCopy];
    finalLineAttrs[NSParagraphStyleAttributeName] = lineStyle;
    CGFloat lineY = panelRect.origin.y + panelRect.size.height - 52.0;
    for (NSString *line in [self inspectorLines]) {
        if (lineY < (panelRect.origin.y + 10.0)) break;
        NSRect lineRect = NSMakeRect(panelRect.origin.x + 14.0, lineY, panelRect.size.width - 28.0, 16.0);
        [R2CString(line) drawInRect:lineRect withAttributes:finalLineAttrs];
        lineY -= 18.0;
    }
}

- (void)mouseDown:(NSEvent *)event {
    if (R2CScriptedScenarioActive) return;
    NSPoint local = [self convertPoint:[event locationInWindow] fromView:nil];
    if (NSPointInRect(local, [self inspectorPanelFrame])) return;
    CGFloat sessionX = R2CClamp(local.x, 0.0, self.sessionWidth);
    CGFloat sessionY = R2CClamp(self.sessionHeight - local.y, 0.0, self.sessionHeight);
    [self handleClickAtSessionX:sessionX y:sessionY];
}

- (void)scrollWheel:(NSEvent *)event {
    if (R2CScriptedScenarioActive) return;
    CGFloat delta = [event hasPreciseScrollingDeltas] ? [event scrollingDeltaY] : ([event deltaY] * 12.0);
    [self handleScrollDelta:delta];
}

- (void)keyDown:(NSEvent *)event {
    if (R2CScriptedScenarioActive) return;
    NSString *characters = R2CString([event charactersIgnoringModifiers]);
    if ([characters length] <= 0) {
        characters = [NSString stringWithFormat:@"keycode:%hu", [event keyCode]];
    }
    [self handleKeyText:characters];
    NSEventModifierFlags modifiers = [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
    BOOL allowText = modifiers == 0;
    if (allowText
        && [characters length] > 0
        && ![characters isEqualToString:@"\t"]
        && ![characters isEqualToString:@"\r"]
        && ![characters isEqualToString:@"\n"]
        && ![characters isEqualToString:@"\x1b"]
        && ![characters isEqualToString:@"\x7f"]) {
        [self handleTextInput:characters];
    }
}

- (NSDictionary *)cardAttributesForCommand:(NSDictionary *)command {
    CGFloat fontSize = R2CCGFloat(command[@"font_size"], 14.0);
    NSFontWeight weight = R2CFontWeightFromText(command[@"font_weight"], NSFontWeightRegular);
    NSColor *color = R2CColorFromHexString(command[@"text_color"],
                                           [self themeColor:@"text_primary"
                                                   fallback:[NSColor colorWithCalibratedRed:0.15 green:0.20 blue:0.27 alpha:1.0]]);
    return @{
        NSFontAttributeName: [NSFont systemFontOfSize:fontSize weight:weight],
        NSForegroundColorAttributeName: color
    };
}

- (NSDictionary *)detailAttributesForCommand:(NSDictionary *)command {
    NSColor *color = R2CColorFromHexString(command[@"detail_color"],
                                           [self themeColor:@"text_muted"
                                                   fallback:[NSColor colorWithCalibratedRed:0.42 green:0.50 blue:0.58 alpha:1.0]]);
    return @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: color
    };
}

- (void)drawRoundedPanelCommand:(NSDictionary *)command frame:(NSRect)frame {
    NSBezierPath *panel = [NSBezierPath bezierPathWithRoundedRect:frame
                                                         xRadius:R2CCGFloat(command[@"corner_radius"], 24.0)
                                                         yRadius:R2CCGFloat(command[@"corner_radius"], 24.0)];
    NSShadow *shadow = [[NSShadow alloc] init];
    shadow.shadowBlurRadius = 18.0;
    shadow.shadowOffset = NSMakeSize(0.0, -2.0);
    shadow.shadowColor = [R2CColorFromHexString(command[@"shadow_color"],
                                                [self themeColor:@"panel_shadow"
                                                        fallback:[NSColor colorWithCalibratedRed:0.80 green:0.86 blue:0.94 alpha:1.0]]) colorWithAlphaComponent:0.35];
    [NSGraphicsContext saveGraphicsState];
    [shadow set];
    [R2CColorFromHexString(command[@"background_color"], [self themeColor:@"panel_background" fallback:[NSColor whiteColor]]) setFill];
    [panel fill];
    [NSGraphicsContext restoreGraphicsState];
    [R2CColorFromHexString(command[@"border_color"], [self themeColor:@"border_color" fallback:[NSColor lightGrayColor]]) setStroke];
    [panel setLineWidth:1.0];
    [panel stroke];
}

- (void)drawItemCardCommand:(NSDictionary *)command frame:(NSRect)frame {
    NSBezierPath *card = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(frame, 0.0, -6.0)
                                                        xRadius:R2CCGFloat(command[@"corner_radius"], 14.0)
                                                        yRadius:R2CCGFloat(command[@"corner_radius"], 14.0)];
    [R2CColorFromHexString(command[@"background_color"], [NSColor colorWithCalibratedWhite:1.0 alpha:0.80]) setFill];
    [card fill];
    [R2CColorFromHexString(command[@"border_color"], [self themeColor:@"border_color" fallback:[NSColor lightGrayColor]]) setStroke];
    [card setLineWidth:R2CCGFloat(command[@"line_width"], 1.0)];
    [card stroke];

    NSString *text = R2CString(command[@"text"]);
    if ([text length] <= 0) return;
    NSString *detailText = R2CString(command[@"detail_text"]);
    NSRect textRect = NSInsetRect(frame, 12.0, 4.0);
    NSMutableParagraphStyle *style = [[NSMutableParagraphStyle alloc] init];
    style.lineBreakMode = NSLineBreakByTruncatingTail;
    style.alignment = NSTextAlignmentLeft;
    NSMutableDictionary *attrs = [[self cardAttributesForCommand:command] mutableCopy];
    attrs[NSParagraphStyleAttributeName] = style;
    if ([detailText length] > 0) {
        CGFloat detailHeight = MIN(12.0, MAX(10.0, textRect.size.height * 0.36));
        NSRect detailRect = NSMakeRect(textRect.origin.x, textRect.origin.y, textRect.size.width, detailHeight);
        NSRect titleRect = NSMakeRect(textRect.origin.x,
                                      textRect.origin.y + detailHeight + 2.0,
                                      textRect.size.width,
                                      MAX(0.0, textRect.size.height - detailHeight - 2.0));
        [text drawInRect:titleRect withAttributes:attrs];
        NSMutableParagraphStyle *detailStyle = [[NSMutableParagraphStyle alloc] init];
        detailStyle.lineBreakMode = NSLineBreakByTruncatingTail;
        detailStyle.alignment = NSTextAlignmentLeft;
        NSMutableDictionary *detailAttrs = [[self detailAttributesForCommand:command] mutableCopy];
        detailAttrs[NSParagraphStyleAttributeName] = detailStyle;
        [detailText drawInRect:detailRect withAttributes:detailAttrs];
    } else {
        [text drawInRect:textRect withAttributes:attrs];
    }
}

- (void)drawFocusRingCommand:(NSDictionary *)command frame:(NSRect)frame {
    NSBezierPath *ring = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(frame, -2.0, -8.0)
                                                        xRadius:R2CCGFloat(command[@"corner_radius"], 18.0)
                                                        yRadius:R2CCGFloat(command[@"corner_radius"], 18.0)];
    [R2CColorFromHexString(command[@"border_color"], [NSColor orangeColor]) setStroke];
    [ring setLineWidth:R2CCGFloat(command[@"line_width"], 2.0)];
    [ring stroke];
}

- (void)drawInteractionMarkerCommand:(NSDictionary *)command {
    CGFloat drawX = R2CCGFloat(command[@"x"], 0.0);
    CGFloat drawY = R2CClamp(self.sessionHeight - R2CCGFloat(command[@"y"], 0.0), 0.0, self.sessionHeight);
    NSColor *accent = R2CColorFromHexString(command[@"accent_color"],
                                            [self themeColor:@"accent_color"
                                                    fallback:[NSColor colorWithCalibratedRed:0.23 green:0.51 blue:0.96 alpha:1.0]]);
    NSRect outerRect = NSMakeRect(drawX - 9.0, drawY - 9.0, 18.0, 18.0);
    NSBezierPath *outer = [NSBezierPath bezierPathWithOvalInRect:outerRect];
    [[accent colorWithAlphaComponent:0.25] setFill];
    [outer fill];
    [accent setStroke];
    [outer setLineWidth:2.0];
    [outer stroke];

    NSRect innerRect = NSMakeRect(drawX - 3.0, drawY - 3.0, 6.0, 6.0);
    NSBezierPath *inner = [NSBezierPath bezierPathWithOvalInRect:innerRect];
    [[NSColor whiteColor] setFill];
    [inner fill];
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
    for (id rawCommand in self.renderCommands ?: @[]) {
        if (![rawCommand isKindOfClass:[NSDictionary class]]) continue;
        NSDictionary *command = (NSDictionary *)rawCommand;
        NSString *type = R2CString(command[@"type"]);
        if ([type isEqualToString:@"background_gradient"]) {
            NSGradient *gradient = [[NSGradient alloc] initWithStartingColor:R2CColorFromHexString(command[@"color_top"], [NSColor whiteColor])
                                                                 endingColor:R2CColorFromHexString(command[@"color_bottom"], [NSColor lightGrayColor])];
            [gradient drawInRect:self.bounds angle:270.0];
            continue;
        }
        if ([type isEqualToString:@"interaction_marker"]) {
            [self drawInteractionMarkerCommand:command];
            continue;
        }
        NSRect frame = [self frameForRenderCommand:command];
        if (!NSIntersectsRect(frame, self.bounds) && ![type isEqualToString:@"rounded_panel"]) continue;
        if ([type isEqualToString:@"rounded_panel"]) {
            [self drawRoundedPanelCommand:command frame:frame];
        } else if ([type isEqualToString:@"item_card"]) {
            [self drawItemCardCommand:command frame:frame];
        } else if ([type isEqualToString:@"focus_ring"]) {
            [self drawFocusRingCommand:command frame:frame];
        }
    }
    [self drawInspectorPanel];
}

@end

@interface R2CAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSDictionary *sessionDoc;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, copy) NSString *screenshotPath;
@property(nonatomic, copy) NSString *renderPlanOutPath;
@property(nonatomic, copy) NSString *runtimeStateOutPath;
@property(nonatomic, assign) NSInteger autoCloseMs;
@property(nonatomic, assign) NSInteger waitAfterClickMs;
@property(nonatomic, assign) BOOL hasRequestedClick;
@property(nonatomic, assign) CGFloat requestedClickX;
@property(nonatomic, assign) CGFloat requestedClickY;
@property(nonatomic, assign) BOOL hasRequestedResize;
@property(nonatomic, assign) CGFloat requestedWidth;
@property(nonatomic, assign) CGFloat requestedHeight;
@property(nonatomic, assign) BOOL hasRequestedScroll;
@property(nonatomic, assign) CGFloat requestedScrollY;
@property(nonatomic, copy) NSString *requestedFocusItemId;
@property(nonatomic, copy) NSString *requestedText;
@property(nonatomic, copy) NSString *requestedKey;
@property(nonatomic, assign) BOOL windowOpened;
@property(nonatomic, assign) BOOL screenshotWritten;
@property(nonatomic, assign) BOOL didEmitSummary;
@property(nonatomic, assign) BOOL didRequestTerminate;
@property(nonatomic, assign) BOOL scriptedScenarioActive;
@property(nonatomic, assign) BOOL suppressNextResizeCallback;
@property(nonatomic, copy) NSString *repoRoot;
@property(nonatomic, assign) BOOL openSourceOnClick;
@end

@implementation R2CAppDelegate

- (R2CSessionView *)sessionView {
    if (self.window == nil) return nil;
    if (![self.window.contentView isKindOfClass:[R2CSessionView class]]) return nil;
    return (R2CSessionView *)self.window.contentView;
}

- (void)writePreviewToStdout {
    if (self.didEmitSummary) return;
    self.didEmitSummary = YES;
    NSDictionary *windowDoc = self.sessionDoc[@"window"];
    NSString *title = R2CString(windowDoc[@"title"]);
    NSString *entryModule = R2CString(windowDoc[@"entry_module"]);
    NSString *routeState = R2CString(windowDoc[@"route_state"]);
    R2CSessionView *view = [self sessionView];
    NSString *layoutItemCountText = [NSString stringWithFormat:@"%lu", (unsigned long)(view != nil ? [view visibleLayoutItemCount] : 0)];
    NSString *clickCountText = [NSString stringWithFormat:@"%lu", (unsigned long)(view != nil ? view.clickCount : 0)];
    NSString *resizeCountText = [NSString stringWithFormat:@"%lu", (unsigned long)(view != nil ? view.resizeCount : 0)];
    NSString *scrollCountText = [NSString stringWithFormat:@"%lu", (unsigned long)(view != nil ? view.scrollCount : 0)];
    NSString *keyCountText = [NSString stringWithFormat:@"%lu", (unsigned long)(view != nil ? view.keyCount : 0)];
    NSString *textCountText = [NSString stringWithFormat:@"%lu", (unsigned long)(view != nil ? view.textCount : 0)];
    NSString *focusCountText = [NSString stringWithFormat:@"%lu", (unsigned long)(view != nil ? view.focusCount : 0)];
    NSString *lastClickXText = (view != nil && view.hasLastClick) ? [NSString stringWithFormat:@"%.2f", view.lastClickX] : @"";
    NSString *lastClickYText = (view != nil && view.hasLastClick) ? [NSString stringWithFormat:@"%.2f", view.lastClickY] : @"";
    NSString *scrollOffsetYText = view != nil ? [NSString stringWithFormat:@"%.2f", view.scrollOffsetY] : @"0.00";
    NSString *focusedItemIdText = view != nil ? R2CString(view.focusedItemId) : @"";
    NSString *typedText = view != nil ? R2CString(view.typedText) : @"";
    NSString *lastKeyText = view != nil ? R2CString(view.lastKey) : @"";
    NSString *hitItemIdText = view != nil ? R2CString(view.selectedItemId) : @"";
    NSString *hitSourceNodeIdText = view != nil ? R2CString(view.selectedSourceNodeId) : @"";
    NSString *hitSourceModulePathText = view != nil ? R2CString(view.selectedSourceModulePath) : @"";
    NSString *hitSourceComponentNameText = view != nil ? R2CString(view.selectedSourceComponentName) : @"";
    NSString *hitSourceLineText = (view != nil && view.selectedSourceLine > 0) ? [NSString stringWithFormat:@"%ld", (long)view.selectedSourceLine] : @"";
    NSString *inspectorTitleText = view != nil ? [view inspectorTitle] : @"Inspector";
    NSString *inspectorLinesJsonText = view != nil ? [view inspectorLinesJsonText] : @"[]";
    NSRect inspectorPanelRect = view != nil ? [view inspectorPanelFrame] : NSMakeRect(0.0, 0.0, 0.0, 0.0);
    NSString *sourceJumpTargetPathText = view != nil ? R2CString(view.sourceJumpTargetPath) : @"";
    NSString *sourceJumpTargetLineText = (view != nil && view.sourceJumpTargetLine > 0) ? [NSString stringWithFormat:@"%ld", (long)view.sourceJumpTargetLine] : @"";
    NSString *sourceJumpCommandText = view != nil ? R2CString(view.sourceJumpCommand) : @"";
    NSString *sourceJumpErrorText = view != nil ? R2CString(view.sourceJumpError) : @"";
    NSString *runtimeReadyText = (view != nil && view.runtimeReady) ? @"true" : @"false";
    NSString *runtimeErrorText = view != nil ? R2CString(view.runtimeError) : @"";
    NSString *renderPlanReadyText = (view != nil && [view.renderPlanDoc isKindOfClass:[NSDictionary class]]) ? @"true" : @"false";
    NSString *renderPlanCommandCountText = [NSString stringWithFormat:@"%ld", (long)(view != nil ? R2CInteger(view.renderPlanDoc[@"command_count"], 0) : 0)];
    NSString *windowWidthText = [NSString stringWithFormat:@"%.0f", view != nil ? view.sessionWidth : R2CCGFloat(windowDoc[@"width"], 0.0)];
    NSString *windowHeightText = [NSString stringWithFormat:@"%.0f", view != nil ? view.sessionHeight : R2CCGFloat(windowDoc[@"height"], 0.0)];
    printf("format=native_gui_run_v1\n");
    printf("window_opened=%s\n", self.windowOpened ? "true" : "false");
    printf("screenshot_written=%s\n", self.screenshotWritten ? "true" : "false");
    printf("window_title=%s\n", [title UTF8String]);
    printf("entry_module=%s\n", [entryModule UTF8String]);
    printf("route_state=%s\n", [routeState UTF8String]);
    printf("layout_item_count=%s\n", [layoutItemCountText UTF8String]);
    printf("click_count=%s\n", [clickCountText UTF8String]);
    printf("resize_count=%s\n", [resizeCountText UTF8String]);
    printf("scroll_count=%s\n", [scrollCountText UTF8String]);
    printf("key_count=%s\n", [keyCountText UTF8String]);
    printf("text_count=%s\n", [textCountText UTF8String]);
    printf("focus_count=%s\n", [focusCountText UTF8String]);
    printf("last_click_x=%s\n", [lastClickXText UTF8String]);
    printf("last_click_y=%s\n", [lastClickYText UTF8String]);
    printf("scroll_offset_y=%s\n", [scrollOffsetYText UTF8String]);
    printf("visible_layout_item_count=%s\n", [layoutItemCountText UTF8String]);
    printf("focused_item_id=%s\n", [focusedItemIdText UTF8String]);
    printf("typed_text=%s\n", [typedText UTF8String]);
    printf("last_key=%s\n", [lastKeyText UTF8String]);
    printf("hit_item_id=%s\n", [hitItemIdText UTF8String]);
    printf("hit_source_node_id=%s\n", [hitSourceNodeIdText UTF8String]);
    printf("hit_source_module_path=%s\n", [hitSourceModulePathText UTF8String]);
    printf("hit_source_component_name=%s\n", [hitSourceComponentNameText UTF8String]);
    printf("hit_source_line=%s\n", [hitSourceLineText UTF8String]);
    printf("hit_item_interactive=%s\n", (view != nil && view.selectedItemInteractive) ? "true" : "false");
    printf("inspector_ready=%s\n", (view != nil && [view.selectedItemId length] > 0) ? "true" : "false");
    printf("inspector_title=%s\n", [inspectorTitleText UTF8String]);
    printf("inspector_panel_x=%.2f\n", inspectorPanelRect.origin.x);
    printf("inspector_panel_y=%.2f\n", inspectorPanelRect.origin.y);
    printf("inspector_panel_width=%.2f\n", inspectorPanelRect.size.width);
    printf("inspector_panel_height=%.2f\n", inspectorPanelRect.size.height);
    printf("inspector_lines_json=%s\n", [inspectorLinesJsonText UTF8String]);
    printf("source_jump_enabled=%s\n", (view != nil && view.openSourceOnClick) ? "true" : "false");
    printf("source_jump_attempted=%s\n", (view != nil && view.sourceJumpAttempted) ? "true" : "false");
    printf("source_jump_ok=%s\n", (view != nil && view.sourceJumpSucceeded) ? "true" : "false");
    printf("source_jump_target_path=%s\n", [sourceJumpTargetPathText UTF8String]);
    printf("source_jump_target_line=%s\n", [sourceJumpTargetLineText UTF8String]);
    printf("source_jump_command=%s\n", [sourceJumpCommandText UTF8String]);
    printf("source_jump_error=%s\n", [sourceJumpErrorText UTF8String]);
    printf("runtime_ready=%s\n", [runtimeReadyText UTF8String]);
    printf("runtime_error=%s\n", [runtimeErrorText UTF8String]);
    printf("render_plan_ready=%s\n", [renderPlanReadyText UTF8String]);
    printf("render_plan_command_count=%s\n", [renderPlanCommandCountText UTF8String]);
    printf("window_width=%s\n", [windowWidthText UTF8String]);
    printf("window_height=%s\n", [windowHeightText UTF8String]);
    fflush(stdout);
}

- (void)writeScreenshotIfNeeded {
    if (self.screenshotPath.length == 0 || self.window == nil) return;
    NSView *view = self.window.contentView;
    if (view == nil) return;
    [view displayIfNeeded];
    NSBitmapImageRep *rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    if (rep == nil) return;
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
    NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if (png == nil) return;
    NSString *dir = [self.screenshotPath stringByDeletingLastPathComponent];
    if (dir.length > 0) {
        [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    }
    self.screenshotWritten = [png writeToFile:self.screenshotPath atomically:YES];
}

- (void)writeJsonDoc:(NSDictionary *)doc toPath:(NSString *)path {
    if (path.length == 0 || ![doc isKindOfClass:[NSDictionary class]]) return;
    NSError *jsonError = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:doc options:NSJSONWritingPrettyPrinted error:&jsonError];
    if (data == nil || jsonError != nil) return;
    NSString *dir = [path stringByDeletingLastPathComponent];
    if (dir.length > 0) {
        [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    }
    [data writeToFile:path atomically:YES];
}

- (void)terminateApp {
    if (self.didRequestTerminate) return;
    self.didRequestTerminate = YES;
    R2CSessionView *view = [self sessionView];
    [self writeJsonDoc:view.renderPlanDoc toPath:self.renderPlanOutPath];
    [self writeJsonDoc:view.runtimeStateDoc toPath:self.runtimeStateOutPath];
    [self writePreviewToStdout];
    R2CScriptedScenarioActive = NO;
    [NSApp terminate:nil];
}

- (void)applyRequestedResize {
    if (!self.hasRequestedResize || self.window == nil) return;
    NSSize contentSize = NSMakeSize(R2CClamp(self.requestedWidth, 220.0, 20000.0),
                                    R2CClamp(self.requestedHeight, 220.0, 20000.0));
    self.suppressNextResizeCallback = YES;
    [self.window setContentSize:contentSize];
    R2CSessionView *view = [self sessionView];
    if (view != nil) {
        [view handleResizeToWidth:contentSize.width height:contentSize.height];
    }
}

- (void)applyRequestedClick {
    if (!self.hasRequestedClick) return;
    R2CSessionView *view = [self sessionView];
    if (view == nil) return;
    [view handleClickAtSessionX:self.requestedClickX y:self.requestedClickY];
}

- (void)applyRequestedScroll {
    if (!self.hasRequestedScroll) return;
    R2CSessionView *view = [self sessionView];
    if (view == nil) return;
    NSArray<NSString *> *args = @[
        @"--event-scroll-to", [NSString stringWithFormat:@"%.0f", self.requestedScrollY],
    ];
    if (![view runRuntimeEventType:@"scroll" arguments:args triggerSourceJump:NO]) {
        [NSApp terminate:nil];
    }
}

- (void)applyRequestedFocus {
    if ([R2CString(self.requestedFocusItemId) length] <= 0) return;
    R2CSessionView *view = [self sessionView];
    if (view == nil) return;
    [view handleFocusForItemId:self.requestedFocusItemId];
}

- (void)applyRequestedText {
    if ([R2CString(self.requestedText) length] <= 0) return;
    R2CSessionView *view = [self sessionView];
    if (view == nil) return;
    [view handleTextInput:self.requestedText];
}

- (void)applyRequestedKey {
    if ([R2CString(self.requestedKey) length] <= 0) return;
    R2CSessionView *view = [self sessionView];
    if (view == nil) return;
    [view handleKeyText:self.requestedKey];
}

- (void)scheduleScenario {
    __weak typeof(self) weakSelf = self;
    NSInteger cursorMs = 120;
    self.scriptedScenarioActive = self.hasRequestedResize
        || self.hasRequestedScroll
        || self.hasRequestedClick
        || [R2CString(self.requestedFocusItemId) length] > 0
        || [R2CString(self.requestedText) length] > 0
        || [R2CString(self.requestedKey) length] > 0;
    R2CScriptedScenarioActive = self.scriptedScenarioActive;
    if (self.hasRequestedResize) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(cursorMs * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
            [weakSelf applyRequestedResize];
        });
        cursorMs += 140;
    }
    if (self.hasRequestedScroll) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(cursorMs * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
            [weakSelf applyRequestedScroll];
        });
        cursorMs += 140;
    }
    if (self.hasRequestedClick) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(cursorMs * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
            [weakSelf applyRequestedClick];
        });
        cursorMs += (self.waitAfterClickMs > 0 ? self.waitAfterClickMs : 140);
    } else {
        cursorMs += 120;
    }
    if ([R2CString(self.requestedFocusItemId) length] > 0) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(cursorMs * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
            [weakSelf applyRequestedFocus];
        });
        cursorMs += 120;
    }
    if ([R2CString(self.requestedText) length] > 0) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(cursorMs * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
            [weakSelf applyRequestedText];
        });
        cursorMs += 120;
    }
    if ([R2CString(self.requestedKey) length] > 0) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(cursorMs * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
            [weakSelf applyRequestedKey];
        });
        cursorMs += 120;
    }

    NSInteger screenshotDelay = cursorMs;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(screenshotDelay * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
        [weakSelf writeScreenshotIfNeeded];
    });

    NSInteger closeDelay = self.autoCloseMs > 0 ? self.autoCloseMs : 0;
    NSInteger minimumCloseDelay = screenshotDelay + 180;
    if (closeDelay <= 0 || closeDelay < minimumCloseDelay) {
        closeDelay = minimumCloseDelay;
    }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(closeDelay * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
        [weakSelf terminateApp];
    });
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    NSDictionary *windowDoc = self.sessionDoc[@"window"];
    CGFloat width = R2CCGFloat(windowDoc[@"width"], 390.0);
    CGFloat height = R2CCGFloat(windowDoc[@"height"], 844.0);
    NSString *title = R2CString(windowDoc[@"title"]);
    BOOL resizable = R2CBool(windowDoc[@"resizable"], NO) || self.hasRequestedResize;

    NSRect rect = NSMakeRect(0.0, 0.0, width, height);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    if (resizable) style |= NSWindowStyleMaskResizable;
    self.window = [[NSWindow alloc] initWithContentRect:rect styleMask:style backing:NSBackingStoreBuffered defer:NO];
    [self.window setTitle:title];
    [self.window center];
    [self.window setDelegate:self];

    R2CSessionView *view = [[R2CSessionView alloc] initWithFrame:rect];
    NSDictionary *planDoc = [self.sessionDoc[@"native_layout_plan"] isKindOfClass:[NSDictionary class]] ? self.sessionDoc[@"native_layout_plan"] : @{};
    NSDictionary *renderPlanDoc = [self.sessionDoc[@"native_render_plan"] isKindOfClass:[NSDictionary class]] ? self.sessionDoc[@"native_render_plan"] : @{};
    NSDictionary *runtimeStateDoc = [self.sessionDoc[@"native_gui_runtime_state"] isKindOfClass:[NSDictionary class]] ? self.sessionDoc[@"native_gui_runtime_state"] : @{};
    NSDictionary *runtimeDoc = [self.sessionDoc[@"native_gui_runtime"] isKindOfClass:[NSDictionary class]] ? self.sessionDoc[@"native_gui_runtime"] : @{};
    view.sessionDoc = self.sessionDoc;
    view.theme = self.sessionDoc[@"theme"];
    view.renderPlanDoc = renderPlanDoc;
    view.runtimeStateDoc = runtimeStateDoc;
    view.renderCommands = [renderPlanDoc[@"commands"] isKindOfClass:[NSArray class]] ? renderPlanDoc[@"commands"] : @[];
    view.sessionWidth = width;
    view.sessionHeight = height;
    view.contentHeight = MAX(height, R2CCGFloat(planDoc[@"content_height"], R2CCGFloat(windowDoc[@"content_height"], height)));
    view.scrollOffsetY = R2CCGFloat(runtimeStateDoc[@"scroll_offset_y"], 0.0);
    view.runtimeExePath = R2CString(runtimeDoc[@"runtime_exe_path"]);
    view.runtimeReady = YES;
    view.runtimeError = @"";
    view.repoRoot = self.repoRoot ?: @"";
    view.openSourceOnClick = self.openSourceOnClick;
    [view clearSourceJumpState];
    [view applyRuntimeStateDoc:runtimeStateDoc];
    [view applyRenderPlanDoc:renderPlanDoc];
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [self.window setContentView:view];
    [self.window makeFirstResponder:view];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    self.windowOpened = YES;
    [self scheduleScenario];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

- (void)windowDidResize:(NSNotification *)notification {
    (void)notification;
    if (self.suppressNextResizeCallback) {
        self.suppressNextResizeCallback = NO;
        return;
    }
    R2CSessionView *view = [self sessionView];
    if (view == nil) return;
    NSRect bounds = view.bounds;
    CGFloat deltaWidth = bounds.size.width - view.sessionWidth;
    if (deltaWidth < 0.0) deltaWidth = -deltaWidth;
    CGFloat deltaHeight = bounds.size.height - view.sessionHeight;
    if (deltaHeight < 0.0) deltaHeight = -deltaHeight;
    if (deltaWidth < 0.5 && deltaHeight < 0.5) return;
    [view handleResizeToWidth:bounds.size.width height:bounds.size.height];
}

- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    [self terminateApp];
}

@end

static NSString *R2CArgValue(int argc, const char *argv[], const char *flagName, NSString *fallback) {
    for (int i = 1; i < argc; i += 1) {
        const char *arg = argv[i];
        if (strcmp(arg, flagName) == 0) {
            if (i + 1 < argc) {
                return [NSString stringWithUTF8String:argv[i + 1]];
            }
            return fallback;
        }
        size_t flagLen = strlen(flagName);
        if (strncmp(arg, flagName, flagLen) == 0 && arg[flagLen] == '=') {
            return [NSString stringWithUTF8String:(arg + flagLen + 1)];
        }
    }
    return fallback;
}

static BOOL R2CHasFlag(int argc, const char *argv[], const char *flagName) {
    for (int i = 1; i < argc; i += 1) {
        if (strcmp(argv[i], flagName) == 0) return YES;
    }
    return NO;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSString *sessionPath = R2CArgValue(argc, argv, "--session", nil);
        NSString *repoRoot = R2CArgValue(argc, argv, "--repo-root", @"");
        NSString *screenshotPath = R2CArgValue(argc, argv, "--screenshot-out", @"");
        NSString *renderPlanOutPath = R2CArgValue(argc, argv, "--render-plan-out", @"");
        NSString *runtimeStateOutPath = R2CArgValue(argc, argv, "--runtime-state-out", @"");
        NSString *autoCloseText = R2CArgValue(argc, argv, "--auto-close-ms", @"0");
        NSString *clickText = R2CArgValue(argc, argv, "--click", @"");
        NSString *resizeText = R2CArgValue(argc, argv, "--resize", @"");
        NSString *scrollText = R2CArgValue(argc, argv, "--scroll-y", @"");
        NSString *focusItemId = R2CArgValue(argc, argv, "--focus-item-id", @"");
        NSString *requestedText = R2CArgValue(argc, argv, "--type-text", @"");
        NSString *requestedKey = R2CArgValue(argc, argv, "--send-key", @"");
        NSString *waitAfterClickText = R2CArgValue(argc, argv, "--wait-after-click-ms", @"0");
        BOOL openSourceOnClick = R2CHasFlag(argc, argv, "--open-source-on-click");
        if (sessionPath == nil || [sessionPath length] == 0) {
            fprintf(stderr, "native_gui_host_macos: missing --session\n");
            return 2;
        }

        CGFloat requestedClickX = 0.0;
        CGFloat requestedClickY = 0.0;
        CGFloat requestedWidth = 0.0;
        CGFloat requestedHeight = 0.0;
        CGFloat requestedScrollY = 0.0;
        BOOL hasRequestedClick = NO;
        BOOL hasRequestedResize = NO;
        BOOL hasRequestedScroll = NO;
        if ([clickText length] > 0) {
            hasRequestedClick = R2CParsePairSpec(clickText, @",", &requestedClickX, &requestedClickY);
            if (!hasRequestedClick) {
                fprintf(stderr, "native_gui_host_macos: invalid --click, expected x,y\n");
                return 2;
            }
        }
        if ([resizeText length] > 0) {
            NSString *normalized = [[R2CString(resizeText) lowercaseString] stringByReplacingOccurrencesOfString:@"×" withString:@"x"];
            hasRequestedResize = R2CParsePairSpec(normalized, @"x", &requestedWidth, &requestedHeight);
            if (!hasRequestedResize) {
                fprintf(stderr, "native_gui_host_macos: invalid --resize, expected WxH\n");
                return 2;
            }
        }
        if ([scrollText length] > 0) {
            hasRequestedScroll = R2CParseCGFloatText(scrollText, &requestedScrollY);
            if (!hasRequestedScroll) {
                fprintf(stderr, "native_gui_host_macos: invalid --scroll-y, expected number\n");
                return 2;
            }
        }

        NSData *data = [NSData dataWithContentsOfFile:sessionPath];
        if (data == nil) {
            fprintf(stderr, "native_gui_host_macos: failed to read session: %s\n", [sessionPath UTF8String]);
            return 2;
        }

        NSError *jsonError = nil;
        NSDictionary *sessionDoc = [NSJSONSerialization JSONObjectWithData:data options:0 error:&jsonError];
        if (![sessionDoc isKindOfClass:[NSDictionary class]]) {
            fprintf(stderr, "native_gui_host_macos: invalid session json\n");
            return 2;
        }
        NSString *format = R2CString(sessionDoc[@"format"]);
        if (![format isEqualToString:@"native_gui_session_v1"]) {
            fprintf(stderr, "native_gui_host_macos: unexpected format: %s\n", [format UTF8String]);
            return 2;
        }

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        R2CAppDelegate *delegate = [[R2CAppDelegate alloc] init];
        delegate.sessionDoc = sessionDoc;
        delegate.screenshotPath = screenshotPath ?: @"";
        delegate.renderPlanOutPath = renderPlanOutPath ?: @"";
        delegate.runtimeStateOutPath = runtimeStateOutPath ?: @"";
        delegate.autoCloseMs = [autoCloseText integerValue];
        delegate.waitAfterClickMs = [waitAfterClickText integerValue];
        delegate.hasRequestedClick = hasRequestedClick;
        delegate.requestedClickX = requestedClickX;
        delegate.requestedClickY = requestedClickY;
        delegate.hasRequestedResize = hasRequestedResize;
        delegate.requestedWidth = requestedWidth;
        delegate.requestedHeight = requestedHeight;
        delegate.hasRequestedScroll = hasRequestedScroll;
        delegate.requestedScrollY = requestedScrollY;
        delegate.requestedFocusItemId = focusItemId ?: @"";
        delegate.requestedText = requestedText ?: @"";
        delegate.requestedKey = requestedKey ?: @"";
        delegate.repoRoot = repoRoot ?: @"";
        delegate.openSourceOnClick = openSourceOnClick;
        [NSApp setDelegate:delegate];
        [NSApp run];
    }
    return 0;
}
