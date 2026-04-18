#import <Cocoa/Cocoa.h>

@interface R2CSessionView : NSView
@property(nonatomic, strong) NSDictionary *sessionDoc;
@property(nonatomic, strong) NSDictionary *theme;
@property(nonatomic, strong) NSDictionary *renderPlanDoc;
@property(nonatomic, strong) NSDictionary *runtimeStateDoc;
@property(nonatomic, strong) NSArray *renderCommands;
@property(nonatomic, strong) NSMutableDictionary *imageCache;
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

static NSString *R2CClipText(NSString *text, NSUInteger limit) {
    NSString *value = R2CTrimmedText(text);
    if ([value length] <= limit) return value;
    if (limit <= 1) return @"";
    return [[value substringToIndex:limit - 1] stringByAppendingString:@"…"];
}

static NSString *R2CExtractAngleTagLabel(NSString *text) {
    NSString *value = R2CTrimmedText(text);
    if (![value hasPrefix:@"<"]) return @"";
    NSRange close = [value rangeOfString:@">"];
    if (close.location == NSNotFound || close.location <= 1) return @"";
    NSString *tag = [value substringWithRange:NSMakeRange(1, close.location - 1)];
    NSRange space = [tag rangeOfString:@" "];
    if (space.location != NSNotFound) {
        tag = [tag substringToIndex:space.location];
    }
    if ([tag hasPrefix:@"/"]) tag = [tag substringFromIndex:1];
    return R2CTrimmedText(tag);
}

static NSString *R2CExtractNodeTrailingText(NSString *text) {
    NSString *value = R2CTrimmedText(text);
    NSRange close = [value rangeOfString:@">"];
    if (close.location == NSNotFound) return @"";
    return R2CTrimmedText([value substringFromIndex:close.location + 1]);
}

static BOOL R2COnlyBooleanTokens(NSString *text) {
    NSString *value = R2CTrimmedText(text);
    if ([value length] == 0) return NO;
    NSArray<NSString *> *tokens = [value componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    BOOL sawToken = NO;
    NSSet<NSString *> *allowed = [NSSet setWithArray:@[@"false", @"true", @"null", @"undefined"]];
    for (NSString *rawToken in tokens) {
        NSString *token = [[R2CTrimmedText(rawToken) lowercaseString] copy];
        if ([token length] == 0) continue;
        sawToken = YES;
        if (![allowed containsObject:token]) return NO;
    }
    return sawToken;
}

static BOOL R2CLooksLikeDisplayText(NSString *text) {
    NSString *value = R2CTrimmedText(text);
    if ([value length] == 0) return NO;
    if ([value isEqualToString:@"false"] || [value isEqualToString:@"true"]) return NO;
    if (R2COnlyBooleanTokens(value)) return NO;
    if ([value hasPrefix:@"."]) return NO;
    if ([value hasPrefix:@"@"]) return NO;
    if ([value hasPrefix:@"<"]) return NO;
    return YES;
}

static NSString *R2CCompactDebugDetailText(NSString *text) {
    NSString *value = R2CTrimmedText(text);
    if ([value length] == 0) return @"";
    NSArray<NSString *> *tokens = [value componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSMutableArray<NSString *> *kept = [NSMutableArray array];
    NSSet<NSString *> *drop = [NSSet setWithArray:@[@"component", @"element", @"event", @"inline", @"scroll", @"button"]];
    for (NSString *rawToken in tokens) {
        NSString *token = R2CTrimmedText(rawToken);
        if ([token length] == 0) continue;
        if ([drop containsObject:[token lowercaseString]]) continue;
        [kept addObject:token];
        if ([kept count] >= 4) break;
    }
    if ([kept count] == 0) return @"";
    return R2CClipText([kept componentsJoinedByString:@" · "], 42);
}

static CGFloat R2CDeterministicWidthFraction(NSString *seedText, NSInteger index) {
    NSString *seed = R2CString(seedText);
    unsigned long long hash = 1469598103934665603ULL;
    NSData *data = [seed dataUsingEncoding:NSUTF8StringEncoding];
    const unsigned char *bytes = (const unsigned char *)[data bytes];
    for (NSUInteger i = 0; i < [data length]; i += 1) {
        hash ^= (unsigned long long)bytes[i];
        hash *= 1099511628211ULL;
    }
    hash ^= (unsigned long long)(index + 1) * 97ULL;
    CGFloat frac = 0.52 + (CGFloat)(hash % 33ULL) / 100.0;
    return R2CClamp(frac, 0.52, 0.84);
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

- (BOOL)isHomeShellSurfaceForRouteState:(NSString *)routeState {
    NSString *route = [R2CTrimmedText(routeState) lowercaseString];
    if (![route hasPrefix:@"home_"]) return NO;
    if ([route isEqualToString:@"home_bazi_overlay_open"]) return NO;
    if ([route isEqualToString:@"home_ziwei_overlay_open"]) return NO;
    if ([route isEqualToString:@"home_ecom_overlay_open"]) return NO;
    return YES;
}

- (NSString *)homeActiveCategoryForRouteState:(NSString *)routeState {
    NSString *route = [R2CTrimmedText(routeState) lowercaseString];
    if ([route isEqualToString:@"home_app_channel"]) return @"app";
    return @"content";
}

- (NSString *)homeTruthContentTimestampLabel {
    NSDate *date = [NSDate dateWithTimeIntervalSinceNow:-(15.0 * 60.0)];
    NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
    formatter.locale = [[NSLocale alloc] initWithLocaleIdentifier:@"zh_CN"];
    formatter.dateFormat = @"M月d日 HH:mm";
    return [formatter stringFromDate:date] ?: @"";
}

- (NSString *)homeTruthContentPosterSVGText {
    return [
        @"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"960\" height=\"1280\" viewBox=\"0 0 960 1280\">"
         "<defs>"
         "<linearGradient id=\"bg\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">"
         "<stop offset=\"0%\" stop-color=\"#8B5CF6\"/>"
         "<stop offset=\"100%\" stop-color=\"#111827\"/>"
         "</linearGradient>"
         "</defs>"
         "<rect width=\"960\" height=\"1280\" fill=\"url(#bg)\"/>"
         "<circle cx=\"760\" cy=\"220\" r=\"180\" fill=\"rgba(255,255,255,0.12)\"/>"
         "<circle cx=\"240\" cy=\"980\" r=\"220\" fill=\"rgba(255,255,255,0.08)\"/>"
         "<text x=\"96\" y=\"1040\" fill=\"#ffffff\" font-family=\"Arial, sans-serif\" font-size=\"88\" font-weight=\"700\">"
         "UniMaker Content"
         "</text>"
         "</svg>" copy];
}

- (NSDictionary *)buildHomeShellRenderPlanDocForState:(NSDictionary *)stateDoc {
    NSMutableArray<NSDictionary *> *commands = [NSMutableArray array];
    NSString *routeState = R2CString(stateDoc[@"route_state"]);
    CGFloat windowWidth = R2CCGFloat(stateDoc[@"window_width"], self.sessionWidth > 0.0 ? self.sessionWidth : 390.0);
    CGFloat windowHeight = R2CCGFloat(stateDoc[@"window_height"], self.sessionHeight > 0.0 ? self.sessionHeight : 844.0);
    CGFloat headerHeight = 56.0;
    CGFloat tabsHeight = 46.0;
    BOOL showSearch = [routeState isEqualToString:@"home_search_open"];
    BOOL showSort = [routeState isEqualToString:@"home_sort_open"];
    BOOL showChannelManager = [routeState isEqualToString:@"home_channel_manager_open"];
    BOOL showContentDetail = [routeState isEqualToString:@"home_content_detail_open"];
    BOOL isAppChannel = [[self homeActiveCategoryForRouteState:routeState] isEqualToString:@"app"];
    CGFloat searchHeight = showSearch ? 56.0 : 0.0;
    CGFloat sortHeight = showSort ? 46.0 : 0.0;
    CGFloat navHeight = 68.0;
    CGFloat contentTop = headerHeight + tabsHeight + searchHeight + sortHeight;
    CGFloat contentBottom = MAX(contentTop, windowHeight - navHeight);
    CGFloat contentHeight = MAX(0.0, contentBottom - contentTop);
    CGFloat (^surfaceY)(CGFloat, CGFloat) = ^CGFloat(CGFloat top, CGFloat height) {
        (void)height;
        return MAX(0.0, top);
    };

    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @0,
        @"width": @(windowWidth),
        @"height": @(windowHeight),
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @0,
    }];

    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(surfaceY(0.0, headerHeight)),
        @"width": @(windowWidth),
        @"height": @(headerHeight),
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @1,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(surfaceY(headerHeight - 1.0, 1.0)),
        @"width": @(windowWidth),
        @"height": @1,
        @"corner_radius": @0,
        @"background_color": @"#e5e7eb",
        @"z_index": @2,
    }];

    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"menu",
        @"x": @16,
        @"y": @(surfaceY(17.0, 22.0)),
        @"width": @22,
        @"height": @22,
        @"color": @"#111827",
        @"stroke_width": @2.0,
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"search",
        @"x": @(windowWidth - 72.0),
        @"y": @(surfaceY(17.0, 22.0)),
        @"width": @22,
        @"height": @22,
        @"color": @"#111827",
        @"stroke_width": @2.0,
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"sliders-horizontal",
        @"x": @(windowWidth - 38.0),
        @"y": @(surfaceY(17.0, 22.0)),
        @"width": @22,
        @"height": @22,
        @"color": @"#111827",
        @"stroke_width": @2.0,
        @"z_index": @3,
    }];

    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(surfaceY(headerHeight, tabsHeight)),
        @"width": @(windowWidth),
        @"height": @(tabsHeight),
        @"corner_radius": @0,
        @"background_color": @"#f9fafb",
        @"z_index": @1,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(surfaceY(headerHeight + tabsHeight - 1.0, 1.0)),
        @"width": @(windowWidth),
        @"height": @1,
        @"corner_radius": @0,
        @"background_color": @"#e5e7eb",
        @"z_index": @2,
    }];

    NSArray<NSDictionary *> *tabSpecs = @[
        @{@"key": @"app", @"label": @"应用"},
        @{@"key": @"content", @"label": @"内容"},
        @{@"key": @"product", @"label": @"电商"},
        @{@"key": @"live", @"label": @"直播"},
        @{@"key": @"food", @"label": @"外卖"},
        @{@"key": @"ride", @"label": @"顺风车"},
    ];
    NSString *activeCategory = [self homeActiveCategoryForRouteState:routeState];
    CGFloat chipX = 12.0;
    CGFloat chipTop = headerHeight + 8.0;
    CGFloat chipHeight = 30.0;
    CGFloat chipReservedRight = 70.0;
    for (NSDictionary *tabSpec in tabSpecs) {
        NSString *key = R2CString(tabSpec[@"key"]);
        NSString *label = R2CString(tabSpec[@"label"]);
        BOOL showBadge = [key isEqualToString:@"app"] && !isAppChannel;
        CGFloat chipWidth = MAX(44.0, [self measuredTextWidth:label fontSize:13.0 weightText:@"medium"] + (showBadge ? 36.0 : 22.0));
        if (chipX + chipWidth > windowWidth - chipReservedRight) break;
        BOOL active = [key isEqualToString:activeCategory];
        [commands addObject:@{
            @"type": @"rounded_rect",
            @"x": @(chipX),
            @"y": @(surfaceY(chipTop, chipHeight)),
            @"width": @(chipWidth),
            @"height": @(chipHeight),
            @"corner_radius": @15,
            @"background_color": active ? @"#a855f7" : @"#ffffff",
            @"border_color": active ? @"#a855f7" : @"#e5e7eb",
            @"line_width": @1.0,
            @"z_index": @3,
        }];
        [commands addObject:@{
            @"type": @"text_label",
            @"text": label,
            @"x": @(chipX),
            @"y": @(surfaceY(chipTop, chipHeight)),
            @"width": @(chipWidth),
            @"height": @(chipHeight),
            @"font_size": @13,
            @"font_weight": @"medium",
            @"text_color": active ? @"#ffffff" : @"#6b7280",
            @"align": @"center",
            @"z_index": @4,
        }];
        if (showBadge) {
            [commands addObject:@{
                @"type": @"rounded_rect",
                @"x": @(chipX + chipWidth - 22.0),
                @"y": @(surfaceY(chipTop + 4.0, 18.0)),
                @"width": @18,
                @"height": @18,
                @"corner_radius": @9,
                @"background_color": @"#ef4444",
                @"z_index": @5,
            }];
            [commands addObject:@{
                @"type": @"text_label",
                @"text": @"7",
                @"x": @(chipX + chipWidth - 22.0),
                @"y": @(surfaceY(chipTop + 4.0, 18.0)),
                @"width": @18,
                @"height": @18,
                @"font_size": @10,
                @"font_weight": @"bold",
                @"text_color": @"#ffffff",
                @"align": @"center",
                @"z_index": @6,
            }];
        }
        chipX += chipWidth + 8.0;
    }

    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"↻",
        @"x": @(windowWidth - 62.0),
        @"y": @(surfaceY(chipTop + 1.0, 28.0)),
        @"width": @24,
        @"height": @28,
        @"font_size": @16,
        @"font_weight": @"medium",
        @"text_color": @"#9ca3af",
        @"align": @"center",
        @"z_index": @4,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"⚙",
        @"x": @(windowWidth - 34.0),
        @"y": @(surfaceY(chipTop + 1.0, 28.0)),
        @"width": @24,
        @"height": @28,
        @"font_size": @14,
        @"font_weight": @"regular",
        @"text_color": @"#9ca3af",
        @"align": @"center",
        @"z_index": @4,
    }];

    CGFloat cursorTop = headerHeight + tabsHeight;
    if (showSearch) {
        [commands addObject:@{
            @"type": @"rounded_rect",
            @"x": @0,
            @"y": @(surfaceY(cursorTop, searchHeight)),
            @"width": @(windowWidth),
            @"height": @(searchHeight),
            @"corner_radius": @0,
            @"background_color": @"#f9fafb",
            @"z_index": @1,
        }];
        [commands addObject:@{
            @"type": @"rounded_rect",
            @"x": @16,
            @"y": @(surfaceY(cursorTop + 10.0, 36.0)),
            @"width": @(windowWidth - 32.0),
            @"height": @36,
            @"corner_radius": @18,
            @"background_color": @"#ffffff",
            @"border_color": @"#d1d5db",
            @"line_width": @1.0,
            @"z_index": @3,
        }];
        [commands addObject:@{
            @"type": @"text_label",
            @"text": @"搜索内容...",
            @"x": @30,
            @"y": @(surfaceY(cursorTop + 10.0, 36.0)),
            @"width": @(windowWidth - 60.0),
            @"height": @36,
            @"font_size": @14,
            @"font_weight": @"regular",
            @"text_color": @"#9ca3af",
            @"align": @"left",
            @"z_index": @4,
        }];
        cursorTop += searchHeight;
    }

    if (showSort) {
        [commands addObject:@{
            @"type": @"rounded_rect",
            @"x": @0,
            @"y": @(surfaceY(cursorTop, sortHeight)),
            @"width": @(windowWidth),
            @"height": @(sortHeight),
            @"corner_radius": @0,
            @"background_color": @"#f9fafb",
            @"z_index": @1,
        }];
        NSArray<NSDictionary *> *sortSpecs = @[
            @{@"label": @"最热", @"active": @YES},
            @{@"label": @"最新", @"active": @NO},
            @{@"label": @"距离最近", @"active": @NO},
        ];
        CGFloat sortX = 16.0;
        for (NSDictionary *sortSpec in sortSpecs) {
            NSString *label = R2CString(sortSpec[@"label"]);
            BOOL active = R2CBool(sortSpec[@"active"], NO);
            CGFloat buttonWidth = [self measuredTextWidth:label fontSize:13.0 weightText:@"medium"] + 28.0;
            [commands addObject:@{
                @"type": @"rounded_rect",
                @"x": @(sortX),
                @"y": @(surfaceY(cursorTop + 6.0, 32.0)),
                @"width": @(buttonWidth),
                @"height": @32,
                @"corner_radius": @16,
                @"background_color": active ? @"#a855f7" : @"#ffffff",
                @"border_color": active ? @"#a855f7" : @"#d1d5db",
                @"line_width": @1.0,
                @"z_index": @3,
            }];
            [commands addObject:@{
                @"type": @"text_label",
                @"text": label,
                @"x": @(sortX),
                @"y": @(surfaceY(cursorTop + 6.0, 32.0)),
                @"width": @(buttonWidth),
                @"height": @32,
                @"font_size": @13,
                @"font_weight": @"medium",
                @"text_color": active ? @"#ffffff" : @"#374151",
                @"align": @"center",
                @"z_index": @4,
            }];
            sortX += buttonWidth + 8.0;
        }
        cursorTop += sortHeight;
    }

    if (isAppChannel) {
        NSArray<NSDictionary *> *appCards = @[
            @{@"title": @"应用市场", @"detail": @"发现、安装与管理原生应用", @"accent": @"#3b82f6"},
            @{@"title": @"交易", @"detail": @"查看行情、买卖深度与钱包联动", @"accent": @"#059669"},
        ];
        CGFloat cardTop = cursorTop + 12.0;
        CGFloat cardWidth = windowWidth - 24.0;
        for (NSDictionary *card in appCards) {
            [commands addObject:@{
                @"type": @"rounded_rect",
                @"x": @12,
                @"y": @(surfaceY(cardTop, 82.0)),
                @"width": @(cardWidth),
                @"height": @82,
                @"corner_radius": @18,
                @"background_color": @"#ffffff",
                @"border_color": @"#edeef2",
                @"line_width": @1.0,
                @"z_index": @3,
            }];
            [commands addObject:@{
                @"type": @"rounded_rect",
                @"x": @24,
                @"y": @(surfaceY(cardTop + 12.0, 58.0)),
                @"width": @58,
                @"height": @58,
                @"corner_radius": @14,
                @"background_color": @"#f5f5f8",
                @"z_index": @4,
            }];
            [commands addObject:@{
                @"type": @"rounded_rect",
                @"x": @42,
                @"y": @(surfaceY(cardTop + 29.0, 24.0)),
                @"width": @24,
                @"height": @24,
                @"corner_radius": @12,
                @"background_color": R2CString(card[@"accent"]),
                @"z_index": @5,
            }];
            [commands addObject:@{
                @"type": @"text_label",
                @"text": R2CString(card[@"title"]),
                @"x": @96,
                @"y": @(surfaceY(cardTop + 16.0, 22.0)),
                @"width": @(cardWidth - 112.0),
                @"height": @22,
                @"font_size": @17,
                @"font_weight": @"semibold",
                @"text_color": @"#111827",
                @"align": @"left",
                @"z_index": @5,
            }];
            [commands addObject:@{
                @"type": @"text_label",
                @"text": R2CString(card[@"detail"]),
                @"x": @96,
                @"y": @(surfaceY(cardTop + 42.0, 18.0)),
                @"width": @(cardWidth - 112.0),
                @"height": @18,
                @"font_size": @13,
                @"font_weight": @"regular",
                @"text_color": @"#9aa1b2",
                @"align": @"left",
                @"z_index": @5,
            }];
            cardTop += 94.0;
        }
    } else if (showContentDetail) {
        [commands addObject:@{
            @"type": @"rounded_rect",
            @"x": @12,
            @"y": @(surfaceY(cursorTop + 16.0, 132.0)),
            @"width": @(windowWidth - 24.0),
            @"height": @132,
            @"corner_radius": @20,
            @"background_color": @"#ffffff",
            @"border_color": @"#eceff3",
            @"line_width": @1.0,
            @"z_index": @3,
        }];
        [commands addObject:@{
            @"type": @"text_label",
            @"text": @"内容详情",
            @"x": @28,
            @"y": @(surfaceY(cursorTop + 34.0, 24.0)),
            @"width": @(windowWidth - 56.0),
            @"height": @24,
            @"font_size": @20,
            @"font_weight": @"semibold",
            @"text_color": @"#111827",
            @"align": @"left",
            @"z_index": @4,
        }];
        [commands addObject:@{
            @"type": @"text_label",
            @"text": @"当前原生壳已切到内容页入口",
            @"x": @28,
            @"y": @(surfaceY(cursorTop + 66.0, 20.0)),
            @"width": @(windowWidth - 56.0),
            @"height": @20,
            @"font_size": @14,
            @"font_weight": @"regular",
            @"text_color": @"#9ca3af",
            @"align": @"left",
            @"z_index": @4,
        }];
    } else {
        NSString *emptyLabel = isAppChannel ? @"暂无应用" : @"内容";
        [commands addObject:@{
            @"type": @"text_label",
            @"text": emptyLabel,
            @"x": @0,
            @"y": @(surfaceY(cursorTop + MIN(84.0, MAX(36.0, contentHeight * 0.20)), 24.0)),
            @"width": @(windowWidth),
            @"height": @24,
            @"font_size": @15,
            @"font_weight": @"regular",
            @"text_color": @"#9ca3af",
            @"align": @"center",
            @"z_index": @3,
        }];
    }

    CGFloat navTop = windowHeight - navHeight;
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(surfaceY(navTop, navHeight)),
        @"width": @(windowWidth),
        @"height": @(navHeight),
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @5,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(surfaceY(navTop, 1.0)),
        @"width": @(windowWidth),
        @"height": @1,
        @"corner_radius": @0,
        @"background_color": @"#e5e7eb",
        @"z_index": @6,
    }];

    CGFloat columnWidth = floor(windowWidth / 5.0);
    NSArray<NSDictionary *> *navSpecs = @[
        @{@"label": @"首页", @"index": @0, @"active": @YES},
        @{@"label": @"消息", @"index": @1, @"active": @NO},
        @{@"label": @"节点", @"index": @3, @"active": @NO},
        @{@"label": @"我", @"index": @4, @"active": @NO},
    ];
    for (NSDictionary *navSpec in navSpecs) {
        NSInteger index = R2CInteger(navSpec[@"index"], 0);
        CGFloat navX = columnWidth * index;
        BOOL active = R2CBool(navSpec[@"active"], NO);
        [commands addObject:@{
            @"type": @"text_label",
            @"text": R2CString(navSpec[@"label"]),
            @"x": @(navX),
            @"y": @(surfaceY(navTop + 11.0, 42.0)),
            @"width": @(columnWidth),
            @"height": @42,
            @"font_size": @18,
            @"font_weight": @"regular",
            @"text_color": active ? @"#a855f7" : @"#4b5563",
            @"align": @"center",
            @"z_index": @7,
        }];
    }

    CGFloat plusColumnX = columnWidth * 2;
    CGFloat plusCircleX = plusColumnX + floor((columnWidth - 40.0) * 0.5);
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @(plusCircleX),
        @"y": @(surfaceY(navTop + 6.0, 40.0)),
        @"width": @40,
        @"height": @40,
        @"corner_radius": @20,
        @"background_color": @"#a855f7",
        @"z_index": @7,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"plus",
        @"x": @(plusCircleX + 9.0),
        @"y": @(surfaceY(navTop + 15.0, 22.0)),
        @"width": @22,
        @"height": @22,
        @"color": @"#ffffff",
        @"stroke_width": @3.0,
        @"z_index": @8,
    }];

    if (showChannelManager) {
        CGFloat overlayHeight = windowHeight;
        CGFloat sheetHeight = MIN(MAX(420.0, windowHeight * 0.70), windowHeight - 24.0);
        CGFloat sheetTop = windowHeight - sheetHeight;
        [commands addObject:@{
            @"type": @"rounded_rect",
            @"x": @0,
            @"y": @0,
            @"width": @(windowWidth),
            @"height": @(overlayHeight),
            @"corner_radius": @0,
            @"background_color": @"#00000080",
            @"z_index": @20,
        }];
        [commands addObject:@{
            @"type": @"rounded_rect",
            @"x": @0,
            @"y": @(surfaceY(sheetTop, sheetHeight)),
            @"width": @(windowWidth),
            @"height": @(sheetHeight),
            @"corner_radius": @22,
            @"background_color": @"#ffffff",
            @"z_index": @21,
        }];
        [commands addObject:@{
            @"type": @"rounded_rect",
            @"x": @0,
            @"y": @(surfaceY(sheetTop + 54.0, 1.0)),
            @"width": @(windowWidth),
            @"height": @1,
            @"corner_radius": @0,
            @"background_color": @"#f3f4f6",
            @"z_index": @22,
        }];
        [commands addObject:@{
            @"type": @"text_label",
            @"text": @"频道管理",
            @"x": @16,
            @"y": @(surfaceY(sheetTop + 14.0, 24.0)),
            @"width": @(windowWidth - 64.0),
            @"height": @24,
            @"font_size": @18,
            @"font_weight": @"bold",
            @"text_color": @"#1f2937",
            @"align": @"left",
            @"z_index": @23,
        }];
        [commands addObject:@{
            @"type": @"text_label",
            @"text": @"×",
            @"x": @(windowWidth - 38.0),
            @"y": @(surfaceY(sheetTop + 14.0, 24.0)),
            @"width": @22,
            @"height": @24,
            @"font_size": @20,
            @"font_weight": @"regular",
            @"text_color": @"#9ca3af",
            @"align": @"center",
            @"z_index": @23,
        }];
        [commands addObject:@{
            @"type": @"text_label",
            @"text": @"长按拖动排序，点击进入频道",
            @"x": @18,
            @"y": @(surfaceY(sheetTop + 68.0, 18.0)),
            @"width": @(windowWidth - 36.0),
            @"height": @18,
            @"font_size": @12,
            @"font_weight": @"regular",
            @"text_color": @"#9ca3af",
            @"align": @"left",
            @"z_index": @23,
        }];
        NSArray<NSDictionary *> *managerTabs = @[
            @{@"key": @"content", @"label": @"内容"},
            @{@"key": @"product", @"label": @"电商"},
            @{@"key": @"live", @"label": @"直播"},
            @{@"key": @"app", @"label": @"应用"},
            @{@"key": @"food", @"label": @"外卖"},
            @{@"key": @"ride", @"label": @"顺风车"},
            @{@"key": @"job", @"label": @"求职"},
            @{@"key": @"hire", @"label": @"招聘"},
            @{@"key": @"rent", @"label": @"出租"},
            @{@"key": @"sell", @"label": @"出售"},
            @{@"key": @"secondhand", @"label": @"二手"},
            @{@"key": @"crowdfunding", @"label": @"众筹"},
            @{@"key": @"ad", @"label": @"广告"},
        ];
        CGFloat gridGap = 12.0;
        CGFloat cellWidth = floor((windowWidth - 16.0 * 2.0 - gridGap * 3.0) / 4.0);
        CGFloat cellHeight = 74.0;
        CGFloat gridTop = sheetTop + 96.0;
        for (NSUInteger index = 0; index < [managerTabs count]; index += 1) {
            NSDictionary *tabSpec = managerTabs[index];
            NSUInteger row = index / 4;
            NSUInteger column = index % 4;
            CGFloat cellX = 16.0 + (cellWidth + gridGap) * column;
            CGFloat cellTop = gridTop + (cellHeight + gridGap) * row;
            BOOL active = [R2CString(tabSpec[@"key"]) isEqualToString:activeCategory];
            [commands addObject:@{
                @"type": @"rounded_rect",
                @"x": @(cellX),
                @"y": @(surfaceY(cellTop, cellHeight)),
                @"width": @(cellWidth),
                @"height": @(cellHeight),
                @"corner_radius": @14,
                @"background_color": active ? @"#faf5ff" : @"#f9fafb",
                @"border_color": active ? @"#e9d5ff" : @"#f3f4f6",
                @"line_width": @1.0,
                @"z_index": @22,
            }];
            if (active) {
                [commands addObject:@{
                    @"type": @"rounded_rect",
                    @"x": @(cellX + cellWidth - 12.0),
                    @"y": @(surfaceY(cellTop + 8.0, 6.0)),
                    @"width": @6,
                    @"height": @6,
                    @"corner_radius": @3,
                    @"background_color": @"#a855f7",
                    @"z_index": @23,
                }];
            }
            [commands addObject:@{
                @"type": @"text_label",
                @"text": R2CString(tabSpec[@"label"]),
                @"x": @(cellX + 6.0),
                @"y": @(surfaceY(cellTop + 20.0, 20.0)),
                @"width": @(cellWidth - 12.0),
                @"height": @20,
                @"font_size": @12,
                @"font_weight": @"medium",
                @"text_color": active ? @"#9333ea" : @"#4b5563",
                @"align": @"center",
                @"z_index": @23,
            }];
            [commands addObject:@{
                @"type": @"text_label",
                @"text": @"⋮",
                @"x": @(cellX + (cellWidth - 12.0) * 0.5),
                @"y": @(surfaceY(cellTop + 44.0, 14.0)),
                @"width": @12,
                @"height": @14,
                @"font_size": @12,
                @"font_weight": @"regular",
                @"text_color": @"#d1d5db",
                @"align": @"center",
                @"z_index": @23,
            }];
        }
    }

    return @{
        @"format": @"native_render_plan_v1",
        @"ready": @YES,
        @"window_title": R2CString(stateDoc[@"window_title"]),
        @"route_state": routeState ?: @"",
        @"entry_module": R2CString(stateDoc[@"entry_module"]),
        @"window_width": @(windowWidth),
        @"window_height": @(windowHeight),
        @"content_height": @(MAX(windowHeight, contentBottom)),
        @"scroll_height": @(MAX(0.0, windowHeight - contentBottom)),
        @"scroll_offset_y": @(0),
        @"visible_layout_item_count": @0,
        @"selected_item_id": @"",
        @"focused_item_id": @"",
        @"typed_text": @"",
        @"commands": commands,
        @"command_count": @((NSInteger)[commands count]),
    };
}

- (NSDictionary *)buildHomeContentDetailRenderPlanDocForState:(NSDictionary *)stateDoc {
    NSMutableArray<NSDictionary *> *commands = [NSMutableArray array];
    CGFloat windowWidth = R2CCGFloat(stateDoc[@"window_width"], self.sessionWidth > 0.0 ? self.sessionWidth : 390.0);
    CGFloat windowHeight = R2CCGFloat(stateDoc[@"window_height"], self.sessionHeight > 0.0 ? self.sessionHeight : 844.0);
    CGFloat headerHeight = 56.0;
    CGFloat heroHeight = floor(MIN(windowWidth * (1280.0 / 960.0), windowHeight * 0.60));
    CGFloat detailTop = headerHeight + heroHeight;
    CGFloat bottomBarHeight = 56.0;
    NSString *timestampLabel = [self homeTruthContentTimestampLabel];

    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @0,
        @"width": @(windowWidth),
        @"height": @(windowHeight),
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @0,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @0,
        @"width": @(windowWidth),
        @"height": @(headerHeight),
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @1,
    }];
    [commands addObject:@{
        @"type": @"svg_image",
        @"x": @0,
        @"y": @(headerHeight),
        @"width": @(windowWidth),
        @"height": @(heroHeight),
        @"svg_text": [self homeTruthContentPosterSVGText],
        @"content_mode": @"cover",
        @"z_index": @1,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(detailTop),
        @"width": @(windowWidth),
        @"height": @(MAX(0.0, windowHeight - detailTop)),
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @1,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"arrow-left",
        @"x": @16,
        @"y": @17,
        @"width": @22,
        @"height": @22,
        @"color": @"#111827",
        @"stroke_width": @2.0,
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"x": @52,
        @"y": @18,
        @"width": @(windowWidth - 140.0),
        @"height": @20,
        @"text": @"UniMaker 节点",
        @"font_size": @14,
        @"font_weight": @"medium",
        @"text_color": @"#111827",
        @"align": @"left",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @(windowWidth - 78.0),
        @"y": @16,
        @"width": @62,
        @"height": @24,
        @"corner_radius": @12,
        @"background_color": @"#ef4444",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"user-plus",
        @"x": @(windowWidth - 70.0),
        @"y": @22,
        @"width": @12,
        @"height": @12,
        @"color": @"#ffffff",
        @"stroke_width": @1.7,
        @"z_index": @4,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"关注",
        @"x": @(windowWidth - 55.0),
        @"y": @17,
        @"width": @34,
        @"height": @24,
        @"font_size": @12,
        @"font_weight": @"medium",
        @"text_color": @"#ffffff",
        @"align": @"left",
        @"z_index": @4,
    }];

    [commands addObject:@{
        @"type": @"text_label",
        @"x": @16,
        @"y": @(detailTop + 16.0),
        @"width": @(windowWidth - 32.0),
        @"height": @50,
        @"text": @"通过 cheng-libp2p 发布的示例内容，用于原生\nAndroid 与 React 真值截图对齐。",
        @"font_size": @15,
        @"font_weight": @"regular",
        @"text_color": @"#111827",
        @"align": @"left",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"x": @16,
        @"y": @(detailTop + 74.0),
        @"width": @(windowWidth - 32.0),
        @"height": @16,
        @"text": timestampLabel,
        @"font_size": @12,
        @"font_weight": @"regular",
        @"text_color": @"#9ca3af",
        @"align": @"left",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"x": @16,
        @"y": @(detailTop + 105.0),
        @"width": @(windowWidth - 32.0),
        @"height": @16,
        @"text": @"128 点赞    32 评论",
        @"font_size": @12,
        @"font_weight": @"regular",
        @"text_color": @"#6b7280",
        @"align": @"left",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(detailTop + 128.0),
        @"width": @(windowWidth),
        @"height": @8,
        @"corner_radius": @0,
        @"background_color": @"#f9fafb",
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"x": @16,
        @"y": @(detailTop + 156.0),
        @"width": @(windowWidth - 32.0),
        @"height": @20,
        @"text": @"评论区",
        @"font_size": @14,
        @"font_weight": @"medium",
        @"text_color": @"#1f2937",
        @"align": @"left",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(windowHeight - bottomBarHeight),
        @"width": @(windowWidth),
        @"height": @(bottomBarHeight),
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @5,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(windowHeight - bottomBarHeight),
        @"width": @(windowWidth),
        @"height": @1,
        @"corner_radius": @0,
        @"background_color": @"#f3f4f6",
        @"z_index": @6,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @14,
        @"y": @(windowHeight - 44.0),
        @"width": @(windowWidth - 136.0),
        @"height": @34,
        @"corner_radius": @17,
        @"background_color": @"#f3f4f6",
        @"z_index": @6,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"说点什么...",
        @"x": @30,
        @"y": @(windowHeight - 44.0),
        @"width": @(windowWidth - 168.0),
        @"height": @34,
        @"font_size": @14,
        @"font_weight": @"regular",
        @"text_color": @"#9ca3af",
        @"align": @"left",
        @"z_index": @7,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"heart",
        @"x": @(windowWidth - 82.0),
        @"y": @(windowHeight - 42.0),
        @"width": @22,
        @"height": @22,
        @"color": @"#6b7280",
        @"stroke_width": @1.8,
        @"z_index": @7,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"128",
        @"x": @(windowWidth - 86.0),
        @"y": @(windowHeight - 18.0),
        @"width": @30,
        @"height": @12,
        @"font_size": @10,
        @"font_weight": @"regular",
        @"text_color": @"#6b7280",
        @"align": @"center",
        @"z_index": @7,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"flag",
        @"x": @(windowWidth - 42.0),
        @"y": @(windowHeight - 42.0),
        @"width": @22,
        @"height": @22,
        @"color": @"#6b7280",
        @"stroke_width": @1.8,
        @"z_index": @7,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"举报",
        @"x": @(windowWidth - 48.0),
        @"y": @(windowHeight - 18.0),
        @"width": @34,
        @"height": @12,
        @"font_size": @10,
        @"font_weight": @"regular",
        @"text_color": @"#6b7280",
        @"align": @"center",
        @"z_index": @7,
    }];

    return @{
        @"format": @"native_render_plan_v1",
        @"ready": @YES,
        @"window_title": R2CString(stateDoc[@"window_title"]),
        @"route_state": R2CString(stateDoc[@"route_state"]),
        @"entry_module": R2CString(stateDoc[@"entry_module"]),
        @"window_width": @(windowWidth),
        @"window_height": @(windowHeight),
        @"content_height": @(windowHeight),
        @"scroll_height": @(0),
        @"scroll_offset_y": @(0),
        @"visible_layout_item_count": @0,
        @"selected_item_id": @"",
        @"focused_item_id": @"",
        @"typed_text": @"",
        @"commands": commands,
        @"command_count": @((NSInteger)[commands count]),
    };
}

- (BOOL)isGenericPreviewChromeItem:(NSDictionary *)item {
    NSString *itemId = R2CString(item[@"id"]);
    return [itemId isEqualToString:@"root"]
        || [itemId isEqualToString:@"header"]
        || [itemId isEqualToString:@"meta_component"]
        || [itemId isEqualToString:@"meta_surface"];
}

- (NSString *)genericSurfaceTitleForLayoutItem:(NSDictionary *)item {
    NSString *text = R2CString(item[@"text"]);
    NSString *trailing = R2CExtractNodeTrailingText(text);
    if (R2CLooksLikeDisplayText(trailing)) return R2CClipText(trailing, 32);
    if (R2CLooksLikeDisplayText(text)) return R2CClipText(text, 32);
    NSString *tag = R2CExtractAngleTagLabel(text);
    if ([tag length] > 0) return R2CClipText(tag, 32);
    NSString *detail = R2CCompactDebugDetailText(item[@"detail_text"]);
    if ([detail length] > 0) return R2CClipText(detail, 32);
    return R2CClipText(R2CString(item[@"kind"]), 32);
}

- (NSString *)genericSurfaceSubtitleForLayoutItem:(NSDictionary *)item title:(NSString *)title {
    NSString *detail = R2CCompactDebugDetailText(item[@"detail_text"]);
    NSString *visualRole = R2CTrimmedText(item[@"visual_role"]);
    NSString *tag = R2CExtractAngleTagLabel(item[@"text"]);
    NSMutableArray<NSString *> *parts = [NSMutableArray array];
    if ([detail length] > 0) [parts addObject:detail];
    if ([parts count] == 0 && [visualRole length] > 0 && ![[visualRole lowercaseString] isEqualToString:@"inline"]) {
        [parts addObject:visualRole];
    }
    if ([parts count] == 0
        && [tag length] > 0
        && ![[[tag lowercaseString] copy] isEqualToString:[[R2CString(title) lowercaseString] copy]]) {
        [parts addObject:tag];
    }
    if ([parts count] == 0) return @"";
    return R2CClipText([parts componentsJoinedByString:@" · "], 48);
}

- (BOOL)shouldSkipGenericSurfaceItem:(NSDictionary *)item {
    if ([self isGenericPreviewChromeItem:item]) return YES;
    NSString *itemId = R2CString(item[@"id"]);
    if ([itemId isEqualToString:@"node_mount_root"]) return YES;
    if (R2CBool(item[@"interactive"], NO)) return NO;
    NSString *kind = [[R2CString(item[@"kind"]) lowercaseString] copy];
    NSString *visualRole = [[R2CString(item[@"visual_role"]) lowercaseString] copy];
    NSString *title = [self genericSurfaceTitleForLayoutItem:item];
    NSString *subtitle = [self genericSurfaceSubtitleForLayoutItem:item title:title];
    NSString *tag = [[R2CExtractAngleTagLabel(item[@"text"]) lowercaseString] copy];
    NSSet<NSString *> *wrapperTags = [NSSet setWithArray:@[@"div", @"span", @"main", @"section", @"article", @"header", @"footer"]];
    NSSet<NSString *> *primitiveTags = [NSSet setWithArray:@[@"svg", @"line", @"path", @"circle", @"rect", @"g", @"defs", @"stop", @"polygon", @"polyline", @"use"]];
    if ([kind isEqualToString:@"tree_fragment"]) return YES;
    if ([primitiveTags containsObject:tag]) return YES;
    if ([tag isEqualToString:@"button"]
        && [[[R2CString(title) lowercaseString] copy] isEqualToString:@"button"]
        && [subtitle length] == 0) {
        return YES;
    }
    if ([visualRole isEqualToString:@"inline"] && [title length] == 0 && [subtitle length] == 0) return YES;
    if ([wrapperTags containsObject:tag]
        && [subtitle length] == 0) {
        return YES;
    }
    return NO;
}

- (CGFloat)genericSurfaceTopTrim {
    CGFloat firstContentY = CGFLOAT_MAX;
    for (id rawItem in [self nativeLayoutItems]) {
        if (![rawItem isKindOfClass:[NSDictionary class]]) continue;
        NSDictionary *item = (NSDictionary *)rawItem;
        if ([self isGenericPreviewChromeItem:item]) continue;
        if ([self shouldSkipGenericSurfaceItem:item]) continue;
        CGFloat itemY = R2CCGFloat(item[@"y"], 0.0);
        if (itemY < firstContentY) firstContentY = itemY;
    }
    if (firstContentY == CGFLOAT_MAX) return 0.0;
    CGFloat desiredTop = 74.0;
    if (firstContentY <= desiredTop) return 0.0;
    return firstContentY - desiredTop;
}

- (BOOL)isGenericPlaceholderTitle:(NSString *)title {
    NSString *trimmed = [[R2CTrimmedText(title) lowercaseString] copy];
    if ([trimmed length] == 0) return YES;
    NSSet<NSString *> *placeholders = [NSSet setWithArray:@[
        @"div", @"span", @"section", @"article", @"header", @"footer", @"main",
        @"button", @"input", @"img", @"audio", @"svg", @"path", @"circle",
        @"line", @"polygon", @"polyline", @"rect", @"g", @"defs", @"stop",
        @"map()", @"&&", @"<>"
    ]];
    return [placeholders containsObject:trimmed];
}

- (NSString *)meaningfulTitleForLayoutItem:(NSDictionary *)item {
    NSString *title = [self genericSurfaceTitleForLayoutItem:item];
    if ([self isGenericPlaceholderTitle:title]) return @"";
    return title;
}

- (NSArray<NSString *> *)visibleMeaningfulControlTitlesForState:(NSDictionary *)stateDoc {
    NSMutableArray<NSString *> *titles = [NSMutableArray array];
    NSMutableSet<NSString *> *seen = [NSMutableSet set];
    for (id rawItem in [self nativeLayoutItems]) {
        if (![rawItem isKindOfClass:[NSDictionary class]]) continue;
        NSDictionary *item = (NSDictionary *)rawItem;
        if (![self itemVisibleForState:stateDoc item:item]) continue;
        if (!R2CBool(item[@"interactive"], NO)) continue;
        NSString *kind = [[R2CString(item[@"kind"]) lowercaseString] copy];
        NSString *visualRole = [[R2CString(item[@"visual_role"]) lowercaseString] copy];
        BOOL controlLike = [visualRole isEqualToString:@"control"]
            || [visualRole isEqualToString:@"overlay"]
            || [kind containsString:@"button"]
            || [kind containsString:@"input"];
        if (!controlLike) continue;
        NSString *title = [self meaningfulTitleForLayoutItem:item];
        if ([title length] == 0) continue;
        if ([seen containsObject:title]) continue;
        [seen addObject:title];
        [titles addObject:title];
    }
    return titles;
}

- (NSString *)firstVisibleMeaningfulTextForState:(NSDictionary *)stateDoc
                                 preferredNeedles:(NSArray<NSString *> *)preferredNeedles
                                         fallback:(NSString *)fallback {
    NSMutableArray<NSString *> *candidates = [NSMutableArray array];
    for (id rawItem in [self nativeLayoutItems]) {
        if (![rawItem isKindOfClass:[NSDictionary class]]) continue;
        NSDictionary *item = (NSDictionary *)rawItem;
        if (![self itemVisibleForState:stateDoc item:item]) continue;
        NSString *title = [self meaningfulTitleForLayoutItem:item];
        if ([title length] == 0) continue;
        [candidates addObject:title];
    }
    for (NSString *needle in preferredNeedles) {
        NSString *lowerNeedle = [[R2CString(needle) lowercaseString] copy];
        if ([lowerNeedle length] == 0) continue;
        for (NSString *candidate in candidates) {
            NSString *lowerCandidate = [[candidate lowercaseString] copy];
            if ([lowerCandidate containsString:lowerNeedle]) return candidate;
        }
    }
    if ([preferredNeedles count] == 0 && [candidates count] > 0) return candidates[0];
    return R2CString(fallback);
}

- (NSString *)chatPlaceholderSVGText {
    return @"<svg width=\"88\" height=\"88\" xmlns=\"http://www.w3.org/2000/svg\" stroke=\"#000\" stroke-linejoin=\"round\" opacity=\".3\" fill=\"none\" stroke-width=\"3.7\"><rect x=\"16\" y=\"16\" width=\"56\" height=\"56\" rx=\"6\"/><path d=\"m16 58 16-18 32 32\"/><circle cx=\"53\" cy=\"35\" r=\"7\"/></svg>";
}

- (NSString *)currentShortTimeLabel {
    NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
    formatter.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    formatter.timeZone = [NSTimeZone localTimeZone];
    formatter.dateFormat = @"HH:mm";
    return R2CString([formatter stringFromDate:[NSDate date]]);
}

- (void)appendPrimaryBottomNavCommandsForActiveLabel:(NSString *)activeLabel
                                         windowWidth:(CGFloat)windowWidth
                                        windowHeight:(CGFloat)windowHeight
                                            commands:(NSMutableArray<NSDictionary *> *)commands {
    CGFloat navHeight = 68.0;
    CGFloat navTop = windowHeight - navHeight;
    CGFloat columnWidth = floor(windowWidth / 5.0);
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(navTop),
        @"width": @(windowWidth),
        @"height": @(navHeight),
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @20,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(navTop),
        @"width": @(windowWidth),
        @"height": @1,
        @"corner_radius": @0,
        @"background_color": @"#e5e7eb",
        @"z_index": @21,
    }];
    NSArray<NSDictionary *> *navSpecs = @[
        @{@"label": @"首页", @"index": @0},
        @{@"label": @"消息", @"index": @1},
        @{@"label": @"节点", @"index": @3},
        @{@"label": @"我", @"index": @4},
    ];
    NSString *active = R2CString(activeLabel);
    for (NSDictionary *navSpec in navSpecs) {
        NSInteger index = R2CInteger(navSpec[@"index"], 0);
        NSString *label = R2CString(navSpec[@"label"]);
        CGFloat navX = columnWidth * index;
        BOOL isActive = [label isEqualToString:active];
        [commands addObject:@{
            @"type": @"text_label",
            @"text": label,
            @"x": @(navX),
            @"y": @(navTop + 10.0),
            @"width": @(columnWidth),
            @"height": @42,
            @"font_size": @18,
            @"font_weight": @"regular",
            @"text_color": isActive ? @"#a855f7" : @"#4b5563",
            @"align": @"center",
            @"z_index": @22,
        }];
    }
    CGFloat plusColumnX = columnWidth * 2.0;
    CGFloat plusCircleX = plusColumnX + floor((columnWidth - 40.0) * 0.5);
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @(plusCircleX),
        @"y": @(navTop + 6.0),
        @"width": @40,
        @"height": @40,
        @"corner_radius": @20,
        @"background_color": @"#a855f7",
        @"z_index": @23,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"plus",
        @"x": @(plusCircleX + 9.0),
        @"y": @(navTop + 15.0),
        @"width": @22,
        @"height": @22,
        @"color": @"#ffffff",
        @"stroke_width": @3.0,
        @"z_index": @24,
    }];
}

- (NSDictionary *)buildMessagesTabShellRenderPlanDocForState:(NSDictionary *)stateDoc {
    NSMutableArray<NSDictionary *> *commands = [NSMutableArray array];
    CGFloat windowWidth = R2CCGFloat(stateDoc[@"window_width"], self.sessionWidth);
    CGFloat windowHeight = R2CCGFloat(stateDoc[@"window_height"], self.sessionHeight);
    CGFloat contentHeight = R2CCGFloat(stateDoc[@"content_height"], self.contentHeight);
    CGFloat scrollHeight = R2CCGFloat(stateDoc[@"scroll_height"], [self maxScrollOffset]);
    CGFloat scrollOffsetY = R2CCGFloat(stateDoc[@"scroll_offset_y"], self.scrollOffsetY);
    NSString *selectedItemId = R2CString(stateDoc[@"selected_item_id"]);
    NSString *focusedItemId = R2CString(stateDoc[@"focused_item_id"]);
    NSString *typedText = R2CString(stateDoc[@"typed_text"]);

    NSString *pageTitle = [self firstVisibleMeaningfulTextForState:stateDoc
                                                   preferredNeedles:@[@"消息"]
                                                           fallback:@"消息"];
    NSArray<NSString *> *controlTitles = [self visibleMeaningfulControlTitlesForState:stateDoc];
    NSMutableArray<NSString *> *chipLabels = [NSMutableArray array];
    for (NSString *title in controlTitles) {
        if ([title isEqualToString:@"展开侧边栏"]) continue;
        if ([title isEqualToString:@"删除"]) continue;
        [chipLabels addObject:title];
        if ([chipLabels count] >= 4) break;
    }
    if ([chipLabels count] == 0) {
        [chipLabels addObjectsFromArray:@[@"会话", @"通讯录", @"朋友圈", @"通知"]];
    }

    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @0,
        @"width": @(windowWidth),
        @"height": @(windowHeight),
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @0,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @0,
        @"width": @(windowWidth),
        @"height": @98,
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @1,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"menu",
        @"x": @16,
        @"y": @14,
        @"width": @22,
        @"height": @22,
        @"color": @"#111827",
        @"stroke_width": @2.0,
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": pageTitle,
        @"x": @56,
        @"y": @12,
        @"width": @(windowWidth - 72.0),
        @"height": @24,
        @"font_size": @16,
        @"font_weight": @"semibold",
        @"text_color": @"#111827",
        @"align": @"left",
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @16,
        @"y": @56,
        @"width": @(windowWidth - 32.0),
        @"height": @34,
        @"corner_radius": @12,
        @"background_color": @"#f3f4f6",
        @"z_index": @2,
    }];

    CGFloat chipAreaX = 20.0;
    CGFloat chipAreaY = 60.0;
    CGFloat chipAreaWidth = windowWidth - 40.0;
    CGFloat chipWidth = floor(chipAreaWidth / MAX(1.0, (CGFloat)[chipLabels count]));
    for (NSUInteger index = 0; index < [chipLabels count]; index += 1) {
        BOOL active = index == 0;
        CGFloat x = chipAreaX + chipWidth * (CGFloat)index;
        CGFloat width = (index == [chipLabels count] - 1) ? (chipAreaX + chipAreaWidth - x) : chipWidth;
        if (active) {
            [commands addObject:@{
                @"type": @"rounded_rect",
                @"x": @(x),
                @"y": @(chipAreaY),
                @"width": @(width),
                @"height": @26,
                @"corner_radius": @10,
                @"background_color": @"#ffffff",
                @"z_index": @3,
            }];
        }
        [commands addObject:@{
            @"type": @"text_label",
            @"text": chipLabels[index],
            @"x": @(x),
            @"y": @(chipAreaY),
            @"width": @(width),
            @"height": @26,
            @"font_size": @12,
            @"font_weight": @"medium",
            @"text_color": active ? @"#a855f7" : @"#6b7280",
            @"align": @"center",
            @"z_index": @4,
        }];
    }
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @98,
        @"width": @(windowWidth),
        @"height": @1,
        @"corner_radius": @0,
        @"background_color": @"#e5e7eb",
        @"z_index": @2,
    }];

    CGFloat rowTop = 99.0;
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(rowTop),
        @"width": @(windowWidth),
        @"height": @64,
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @1,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"ASI 助手",
        @"x": @16,
        @"y": @(rowTop + 10.0),
        @"width": @(windowWidth - 96.0),
        @"height": @18,
        @"font_size": @14,
        @"font_weight": @"medium",
        @"text_color": @"#111827",
        @"align": @"left",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @78,
        @"y": @(rowTop + 10.0),
        @"width": @18,
        @"height": @18,
        @"corner_radius": @9,
        @"background_color": @"#ef4444",
        @"z_index": @4,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"1",
        @"x": @78,
        @"y": @(rowTop + 10.0),
        @"width": @18,
        @"height": @18,
        @"font_size": @10,
        @"font_weight": @"medium",
        @"text_color": @"#ffffff",
        @"align": @"center",
        @"z_index": @5,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"1分钟前",
        @"x": @(windowWidth - 82.0),
        @"y": @(rowTop + 10.0),
        @"width": @54,
        @"height": @18,
        @"font_size": @12,
        @"font_weight": @"regular",
        @"text_color": @"#6b7280",
        @"align": @"right",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"我是 ASI (Artificial Super Intelligence) 助手。我可以帮助您处理各种任务，请随时吩咐。",
        @"x": @16,
        @"y": @(rowTop + 30.0),
        @"width": @(windowWidth - 48.0),
        @"height": @18,
        @"font_size": @13,
        @"font_weight": @"regular",
        @"text_color": @"#6b7280",
        @"align": @"left",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @">",
        @"x": @(windowWidth - 24.0),
        @"y": @(rowTop + 18.0),
        @"width": @12,
        @"height": @20,
        @"font_size": @18,
        @"font_weight": @"regular",
        @"text_color": @"#9ca3af",
        @"align": @"center",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(rowTop + 63.0),
        @"width": @(windowWidth),
        @"height": @1,
        @"corner_radius": @0,
        @"background_color": @"#f3f4f6",
        @"z_index": @2,
    }];

    [self appendPrimaryBottomNavCommandsForActiveLabel:@"消息"
                                           windowWidth:windowWidth
                                          windowHeight:windowHeight
                                              commands:commands];

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

- (NSDictionary *)buildNodesTabShellRenderPlanDocForState:(NSDictionary *)stateDoc {
    NSMutableArray<NSDictionary *> *commands = [NSMutableArray array];
    CGFloat windowWidth = R2CCGFloat(stateDoc[@"window_width"], self.sessionWidth);
    CGFloat windowHeight = R2CCGFloat(stateDoc[@"window_height"], self.sessionHeight);
    CGFloat contentHeight = R2CCGFloat(stateDoc[@"content_height"], self.contentHeight);
    CGFloat scrollHeight = R2CCGFloat(stateDoc[@"scroll_height"], [self maxScrollOffset]);
    CGFloat scrollOffsetY = R2CCGFloat(stateDoc[@"scroll_offset_y"], self.scrollOffsetY);
    NSString *selectedItemId = R2CString(stateDoc[@"selected_item_id"]);
    NSString *focusedItemId = R2CString(stateDoc[@"focused_item_id"]);
    NSString *typedText = R2CString(stateDoc[@"typed_text"]);

    NSString *statusTitle = [self firstVisibleMeaningfulTextForState:stateDoc
                                                     preferredNeedles:@[@"在网计算资源"]
                                                             fallback:@"在网计算资源"];
    NSString *statusCount = [self firstVisibleMeaningfulTextForState:stateDoc
                                                     preferredNeedles:@[@"在线节点:"]
                                                             fallback:@"在线节点: 1"];
    NSString *emptyTitle = [self firstVisibleMeaningfulTextForState:stateDoc
                                                    preferredNeedles:@[@"暂无可展示节点"]
                                                            fallback:@"暂无可展示节点"];
    NSString *emptySubtitle = [self firstVisibleMeaningfulTextForState:stateDoc
                                                       preferredNeedles:@[@"节点将从网络自动发现"]
                                                               fallback:@"节点将从网络自动发现或从聊天自动沉淀"];
    NSString *runtimeErrorText = [self firstVisibleMeaningfulTextForState:stateDoc
                                                          preferredNeedles:@[@"attempt_"]
                                                                  fallback:@""];
    NSString *runtimeStartingText = [self firstVisibleMeaningfulTextForState:stateDoc
                                                             preferredNeedles:@[@"runtime starting"]
                                                                     fallback:@""];
    NSArray<NSString *> *controlTitles = [self visibleMeaningfulControlTitlesForState:stateDoc];
    NSMutableArray<NSString *> *filterLabels = [NSMutableArray array];
    for (NSString *title in controlTitles) {
        if ([title isEqualToString:@"展开侧边栏"]) continue;
        if ([title isEqualToString:@"搜索节点"]) continue;
        if ([title isEqualToString:@"多选建群"]) continue;
        [filterLabels addObject:title];
        if ([filterLabels count] >= 6) break;
    }
    if ([filterLabels count] == 0) {
        [filterLabels addObjectsFromArray:@[@"全部", @"mDNS", @"DHT", @"直连", @"LAN", @"WAN"]];
    }

    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @0,
        @"width": @(windowWidth),
        @"height": @(windowHeight),
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @0,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @0,
        @"width": @(windowWidth),
        @"height": @58,
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @1,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @57,
        @"width": @(windowWidth),
        @"height": @1,
        @"corner_radius": @0,
        @"background_color": @"#e5e7eb",
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"menu",
        @"x": @16,
        @"y": @14,
        @"width": @22,
        @"height": @22,
        @"color": @"#111827",
        @"stroke_width": @2.0,
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"search",
        @"x": @(windowWidth - 72.0),
        @"y": @14,
        @"width": @22,
        @"height": @22,
        @"color": @"#111827",
        @"stroke_width": @2.0,
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"users",
        @"x": @(windowWidth - 40.0),
        @"y": @14,
        @"width": @22,
        @"height": @22,
        @"color": @"#111827",
        @"stroke_width": @1.8,
        @"z_index": @3,
    }];

    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @16,
        @"y": @64,
        @"width": @(windowWidth - 32.0),
        @"height": @42,
        @"corner_radius": @14,
        @"background_color": @"#f5f3ff",
        @"border_color": @"#ede9fe",
        @"line_width": @1.0,
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": statusTitle,
        @"x": @28,
        @"y": @76,
        @"width": @(windowWidth - 156.0),
        @"height": @16,
        @"font_size": @14,
        @"font_weight": @"semibold",
        @"text_color": @"#1f2937",
        @"align": @"left",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": statusCount,
        @"x": @(windowWidth - 140.0),
        @"y": @76,
        @"width": @92,
        @"height": @16,
        @"font_size": @12,
        @"font_weight": @"medium",
        @"text_color": @"#a855f7",
        @"align": @"left",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @">",
        @"x": @(windowWidth - 28.0),
        @"y": @75,
        @"width": @12,
        @"height": @18,
        @"font_size": @18,
        @"font_weight": @"regular",
        @"text_color": @"#9ca3af",
        @"align": @"center",
        @"z_index": @3,
    }];

    CGFloat chipX = 16.0;
    CGFloat chipY = 116.0;
    for (NSUInteger index = 0; index < [filterLabels count]; index += 1) {
        NSString *label = filterLabels[index];
        BOOL active = index == 0;
        CGFloat width = [self measuredTextWidth:label fontSize:12.0 weightText:@"medium"] + 22.0;
        if (chipX + width > windowWidth - 16.0) break;
        [commands addObject:@{
            @"type": @"rounded_rect",
            @"x": @(chipX),
            @"y": @(chipY),
            @"width": @(width),
            @"height": @24,
            @"corner_radius": @12,
            @"background_color": active ? @"#a855f7" : @"#f3f4f6",
            @"z_index": @2,
        }];
        [commands addObject:@{
            @"type": @"text_label",
            @"text": label,
            @"x": @(chipX),
            @"y": @(chipY),
            @"width": @(width),
            @"height": @24,
            @"font_size": @12,
            @"font_weight": @"medium",
            @"text_color": active ? @"#ffffff" : @"#6b7280",
            @"align": @"center",
            @"z_index": @3,
        }];
        chipX += width + 8.0;
    }

    CGFloat bannerTop = 148.0;
    if ([runtimeErrorText length] > 0) {
        [commands addObject:@{
            @"type": @"rounded_rect",
            @"x": @16,
            @"y": @(bannerTop),
            @"width": @(windowWidth - 32.0),
            @"height": @32,
            @"corner_radius": @8,
            @"background_color": @"#fef2f2",
            @"border_color": @"#fee2e2",
            @"line_width": @1.0,
            @"z_index": @2,
        }];
        [commands addObject:@{
            @"type": @"text_label",
            @"text": runtimeErrorText,
            @"x": @24,
            @"y": @(bannerTop + 6.0),
            @"width": @(windowWidth - 48.0),
            @"height": @20,
            @"font_size": @12,
            @"font_weight": @"regular",
            @"text_color": @"#dc2626",
            @"align": @"left",
            @"z_index": @3,
        }];
        bannerTop += 42.0;
    }
    if ([runtimeStartingText length] > 0) {
        [commands addObject:@{
            @"type": @"rounded_rect",
            @"x": @16,
            @"y": @(bannerTop),
            @"width": @(windowWidth - 32.0),
            @"height": @32,
            @"corner_radius": @8,
            @"background_color": @"#fffbeb",
            @"border_color": @"#fef3c7",
            @"line_width": @1.0,
            @"z_index": @2,
        }];
        [commands addObject:@{
            @"type": @"text_label",
            @"text": runtimeStartingText,
            @"x": @24,
            @"y": @(bannerTop + 6.0),
            @"width": @(windowWidth - 48.0),
            @"height": @20,
            @"font_size": @12,
            @"font_weight": @"regular",
            @"text_color": @"#b45309",
            @"align": @"left",
            @"z_index": @3,
        }];
        bannerTop += 42.0;
    }

    CGFloat emptyStateTop = MAX(360.0, bannerTop + 156.0);
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"activity",
        @"x": @(floor((windowWidth - 48.0) * 0.5)),
        @"y": @(emptyStateTop),
        @"width": @48,
        @"height": @48,
        @"color": @"#d1d5db",
        @"stroke_width": @2.4,
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": emptyTitle,
        @"x": @32,
        @"y": @(emptyStateTop + 60.0),
        @"width": @(windowWidth - 64.0),
        @"height": @22,
        @"font_size": @14,
        @"font_weight": @"medium",
        @"text_color": @"#9ca3af",
        @"align": @"center",
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": emptySubtitle,
        @"x": @40,
        @"y": @(emptyStateTop + 87.0),
        @"width": @(windowWidth - 80.0),
        @"height": @18,
        @"font_size": @12,
        @"font_weight": @"regular",
        @"text_color": @"#c0c4cc",
        @"align": @"center",
        @"z_index": @2,
    }];

    [self appendPrimaryBottomNavCommandsForActiveLabel:@"节点"
                                           windowWidth:windowWidth
                                          windowHeight:windowHeight
                                              commands:commands];

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

- (NSDictionary *)buildMessageThreadShellRenderPlanDocForState:(NSDictionary *)stateDoc {
    NSMutableArray<NSDictionary *> *commands = [NSMutableArray array];
    CGFloat windowWidth = R2CCGFloat(stateDoc[@"window_width"], self.sessionWidth);
    CGFloat windowHeight = R2CCGFloat(stateDoc[@"window_height"], self.sessionHeight);
    CGFloat contentHeight = R2CCGFloat(stateDoc[@"content_height"], self.contentHeight);
    CGFloat scrollHeight = R2CCGFloat(stateDoc[@"scroll_height"], [self maxScrollOffset]);
    CGFloat scrollOffsetY = R2CCGFloat(stateDoc[@"scroll_offset_y"], self.scrollOffsetY);
    NSString *selectedItemId = R2CString(stateDoc[@"selected_item_id"]);
    NSString *focusedItemId = R2CString(stateDoc[@"focused_item_id"]);
    NSString *typedText = R2CString(stateDoc[@"typed_text"]);
    NSString *inputPlaceholder = @"输入消息...";
    NSString *timeLabel = [self currentShortTimeLabel];
    NSString *placeholderSVG = [self chatPlaceholderSVGText];
    CGFloat bubbleWidth = MIN(windowWidth - 88.0, 222.0);
    CGFloat appCardWidth = MIN(windowWidth - 88.0, 226.0);

    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @0,
        @"width": @(windowWidth),
        @"height": @(windowHeight),
        @"corner_radius": @0,
        @"background_color": @"#f3f4f6",
        @"z_index": @0,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @0,
        @"width": @(windowWidth),
        @"height": @64,
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @1,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @63,
        @"width": @(windowWidth),
        @"height": @1,
        @"corner_radius": @0,
        @"background_color": @"#e5e7eb",
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"arrow-left",
        @"x": @14,
        @"y": @19,
        @"width": @24,
        @"height": @24,
        @"color": @"#111827",
        @"stroke_width": @2.0,
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @50,
        @"y": @12,
        @"width": @40,
        @"height": @40,
        @"corner_radius": @20,
        @"background_color": @"#f3f4f6",
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"svg_image",
        @"x": @58,
        @"y": @20,
        @"width": @24,
        @"height": @24,
        @"svg_text": placeholderSVG,
        @"content_mode": @"fit",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"ASI 助手",
        @"x": @102,
        @"y": @11,
        @"width": @(windowWidth - 118.0),
        @"height": @18,
        @"font_size": @16,
        @"font_weight": @"semibold",
        @"text_color": @"#111827",
        @"align": @"left",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"节点直连会话",
        @"x": @102,
        @"y": @30,
        @"width": @(windowWidth - 118.0),
        @"height": @14,
        @"font_size": @12,
        @"font_weight": @"regular",
        @"text_color": @"#6b7280",
        @"align": @"left",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"等待建链",
        @"x": @102,
        @"y": @44,
        @"width": @(windowWidth - 118.0),
        @"height": @14,
        @"font_size": @11,
        @"font_weight": @"regular",
        @"text_color": @"#6b7280",
        @"align": @"left",
        @"z_index": @3,
    }];

    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @16,
        @"y": @80,
        @"width": @40,
        @"height": @40,
        @"corner_radius": @20,
        @"background_color": @"#f3f4f6",
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"svg_image",
        @"x": @24,
        @"y": @88,
        @"width": @24,
        @"height": @24,
        @"svg_text": placeholderSVG,
        @"content_mode": @"fit",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @64,
        @"y": @80,
        @"width": @(bubbleWidth),
        @"height": @70,
        @"corner_radius": @20,
        @"background_color": @"#ffffff",
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"我是 ASI (Artificial Super Intelligence) 助手。我可以帮助您处理各种任务，请随时吩咐。",
        @"x": @76,
        @"y": @92,
        @"width": @(bubbleWidth - 24.0),
        @"height": @46,
        @"font_size": @13,
        @"font_weight": @"regular",
        @"text_color": @"#111827",
        @"align": @"left",
        @"line_break_mode": @"word_wrap",
        @"vertical_align": @"top",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": timeLabel,
        @"x": @64,
        @"y": @153,
        @"width": @42,
        @"height": @16,
        @"font_size": @12,
        @"font_weight": @"regular",
        @"text_color": @"#9ca3af",
        @"align": @"left",
        @"z_index": @2,
    }];

    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @16,
        @"y": @184,
        @"width": @40,
        @"height": @40,
        @"corner_radius": @20,
        @"background_color": @"#f3f4f6",
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"svg_image",
        @"x": @24,
        @"y": @192,
        @"width": @24,
        @"height": @24,
        @"svg_text": placeholderSVG,
        @"content_mode": @"fit",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @64,
        @"y": @184,
        @"width": @(appCardWidth),
        @"height": @174,
        @"corner_radius": @14,
        @"background_color": @"#ffffff",
        @"border_color": @"#e5e7eb",
        @"line_width": @1.0,
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"linear_gradient_rect",
        @"x": @64,
        @"y": @184,
        @"width": @(appCardWidth),
        @"height": @80,
        @"start_color": @"#eef2ff",
        @"end_color": @"#faf5ff",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"🀄",
        @"x": @(64.0 + floor((appCardWidth - 44.0) * 0.5)),
        @"y": @202,
        @"width": @44,
        @"height": @44,
        @"font_size": @34,
        @"font_weight": @"regular",
        @"text_color": @"#111827",
        @"align": @"center",
        @"z_index": @4,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"中国象棋",
        @"x": @82,
        @"y": @278,
        @"width": @96,
        @"height": @18,
        @"font_size": @14,
        @"font_weight": @"medium",
        @"text_color": @"#111827",
        @"align": @"left",
        @"z_index": @5,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"[应用邀请] 中国象棋",
        @"x": @82,
        @"y": @298,
        @"width": @(appCardWidth - 32.0),
        @"height": @18,
        @"font_size": @11,
        @"font_weight": @"regular",
        @"text_color": @"#6b7280",
        @"align": @"left",
        @"z_index": @5,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"房间ID：truth-room",
        @"x": @82,
        @"y": @320,
        @"width": @(appCardWidth - 32.0),
        @"height": @14,
        @"font_size": @11,
        @"font_weight": @"regular",
        @"text_color": @"#6b7280",
        @"align": @"left",
        @"z_index": @5,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @82,
        @"y": @340,
        @"width": @(appCardWidth - 36.0),
        @"height": @32,
        @"corner_radius": @10,
        @"background_color": @"#e5e7eb",
        @"z_index": @4,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": @"房间已关闭",
        @"x": @82,
        @"y": @340,
        @"width": @(appCardWidth - 36.0),
        @"height": @32,
        @"font_size": @12,
        @"font_weight": @"medium",
        @"text_color": @"#6b7280",
        @"align": @"center",
        @"z_index": @5,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": timeLabel,
        @"x": @64,
        @"y": @364,
        @"width": @42,
        @"height": @16,
        @"font_size": @12,
        @"font_weight": @"regular",
        @"text_color": @"#9ca3af",
        @"align": @"left",
        @"z_index": @2,
    }];

    CGFloat composerTop = windowHeight - 72.0;
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(composerTop),
        @"width": @(windowWidth),
        @"height": @72,
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @20,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @(composerTop),
        @"width": @(windowWidth),
        @"height": @1,
        @"corner_radius": @0,
        @"background_color": @"#e5e7eb",
        @"z_index": @21,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"mic",
        @"x": @16,
        @"y": @(composerTop + 21.0),
        @"width": @24,
        @"height": @24,
        @"color": @"#4b5563",
        @"stroke_width": @2.0,
        @"z_index": @22,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @48,
        @"y": @(composerTop + 14.0),
        @"width": @(windowWidth - 108.0),
        @"height": @36,
        @"corner_radius": @18,
        @"background_color": @"#f3f4f6",
        @"z_index": @21,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"text": inputPlaceholder,
        @"x": @64,
        @"y": @(composerTop + 14.0),
        @"width": @(windowWidth - 140.0),
        @"height": @36,
        @"font_size": @14,
        @"font_weight": @"regular",
        @"text_color": @"#9ca3af",
        @"align": @"left",
        @"z_index": @22,
    }];
    [commands addObject:@{
        @"type": @"icon_symbol",
        @"symbol": @"plus",
        @"x": @(windowWidth - 38.0),
        @"y": @(composerTop + 21.0),
        @"width": @24,
        @"height": @24,
        @"color": @"#4b5563",
        @"stroke_width": @2.5,
        @"z_index": @22,
    }];

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

- (void)appendGenericSurfaceCommandsForItem:(NSDictionary *)item
                                      state:(NSDictionary *)stateDoc
                                    topTrim:(CGFloat)topTrim
                                   commands:(NSMutableArray<NSDictionary *> *)commands {
    if ([self shouldSkipGenericSurfaceItem:item]) return;

    CGFloat windowWidth = R2CCGFloat(stateDoc[@"window_width"], self.sessionWidth);
    CGFloat windowHeight = R2CCGFloat(stateDoc[@"window_height"], self.sessionHeight);
    CGFloat rawX = R2CCGFloat(item[@"x"], 0.0);
    CGFloat width = [self itemRenderWidthForState:stateDoc item:item];
    CGFloat height = [self itemRenderHeightForState:stateDoc item:item];
    CGFloat viewportY = [self itemViewportYForState:stateDoc item:item];
    CGFloat y = viewportY - topTrim;
    if (width <= 6.0 || height <= 6.0) return;
    if (y + height <= 0.0 || y >= windowHeight) return;

    CGFloat x = R2CClamp(rawX, 12.0, MAX(12.0, windowWidth - 48.0));
    CGFloat maxWidth = MAX(36.0, windowWidth - x - 12.0);
    width = MIN(width, maxWidth);

    NSString *itemId = R2CString(item[@"id"]);
    NSString *kind = [[R2CString(item[@"kind"]) lowercaseString] copy];
    NSString *visualRole = [[R2CString(item[@"visual_role"]) lowercaseString] copy];
    NSString *title = [self genericSurfaceTitleForLayoutItem:item];
    NSString *subtitle = [self genericSurfaceSubtitleForLayoutItem:item title:title];
    BOOL selected = [itemId isEqualToString:R2CString(stateDoc[@"selected_item_id"])];
    BOOL focused = [itemId isEqualToString:R2CString(stateDoc[@"focused_item_id"])];
    NSString *typedText = focused ? R2CTrimmedText(stateDoc[@"typed_text"]) : @"";
    if ([typedText length] > 0) {
        NSString *inputText = [NSString stringWithFormat:@"输入 %@", R2CClipText(typedText, 18)];
        subtitle = [subtitle length] > 0 ? [NSString stringWithFormat:@"%@ · %@", subtitle, inputText] : inputText;
    }

    BOOL component = [visualRole isEqualToString:@"component"] || [kind rangeOfString:@"component"].location != NSNotFound;
    BOOL overlay = [visualRole isEqualToString:@"overlay"];
    BOOL surface = overlay
        || component
        || [visualRole isEqualToString:@"surface"]
        || [visualRole isEqualToString:@"scroll_area"];
    BOOL control = R2CBool(item[@"interactive"], NO)
        || [visualRole isEqualToString:@"control"]
        || [kind rangeOfString:@"button"].location != NSNotFound
        || [kind rangeOfString:@"input"].location != NSNotFound;

    NSString *background = R2CTrimmedText(item[@"background_color"]);
    NSString *border = R2CTrimmedText(item[@"border_color"]);
    NSString *titleColor = R2CTrimmedText(item[@"text_color"]);
    NSString *detailColor = R2CTrimmedText(item[@"detail_color"]);
    NSString *skeletonColor = @"#e5e7eb";

    if (component) {
        if ([background length] == 0) background = @"#eef4ff";
        if ([border length] == 0) border = @"#bfd7f6";
        if ([titleColor length] == 0) titleColor = @"#102a43";
        if ([detailColor length] == 0) detailColor = @"#486581";
        skeletonColor = @"#dbeafe";
    } else if (overlay) {
        if ([background length] == 0) background = @"#fff7ed";
        if ([border length] == 0) border = @"#fdba74";
        if ([titleColor length] == 0) titleColor = @"#9a3412";
        if ([detailColor length] == 0) detailColor = @"#c2410c";
        skeletonColor = @"#fed7aa";
    } else if (control) {
        if ([background length] == 0) background = @"#ffffff";
        if ([border length] == 0) border = @"#cbd5e1";
        if ([titleColor length] == 0) titleColor = @"#0f172a";
        if ([detailColor length] == 0) detailColor = @"#475569";
        skeletonColor = @"#dbeafe";
    } else if (surface) {
        if ([background length] == 0) background = @"#ffffff";
        if ([border length] == 0) border = @"#e2e8f0";
        if ([titleColor length] == 0) titleColor = @"#0f172a";
        if ([detailColor length] == 0) detailColor = @"#64748b";
        skeletonColor = @"#e5e7eb";
    } else {
        if ([background length] == 0) background = @"#f8fafc";
        if ([border length] == 0) border = @"#e2e8f0";
        if ([titleColor length] == 0) titleColor = @"#334155";
        if ([detailColor length] == 0) detailColor = @"#64748b";
        skeletonColor = @"#e2e8f0";
    }

    CGFloat lineWidth = 1.0;
    if (selected) {
        border = @"#2563eb";
        lineWidth = 2.0;
        if ([background isEqualToString:@"#ffffff"]) background = @"#eff6ff";
    } else if (focused) {
        border = @"#f59e0b";
        lineWidth = 2.0;
    }

    CGFloat cornerRadius = control ? MIN(18.0, MAX(10.0, floor(height * 0.40))) : MIN(22.0, MAX(12.0, floor(height * 0.25)));
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @(x),
        @"y": @(MAX(0.0, y)),
        @"width": @(width),
        @"height": @(height),
        @"corner_radius": @(cornerRadius),
        @"background_color": background,
        @"border_color": border,
        @"line_width": @(lineWidth),
        @"z_index": @2,
    }];

    CGFloat innerX = x + 14.0;
    CGFloat innerWidth = MAX(28.0, width - 28.0);
    if (control) {
        if ([subtitle length] > 0 && height >= 54.0) {
            [commands addObject:@{
                @"type": @"text_label",
                @"x": @(innerX),
                @"y": @(y + 10.0),
                @"width": @(innerWidth),
                @"height": @18,
                @"text": title,
                @"font_size": @14,
                @"font_weight": @"semibold",
                @"text_color": titleColor,
                @"align": @"left",
                @"z_index": @3,
            }];
            [commands addObject:@{
                @"type": @"text_label",
                @"x": @(innerX),
                @"y": @(y + 30.0),
                @"width": @(innerWidth),
                @"height": @14,
                @"text": subtitle,
                @"font_size": @11,
                @"font_weight": @"regular",
                @"text_color": detailColor,
                @"align": @"left",
                @"z_index": @3,
            }];
        } else {
            [commands addObject:@{
                @"type": @"text_label",
                @"x": @(innerX),
                @"y": @(y + MAX(0.0, floor((height - 20.0) * 0.5))),
                @"width": @(innerWidth),
                @"height": @20,
                @"text": title,
                @"font_size": @14,
                @"font_weight": @"semibold",
                @"text_color": titleColor,
                @"align": width <= 120.0 ? @"center" : @"left",
                @"z_index": @3,
            }];
        }
        return;
    }

    NSString *surfaceTitle = title;
    NSString *surfaceSubtitle = subtitle;
    if ([surfaceTitle length] == 0 && [surfaceSubtitle length] > 0) {
        surfaceTitle = surfaceSubtitle;
        surfaceSubtitle = @"";
    }

    CGFloat cursorY = y + 12.0;
    if ([surfaceTitle length] > 0) {
        [commands addObject:@{
            @"type": @"text_label",
            @"x": @(innerX),
            @"y": @(cursorY),
            @"width": @(innerWidth),
            @"height": @18,
            @"text": surfaceTitle,
            @"font_size": @(surface ? 14 : 12),
            @"font_weight": surface ? @"semibold" : @"medium",
            @"text_color": titleColor,
            @"align": @"left",
            @"z_index": @3,
        }];
        cursorY += 20.0;
    }
    if ([surfaceSubtitle length] > 0 && height >= 52.0) {
        [commands addObject:@{
            @"type": @"text_label",
            @"x": @(innerX),
            @"y": @(cursorY),
            @"width": @(innerWidth),
            @"height": @14,
            @"text": surfaceSubtitle,
            @"font_size": @11,
            @"font_weight": @"regular",
            @"text_color": detailColor,
            @"align": @"left",
            @"z_index": @3,
        }];
        cursorY += 18.0;
    }

    CGFloat remainingHeight = y + height - cursorY - 12.0;
    NSInteger skeletonCount = 0;
    if (remainingHeight >= 42.0) skeletonCount = 3;
    else if (remainingHeight >= 26.0) skeletonCount = 2;
    else if (remainingHeight >= 12.0) skeletonCount = 1;
    NSString *seed = [NSString stringWithFormat:@"%@:%@", itemId, surfaceTitle];
    for (NSInteger index = 0; index < skeletonCount; index += 1) {
        CGFloat fraction = R2CDeterministicWidthFraction(seed, index);
        CGFloat barWidth = MAX(40.0, floor(innerWidth * fraction));
        [commands addObject:@{
            @"type": @"rounded_rect",
            @"x": @(innerX),
            @"y": @(cursorY + (CGFloat)index * 14.0),
            @"width": @(MIN(innerWidth, barWidth)),
            @"height": @8,
            @"corner_radius": @4,
            @"background_color": skeletonColor,
            @"z_index": @3,
        }];
    }
}

- (NSDictionary *)buildGenericSurfaceRenderPlanDocForState:(NSDictionary *)stateDoc {
    NSArray *layoutItems = [self nativeLayoutItems];
    if ([layoutItems count] == 0) {
        return [self buildDebugRenderPlanDocForState:stateDoc];
    }

    NSMutableArray<NSDictionary *> *commands = [NSMutableArray array];
    CGFloat windowWidth = R2CCGFloat(stateDoc[@"window_width"], self.sessionWidth);
    CGFloat windowHeight = R2CCGFloat(stateDoc[@"window_height"], self.sessionHeight);
    CGFloat contentHeight = R2CCGFloat(stateDoc[@"content_height"], self.contentHeight);
    CGFloat scrollHeight = R2CCGFloat(stateDoc[@"scroll_height"], [self maxScrollOffset]);
    CGFloat scrollOffsetY = R2CCGFloat(stateDoc[@"scroll_offset_y"], self.scrollOffsetY);
    NSString *selectedItemId = R2CString(stateDoc[@"selected_item_id"]);
    NSString *focusedItemId = R2CString(stateDoc[@"focused_item_id"]);
    NSString *typedText = R2CString(stateDoc[@"typed_text"]);

    NSString *routeTitle = R2CString(stateDoc[@"route_state"]);
    if ([routeTitle length] == 0) routeTitle = R2CString(stateDoc[@"window_title"]);
    if ([routeTitle length] == 0) routeTitle = @"native_gui";
    routeTitle = [routeTitle stringByReplacingOccurrencesOfString:@"_" withString:@" · "];
    NSString *entryModule = R2CClipText(R2CString(stateDoc[@"entry_module"]), 28);
    NSString *metaText = [NSString stringWithFormat:@"%@ · visible %ld",
                          [entryModule length] > 0 ? entryModule : @"compiled surface",
                          (long)R2CInteger(stateDoc[@"visible_layout_item_count"], 0)];

    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @0,
        @"width": @(windowWidth),
        @"height": @(windowHeight),
        @"corner_radius": @0,
        @"background_color": @"#f8fafc",
        @"z_index": @0,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @0,
        @"width": @(windowWidth),
        @"height": @56,
        @"corner_radius": @0,
        @"background_color": @"#ffffff",
        @"z_index": @1,
    }];
    [commands addObject:@{
        @"type": @"rounded_rect",
        @"x": @0,
        @"y": @55,
        @"width": @(windowWidth),
        @"height": @1,
        @"corner_radius": @0,
        @"background_color": @"#e5e7eb",
        @"z_index": @2,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"x": @16,
        @"y": @12,
        @"width": @(windowWidth - 32.0),
        @"height": @18,
        @"text": routeTitle,
        @"font_size": @16,
        @"font_weight": @"semibold",
        @"text_color": @"#0f172a",
        @"align": @"left",
        @"z_index": @3,
    }];
    [commands addObject:@{
        @"type": @"text_label",
        @"x": @16,
        @"y": @31,
        @"width": @(windowWidth - 32.0),
        @"height": @14,
        @"text": metaText,
        @"font_size": @11,
        @"font_weight": @"regular",
        @"text_color": @"#64748b",
        @"align": @"left",
        @"z_index": @3,
    }];

    CGFloat topTrim = [self genericSurfaceTopTrim];
    NSInteger appendedSurfaceCount = 0;
    for (id rawItem in layoutItems) {
        if (![rawItem isKindOfClass:[NSDictionary class]]) continue;
        NSDictionary *item = (NSDictionary *)rawItem;
        if (![self itemVisibleForState:stateDoc item:item]) continue;
        NSUInteger beforeCount = [commands count];
        [self appendGenericSurfaceCommandsForItem:item state:stateDoc topTrim:topTrim commands:commands];
        if ([commands count] > beforeCount) appendedSurfaceCount += 1;
    }

    if (appendedSurfaceCount == 0) {
        return [self buildDebugRenderPlanDocForState:stateDoc];
    }

    if (R2CBool(stateDoc[@"has_last_click"], NO)) {
        [commands addObject:@{
            @"type": @"interaction_marker",
            @"x": @((NSInteger)R2CInteger(stateDoc[@"last_click_x"], 0)),
            @"y": @((NSInteger)R2CInteger(stateDoc[@"last_click_y"], 0)),
            @"accent_color": @"#2563eb",
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

- (NSDictionary *)buildRenderPlanDocForState:(NSDictionary *)stateDoc {
    NSString *routeState = R2CString(stateDoc[@"route_state"]);
    if ([routeState isEqualToString:@"home_content_detail_open"]) {
        return [self buildHomeContentDetailRenderPlanDocForState:stateDoc];
    }
    if ([routeState isEqualToString:@"tab_messages"]) {
        return [self buildMessagesTabShellRenderPlanDocForState:stateDoc];
    }
    if ([routeState isEqualToString:@"tab_nodes"]) {
        return [self buildNodesTabShellRenderPlanDocForState:stateDoc];
    }
    if ([routeState isEqualToString:@"message_thread"]) {
        return [self buildMessageThreadShellRenderPlanDocForState:stateDoc];
    }
    if ([self isHomeShellSurfaceForRouteState:routeState]) {
        return [self buildHomeShellRenderPlanDocForState:stateDoc];
    }
    if ([[self nativeLayoutItems] count] > 0) {
        return [self buildGenericSurfaceRenderPlanDocForState:stateDoc];
    }
    return [self buildDebugRenderPlanDocForState:stateDoc];
}

- (NSDictionary *)buildDebugRenderPlanDocForState:(NSDictionary *)stateDoc {
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
    if ([R2CString(self.selectedItemId) length] <= 0) return;
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

- (NSFont *)fontForSize:(CGFloat)fontSize weightText:(NSString *)weightText {
    CGFloat resolvedSize = fontSize > 0.0 ? fontSize : 14.0;
    return [NSFont systemFontOfSize:resolvedSize weight:R2CFontWeightFromText(weightText, NSFontWeightRegular)];
}

- (CGFloat)measuredTextWidth:(NSString *)text fontSize:(CGFloat)fontSize weightText:(NSString *)weightText {
    NSString *value = R2CString(text);
    if ([value length] <= 0) return 0.0;
    NSDictionary *attrs = @{
        NSFontAttributeName: [self fontForSize:fontSize weightText:weightText],
    };
    return ceil([value sizeWithAttributes:attrs].width);
}

- (void)drawRoundedRectCommand:(NSDictionary *)command frame:(NSRect)frame {
    CGFloat radius = R2CCGFloat(command[@"corner_radius"], 0.0);
    NSBezierPath *shape = radius > 0.0
        ? [NSBezierPath bezierPathWithRoundedRect:frame xRadius:radius yRadius:radius]
        : [NSBezierPath bezierPathWithRect:frame];
    NSString *backgroundText = R2CTrimmedText(command[@"background_color"]);
    if ([backgroundText length] > 0) {
        [R2CColorFromHexString(backgroundText, [NSColor clearColor]) setFill];
        [shape fill];
    }
    CGFloat lineWidth = R2CCGFloat(command[@"line_width"], 0.0);
    NSString *borderText = R2CTrimmedText(command[@"border_color"]);
    if (lineWidth > 0.0 && [borderText length] > 0) {
        [R2CColorFromHexString(borderText, [NSColor clearColor]) setStroke];
        [shape setLineWidth:lineWidth];
        [shape stroke];
    }
}

- (void)drawTextLabelCommand:(NSDictionary *)command frame:(NSRect)frame {
    NSString *text = R2CString(command[@"text"]);
    if ([text length] <= 0) return;
    CGFloat fontSize = R2CCGFloat(command[@"font_size"], 14.0);
    NSMutableParagraphStyle *style = [[NSMutableParagraphStyle alloc] init];
    NSString *alignText = [[R2CString(command[@"align"]) lowercaseString] copy];
    NSString *lineBreakText = [[R2CString(command[@"line_break_mode"]) lowercaseString] copy];
    if ([alignText isEqualToString:@"center"]) style.alignment = NSTextAlignmentCenter;
    else if ([alignText isEqualToString:@"right"]) style.alignment = NSTextAlignmentRight;
    else style.alignment = NSTextAlignmentLeft;
    if ([lineBreakText isEqualToString:@"word_wrap"] || [lineBreakText isEqualToString:@"wrap"]) {
        style.lineBreakMode = NSLineBreakByWordWrapping;
    } else {
        style.lineBreakMode = NSLineBreakByTruncatingTail;
    }
    NSMutableDictionary *attrs = [@{
        NSFontAttributeName: [self fontForSize:fontSize weightText:R2CString(command[@"font_weight"])],
        NSForegroundColorAttributeName: R2CColorFromHexString(command[@"text_color"],
                                                              [self themeColor:@"text_primary"
                                                                      fallback:[NSColor colorWithCalibratedRed:0.15 green:0.20 blue:0.27 alpha:1.0]]),
    } mutableCopy];
    attrs[NSParagraphStyleAttributeName] = style;
    NSRect insetRect = NSInsetRect(frame,
                                   R2CCGFloat(command[@"padding_x"], 0.0),
                                   R2CCGFloat(command[@"padding_y"], 0.0));
    NSRect textBounds = [text boundingRectWithSize:insetRect.size
                                           options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading
                                        attributes:attrs];
    CGFloat drawY = insetRect.origin.y + MAX(0.0, floor((insetRect.size.height - textBounds.size.height) * 0.5));
    NSString *verticalAlign = [[R2CString(command[@"vertical_align"]) lowercaseString] copy];
    if ([verticalAlign isEqualToString:@"top"]) {
        drawY = insetRect.origin.y;
    } else if ([verticalAlign isEqualToString:@"bottom"]) {
        drawY = insetRect.origin.y + MAX(0.0, insetRect.size.height - textBounds.size.height);
    }
    NSRect drawRect = NSMakeRect(insetRect.origin.x, drawY, insetRect.size.width, MAX(textBounds.size.height, insetRect.size.height));
    [text drawInRect:drawRect withAttributes:attrs];
}

- (void)drawIconSymbolCommand:(NSDictionary *)command frame:(NSRect)frame {
    NSString *symbol = [[R2CString(command[@"symbol"]) lowercaseString] copy];
    if ([symbol length] <= 0) return;
    NSColor *color = R2CColorFromHexString(command[@"color"],
                                           [self themeColor:@"text_primary"
                                                   fallback:[NSColor colorWithCalibratedRed:0.15 green:0.20 blue:0.27 alpha:1.0]]);
    CGFloat strokeWidth = R2CCGFloat(command[@"stroke_width"], 2.0);
    NSBezierPath *path = [NSBezierPath bezierPath];
    [path setLineCapStyle:NSLineCapStyleRound];
    [path setLineJoinStyle:NSLineJoinStyleRound];
    [path setLineWidth:strokeWidth];
    CGFloat minX = NSMinX(frame);
    CGFloat minY = NSMinY(frame);
    CGFloat width = NSWidth(frame);
    CGFloat height = NSHeight(frame);
    if ([symbol isEqualToString:@"arrow-left"]) {
        CGFloat cy = NSMidY(frame);
        CGFloat tailX = minX + width * 0.78;
        CGFloat headX = minX + width * 0.26;
        CGFloat wingY = height * 0.26;
        [path moveToPoint:NSMakePoint(tailX, cy)];
        [path lineToPoint:NSMakePoint(headX, cy)];
        [path moveToPoint:NSMakePoint(headX, cy)];
        [path lineToPoint:NSMakePoint(minX + width * 0.50, minY + wingY)];
        [path moveToPoint:NSMakePoint(headX, cy)];
        [path lineToPoint:NSMakePoint(minX + width * 0.50, minY + height - wingY)];
        [color setStroke];
        [path stroke];
        return;
    }
    if ([symbol isEqualToString:@"menu"]) {
        CGFloat y1 = minY + height * 0.28;
        CGFloat y2 = minY + height * 0.50;
        CGFloat y3 = minY + height * 0.72;
        CGFloat x0 = minX + width * 0.18;
        CGFloat x1 = minX + width * 0.82;
        [path moveToPoint:NSMakePoint(x0, y1)];
        [path lineToPoint:NSMakePoint(x1, y1)];
        [path moveToPoint:NSMakePoint(x0, y2)];
        [path lineToPoint:NSMakePoint(x1, y2)];
        [path moveToPoint:NSMakePoint(x0, y3)];
        [path lineToPoint:NSMakePoint(x1, y3)];
        [color setStroke];
        [path stroke];
        return;
    }
    if ([symbol isEqualToString:@"plus"]) {
        CGFloat cx = NSMidX(frame);
        CGFloat cy = NSMidY(frame);
        CGFloat radius = MIN(width, height) * 0.28;
        [path moveToPoint:NSMakePoint(cx - radius, cy)];
        [path lineToPoint:NSMakePoint(cx + radius, cy)];
        [path moveToPoint:NSMakePoint(cx, cy - radius)];
        [path lineToPoint:NSMakePoint(cx, cy + radius)];
        [color setStroke];
        [path stroke];
        return;
    }
    if ([symbol isEqualToString:@"user-plus"]) {
        CGFloat headRadius = MIN(width, height) * 0.16;
        NSRect headRect = NSMakeRect(minX + width * 0.16,
                                     minY + height * 0.52,
                                     headRadius * 2.0,
                                     headRadius * 2.0);
        [path appendBezierPathWithOvalInRect:headRect];
        CGFloat shoulderY = minY + height * 0.34;
        [path moveToPoint:NSMakePoint(minX + width * 0.10, shoulderY)];
        [path curveToPoint:NSMakePoint(minX + width * 0.56, shoulderY)
             controlPoint1:NSMakePoint(minX + width * 0.16, minY + height * 0.14)
             controlPoint2:NSMakePoint(minX + width * 0.50, minY + height * 0.14)];
        CGFloat plusCX = minX + width * 0.78;
        CGFloat plusCY = minY + height * 0.52;
        CGFloat plusRadius = MIN(width, height) * 0.15;
        [path moveToPoint:NSMakePoint(plusCX - plusRadius, plusCY)];
        [path lineToPoint:NSMakePoint(plusCX + plusRadius, plusCY)];
        [path moveToPoint:NSMakePoint(plusCX, plusCY - plusRadius)];
        [path lineToPoint:NSMakePoint(plusCX, plusCY + plusRadius)];
        [color setStroke];
        [path stroke];
        return;
    }
    if ([symbol isEqualToString:@"users"]) {
        NSBezierPath *front = [NSBezierPath bezierPath];
        NSBezierPath *back = [NSBezierPath bezierPath];
        CGFloat frontRadius = MIN(width, height) * 0.15;
        CGFloat backRadius = MIN(width, height) * 0.13;
        [front appendBezierPathWithOvalInRect:NSMakeRect(minX + width * 0.16,
                                                         minY + height * 0.50,
                                                         frontRadius * 2.0,
                                                         frontRadius * 2.0)];
        [front moveToPoint:NSMakePoint(minX + width * 0.12, minY + height * 0.28)];
        [front curveToPoint:NSMakePoint(minX + width * 0.58, minY + height * 0.28)
              controlPoint1:NSMakePoint(minX + width * 0.16, minY + height * 0.10)
              controlPoint2:NSMakePoint(minX + width * 0.54, minY + height * 0.10)];
        [back appendBezierPathWithOvalInRect:NSMakeRect(minX + width * 0.52,
                                                        minY + height * 0.56,
                                                        backRadius * 2.0,
                                                        backRadius * 2.0)];
        [back moveToPoint:NSMakePoint(minX + width * 0.52, minY + height * 0.38)];
        [back curveToPoint:NSMakePoint(minX + width * 0.88, minY + height * 0.38)
             controlPoint1:NSMakePoint(minX + width * 0.56, minY + height * 0.22)
             controlPoint2:NSMakePoint(minX + width * 0.84, minY + height * 0.22)];
        [color setStroke];
        [front setLineWidth:strokeWidth];
        [front stroke];
        [back setLineWidth:strokeWidth];
        [back stroke];
        return;
    }
    if ([symbol isEqualToString:@"search"]) {
        CGFloat radius = MIN(width, height) * 0.26;
        NSRect circleRect = NSMakeRect(NSMidX(frame) - radius - width * 0.06,
                                       NSMidY(frame) - radius + height * 0.02,
                                       radius * 2.0,
                                       radius * 2.0);
        [path appendBezierPathWithOvalInRect:circleRect];
        [path moveToPoint:NSMakePoint(NSMidX(frame) + radius * 0.48, NSMidY(frame) - radius * 0.48)];
        [path lineToPoint:NSMakePoint(minX + width * 0.84, minY + height * 0.18)];
        [color setStroke];
        [path stroke];
        return;
    }
    if ([symbol isEqualToString:@"activity"]) {
        CGFloat left = minX + width * 0.14;
        CGFloat right = minX + width * 0.86;
        CGFloat midY = NSMidY(frame);
        [path moveToPoint:NSMakePoint(left, midY)];
        [path lineToPoint:NSMakePoint(minX + width * 0.32, midY)];
        [path lineToPoint:NSMakePoint(minX + width * 0.44, minY + height * 0.22)];
        [path lineToPoint:NSMakePoint(minX + width * 0.58, minY + height * 0.78)];
        [path lineToPoint:NSMakePoint(minX + width * 0.70, midY)];
        [path lineToPoint:NSMakePoint(right, midY)];
        [color setStroke];
        [path stroke];
        return;
    }
    if ([symbol isEqualToString:@"mic"]) {
        CGFloat bodyWidth = width * 0.30;
        CGFloat bodyHeight = height * 0.42;
        CGFloat bodyX = NSMidX(frame) - bodyWidth * 0.5;
        CGFloat bodyY = minY + height * 0.36;
        NSBezierPath *body = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(bodyX, bodyY, bodyWidth, bodyHeight)
                                                             xRadius:bodyWidth * 0.5
                                                             yRadius:bodyWidth * 0.5];
        [body setLineWidth:strokeWidth];
        [color setStroke];
        [body stroke];
        [path moveToPoint:NSMakePoint(minX + width * 0.22, minY + height * 0.46)];
        [path lineToPoint:NSMakePoint(minX + width * 0.22, minY + height * 0.36)];
        [path moveToPoint:NSMakePoint(minX + width * 0.78, minY + height * 0.46)];
        [path lineToPoint:NSMakePoint(minX + width * 0.78, minY + height * 0.36)];
        [path moveToPoint:NSMakePoint(minX + width * 0.22, minY + height * 0.36)];
        [path curveToPoint:NSMakePoint(minX + width * 0.78, minY + height * 0.36)
             controlPoint1:NSMakePoint(minX + width * 0.26, minY + height * 0.18)
             controlPoint2:NSMakePoint(minX + width * 0.74, minY + height * 0.18)];
        [path moveToPoint:NSMakePoint(NSMidX(frame), minY + height * 0.18)];
        [path lineToPoint:NSMakePoint(NSMidX(frame), minY + height * 0.08)];
        [path moveToPoint:NSMakePoint(NSMidX(frame) - width * 0.14, minY + height * 0.06)];
        [path lineToPoint:NSMakePoint(NSMidX(frame) + width * 0.14, minY + height * 0.06)];
        [color setStroke];
        [path stroke];
        return;
    }
    if ([symbol isEqualToString:@"heart"]) {
        CGFloat inset = MIN(width, height) * 0.16;
        NSBezierPath *heart = [NSBezierPath bezierPath];
        [heart setLineCapStyle:NSLineCapStyleRound];
        [heart setLineJoinStyle:NSLineJoinStyleRound];
        [heart setLineWidth:strokeWidth];
        CGFloat left = minX + inset;
        CGFloat right = minX + width - inset;
        CGFloat bottom = minY + inset;
        CGFloat top = minY + height - inset;
        CGFloat midX = NSMidX(frame);
        CGFloat midY = minY + height * 0.36;
        [heart moveToPoint:NSMakePoint(midX, bottom)];
        [heart curveToPoint:NSMakePoint(left, minY + height * 0.56)
              controlPoint1:NSMakePoint(midX - width * 0.20, minY + height * 0.18)
              controlPoint2:NSMakePoint(left, minY + height * 0.30)];
        [heart curveToPoint:NSMakePoint(midX, top)
              controlPoint1:NSMakePoint(left, minY + height * 0.82)
              controlPoint2:NSMakePoint(midX - width * 0.18, top)];
        [heart curveToPoint:NSMakePoint(right, minY + height * 0.56)
              controlPoint1:NSMakePoint(midX + width * 0.18, top)
              controlPoint2:NSMakePoint(right, minY + height * 0.82)];
        [heart curveToPoint:NSMakePoint(midX, bottom)
              controlPoint1:NSMakePoint(right, minY + height * 0.30)
              controlPoint2:NSMakePoint(midX + width * 0.20, minY + height * 0.18)];
        [color setStroke];
        [heart stroke];
        return;
    }
    if ([symbol isEqualToString:@"flag"]) {
        CGFloat poleX = minX + width * 0.30;
        CGFloat topY = minY + height * 0.82;
        CGFloat bottomY = minY + height * 0.18;
        [path moveToPoint:NSMakePoint(poleX, bottomY)];
        [path lineToPoint:NSMakePoint(poleX, topY)];
        [path moveToPoint:NSMakePoint(poleX, topY - height * 0.04)];
        [path lineToPoint:NSMakePoint(minX + width * 0.76, topY - height * 0.10)];
        [path lineToPoint:NSMakePoint(minX + width * 0.62, minY + height * 0.48)];
        [path lineToPoint:NSMakePoint(poleX, minY + height * 0.54)];
        [color setStroke];
        [path stroke];
        return;
    }
    if ([symbol isEqualToString:@"sliders-horizontal"]) {
        CGFloat x0 = minX + width * 0.18;
        CGFloat x1 = minX + width * 0.82;
        CGFloat y1 = minY + height * 0.26;
        CGFloat y2 = minY + height * 0.50;
        CGFloat y3 = minY + height * 0.74;
        [path moveToPoint:NSMakePoint(x0, y1)];
        [path lineToPoint:NSMakePoint(x1, y1)];
        [path moveToPoint:NSMakePoint(x0, y2)];
        [path lineToPoint:NSMakePoint(x1, y2)];
        [path moveToPoint:NSMakePoint(x0, y3)];
        [path lineToPoint:NSMakePoint(x1, y3)];
        [color setStroke];
        [path stroke];
        NSArray<NSValue *> *centers = @[
            [NSValue valueWithPoint:NSMakePoint(minX + width * 0.62, y1)],
            [NSValue valueWithPoint:NSMakePoint(minX + width * 0.38, y2)],
            [NSValue valueWithPoint:NSMakePoint(minX + width * 0.70, y3)],
        ];
        CGFloat knob = MAX(3.0, MIN(width, height) * 0.10);
        for (NSValue *value in centers) {
            NSPoint center = [value pointValue];
            NSBezierPath *circle = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(center.x - knob, center.y - knob, knob * 2.0, knob * 2.0)];
            [[NSColor whiteColor] setFill];
            [circle fill];
            [color setStroke];
            [circle setLineWidth:strokeWidth];
            [circle stroke];
        }
    }
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

- (void)drawTruthContentPosterCommand:(NSDictionary *)command frame:(NSRect)frame {
    CGFloat sourceWidth = 960.0;
    CGFloat sourceHeight = 1280.0;
    CGFloat scale = MAX(NSWidth(frame) / sourceWidth, NSHeight(frame) / sourceHeight);
    CGFloat contentWidth = sourceWidth * scale;
    CGFloat contentHeight = sourceHeight * scale;
    CGFloat originX = NSMinX(frame) + (NSWidth(frame) - contentWidth) * 0.5;
    CGFloat originY = NSMinY(frame) + (NSHeight(frame) - contentHeight) * 0.5;
    NSRect contentRect = NSMakeRect(originX, originY, contentWidth, contentHeight);
    NSColor *startColor = R2CColorFromHexString(command[@"accent_color"], [NSColor colorWithCalibratedRed:0.545 green:0.361 blue:0.965 alpha:1.0]);
    NSColor *endColor = R2CColorFromHexString(command[@"end_color"], [NSColor colorWithCalibratedRed:0.067 green:0.094 blue:0.153 alpha:1.0]);
    NSString *label = R2CString(command[@"label"]);

    [NSGraphicsContext saveGraphicsState];
    NSRectClip(frame);

    NSGradient *gradient = [[NSGradient alloc] initWithStartingColor:startColor endingColor:endColor];
    [gradient drawFromPoint:NSMakePoint(NSMinX(contentRect), NSMaxY(contentRect))
                    toPoint:NSMakePoint(NSMaxX(contentRect), NSMinY(contentRect))
                    options:0];

    NSBezierPath *circleA = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(originX + 580.0 * scale,
                                                                              NSMaxY(contentRect) - 400.0 * scale,
                                                                              360.0 * scale,
                                                                              360.0 * scale)];
    [[NSColor colorWithCalibratedWhite:1.0 alpha:0.12] setFill];
    [circleA fill];

    NSBezierPath *circleB = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(originX + 20.0 * scale,
                                                                              NSMaxY(contentRect) - 1200.0 * scale,
                                                                              440.0 * scale,
                                                                              440.0 * scale)];
    [[NSColor colorWithCalibratedWhite:1.0 alpha:0.08] setFill];
    [circleB fill];

    CGFloat fontSize = 88.0 * scale;
    NSFont *font = [NSFont fontWithName:@"Arial-BoldMT" size:fontSize];
    if (font == nil) {
        font = [NSFont systemFontOfSize:fontSize weight:NSFontWeightBold];
    }
    NSDictionary *attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: [NSColor whiteColor],
    };
    CGFloat textX = originX + 96.0 * scale;
    CGFloat textTop = NSMaxY(contentRect) - 1116.0 * scale;
    NSRect textRect = NSMakeRect(textX,
                                 textTop,
                                 MAX(0.0, NSMaxX(contentRect) - textX - 48.0 * scale),
                                 120.0 * scale);
    [label drawInRect:textRect withAttributes:attrs];

    [NSGraphicsContext restoreGraphicsState];
}

- (NSImage *)cachedImageForSVGText:(NSString *)svgText {
    NSString *cacheKey = R2CString(svgText);
    if ([cacheKey length] == 0) return nil;
    if (self.imageCache == nil) {
        self.imageCache = [NSMutableDictionary dictionary];
    }
    NSImage *cached = self.imageCache[cacheKey];
    if (cached != nil) return cached;
    NSData *data = [cacheKey dataUsingEncoding:NSUTF8StringEncoding];
    if (data == nil) return nil;
    NSImage *image = [[NSImage alloc] initWithData:data];
    if (image != nil) {
        self.imageCache[cacheKey] = image;
    }
    return image;
}

- (void)drawSVGImageCommand:(NSDictionary *)command frame:(NSRect)frame {
    NSImage *image = [self cachedImageForSVGText:command[@"svg_text"]];
    if (image == nil) return;
    NSSize size = [image size];
    CGFloat sourceWidth = size.width > 0.0 ? size.width : 1.0;
    CGFloat sourceHeight = size.height > 0.0 ? size.height : 1.0;
    NSString *contentMode = [[R2CString(command[@"content_mode"]) lowercaseString] copy];
    NSRect sourceRect = NSMakeRect(0.0, 0.0, sourceWidth, sourceHeight);
    if ([contentMode isEqualToString:@"cover"]) {
        CGFloat frameRatio = NSWidth(frame) / MAX(NSHeight(frame), 1.0);
        CGFloat sourceRatio = sourceWidth / MAX(sourceHeight, 1.0);
        if (sourceRatio > frameRatio) {
            CGFloat cropWidth = sourceHeight * frameRatio;
            sourceRect.origin.x = floor((sourceWidth - cropWidth) * 0.5);
            sourceRect.size.width = cropWidth;
        } else {
            CGFloat cropHeight = sourceWidth / frameRatio;
            sourceRect.origin.y = floor((sourceHeight - cropHeight) * 0.5);
            sourceRect.size.height = cropHeight;
        }
    }
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(frame);
    [image drawInRect:frame
             fromRect:sourceRect
            operation:NSCompositingOperationSourceOver
             fraction:1.0
       respectFlipped:YES
                hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
    [NSGraphicsContext restoreGraphicsState];
}

- (void)drawLinearGradientRectCommand:(NSDictionary *)command frame:(NSRect)frame {
    NSColor *startColor = R2CColorFromHexString(command[@"start_color"], [NSColor whiteColor]);
    NSColor *endColor = R2CColorFromHexString(command[@"end_color"], [NSColor blackColor]);
    NSGradient *gradient = [[NSGradient alloc] initWithStartingColor:startColor endingColor:endColor];
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(frame);
    [gradient drawFromPoint:NSMakePoint(NSMinX(frame), NSMaxY(frame))
                    toPoint:NSMakePoint(NSMaxX(frame), NSMinY(frame))
                    options:0];
    [NSGraphicsContext restoreGraphicsState];
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
        } else if ([type isEqualToString:@"svg_image"]) {
            [self drawSVGImageCommand:command frame:frame];
        } else if ([type isEqualToString:@"truth_content_poster"]) {
            [self drawTruthContentPosterCommand:command frame:frame];
        } else if ([type isEqualToString:@"linear_gradient_rect"]) {
            [self drawLinearGradientRectCommand:command frame:frame];
        } else if ([type isEqualToString:@"rounded_rect"]) {
            [self drawRoundedRectCommand:command frame:frame];
        } else if ([type isEqualToString:@"text_label"]) {
            [self drawTextLabelCommand:command frame:frame];
        } else if ([type isEqualToString:@"icon_symbol"]) {
            [self drawIconSymbolCommand:command frame:frame];
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
    NSDictionary *sessionRenderPlanDoc = [self.sessionDoc[@"native_render_plan"] isKindOfClass:[NSDictionary class]] ? self.sessionDoc[@"native_render_plan"] : @{};
    NSDictionary *runtimeStateDoc = [self.sessionDoc[@"native_gui_runtime_state"] isKindOfClass:[NSDictionary class]] ? self.sessionDoc[@"native_gui_runtime_state"] : @{};
    NSDictionary *runtimeDoc = [self.sessionDoc[@"native_gui_runtime"] isKindOfClass:[NSDictionary class]] ? self.sessionDoc[@"native_gui_runtime"] : @{};
    if (![runtimeStateDoc isKindOfClass:[NSDictionary class]] || [runtimeStateDoc count] <= 0) {
        runtimeStateDoc = @{
            @"window_title": title ?: @"",
            @"route_state": R2CString(windowDoc[@"route_state"]),
            @"entry_module": R2CString(windowDoc[@"entry_module"]),
            @"window_width": @(width),
            @"window_height": @(height),
            @"content_height": @(MAX(height, R2CCGFloat(planDoc[@"content_height"], R2CCGFloat(windowDoc[@"content_height"], height)))),
            @"scroll_height": @0,
            @"scroll_offset_y": @0,
            @"click_count": @0,
            @"resize_count": @0,
            @"scroll_count": @0,
            @"key_count": @0,
            @"text_count": @0,
            @"focus_count": @0,
            @"has_last_click": @NO,
            @"last_click_x": @0,
            @"last_click_y": @0,
            @"selected_item_id": @"",
            @"selected_source_node_id": @"",
            @"selected_source_module_path": @"",
            @"selected_source_component_name": @"",
            @"selected_source_line": @0,
            @"selected_item_interactive": @NO,
            @"focused_item_id": @"",
            @"typed_text": @"",
            @"last_key": @"",
            @"visible_layout_item_count": @0,
            @"selected_item": @{
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
            },
        };
    }
    view.sessionDoc = self.sessionDoc;
    view.theme = self.sessionDoc[@"theme"];
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
    NSDictionary *renderPlanDoc = [view buildRenderPlanDocForState:runtimeStateDoc];
    if (![renderPlanDoc isKindOfClass:[NSDictionary class]] || ![renderPlanDoc[@"commands"] isKindOfClass:[NSArray class]]) {
        renderPlanDoc = sessionRenderPlanDoc;
    }
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
