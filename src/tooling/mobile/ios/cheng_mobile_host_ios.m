#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <QuartzCore/CAMetalLayer.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <Metal/Metal.h>
#import <Security/Security.h>
#import <CommonCrypto/CommonDigest.h>
#import <dispatch/dispatch.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cheng_mobile_bridge.h"
#include "cheng_mobile_host_core.h"
#include "cheng_mobile_ios_glue.h"

extern uint64_t cheng_app_init(void) __attribute__((weak_import));
extern void cheng_app_set_window(uint64_t app_id, uint64_t window_id, int physical_w, int physical_h, float scale) __attribute__((weak_import));
extern void cheng_app_on_resize(uint64_t app_id, int physical_w, int physical_h, float scale) __attribute__((weak_import));

static CALayer* cheng_mobile_present_layer = nil;
static CAMetalLayer* cheng_mobile_metal_layer = nil;
static id<MTLDevice> cheng_mobile_metal_device = nil;
static id<MTLCommandQueue> cheng_mobile_metal_queue = nil;
static CGSize cheng_mobile_metal_drawable_size = {0, 0};
static CGColorSpaceRef cheng_mobile_present_color_space = NULL;
static char* cheng_mobile_resource_root = NULL;
static uint64_t cheng_mobile_app_id_v2 = 0u;
static int cheng_mobile_ios_host_api_injected = 0;

static int cheng_mobile_ios_has_app_abi_v2(void);

static void cheng_mobile_ios_safe_copy(char* dst, int32_t cap, const char* src) {
  if (dst == NULL || cap <= 0) {
    return;
  }
  if (src == NULL) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, (size_t)cap - 1u);
  dst[cap - 1] = '\0';
}

static NSString* cheng_mobile_ios_string_or_empty(const char* text) {
  if (text == NULL || text[0] == '\0') {
    return @"";
  }
  NSString* value = [NSString stringWithUTF8String:text];
  return value != nil ? value : @"";
}

static NSData* cheng_mobile_ios_utf8_data(NSString* text) {
  NSString* value = text != nil ? text : @"";
  NSData* data = [value dataUsingEncoding:NSUTF8StringEncoding];
  return data != nil ? data : [NSData data];
}

static NSString* cheng_mobile_ios_hex_from_data(NSData* data) {
  if (data == nil || data.length == 0) {
    return @"";
  }
  const uint8_t* bytes = (const uint8_t*)data.bytes;
  NSMutableString* out = [NSMutableString stringWithCapacity:data.length * 2u];
  for (NSUInteger i = 0; i < data.length; i += 1u) {
    [out appendFormat:@"%02x", bytes[i]];
  }
  return out;
}

static NSString* cheng_mobile_ios_hex_from_utf8(NSString* text) {
  return cheng_mobile_ios_hex_from_data(cheng_mobile_ios_utf8_data(text));
}

static NSData* cheng_mobile_ios_sha256_parts(NSArray<NSData*>* parts) {
  CC_SHA256_CTX ctx;
  CC_SHA256_Init(&ctx);
  for (NSData* part in parts) {
    if (part != nil && part.length > 0) {
      CC_SHA256_Update(&ctx, part.bytes, (CC_LONG)part.length);
    }
  }
  uint8_t digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256_Final(digest, &ctx);
  return [NSData dataWithBytes:digest length:sizeof(digest)];
}

static NSData* cheng_mobile_ios_fixed32(NSData* raw) {
  NSMutableData* out = [NSMutableData dataWithLength:32u];
  if (raw == nil || raw.length == 0u) {
    return out;
  }
  const uint8_t* src = (const uint8_t*)raw.bytes;
  NSUInteger src_len = raw.length;
  if (src_len > 32u) {
    src += src_len - 32u;
    src_len = 32u;
  }
  memcpy((uint8_t*)out.mutableBytes + (32u - src_len), src, src_len);
  return out;
}

static NSString* cheng_mobile_ios_sanitize_for_text(NSString* text) {
  NSString* out = text != nil ? text : @"";
  out = [out stringByReplacingOccurrencesOfString:@"|" withString:@"/"];
  out = [out stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
  out = [out stringByReplacingOccurrencesOfString:@"\r" withString:@" "];
  return out;
}

static NSString* cheng_mobile_ios_map_biometric_error(NSError* error) {
  if (error == nil) {
    return @"biometric_not_available";
  }
  if ([error.domain isEqualToString:LAErrorDomain]) {
    switch (error.code) {
      case LAErrorUserCancel:
      case LAErrorSystemCancel:
      case LAErrorAppCancel:
      case LAErrorUserFallback:
        return @"biometric_user_cancelled";
      case LAErrorBiometryLockout:
        return @"biometric_lockout";
      case LAErrorBiometryNotAvailable:
      case LAErrorBiometryNotEnrolled:
        return @"biometric_not_available";
      default:
        return @"biometric_not_available";
    }
  }
  return @"biometric_not_available";
}

static NSString* cheng_mobile_ios_map_sec_status(OSStatus status) {
  switch (status) {
    case errSecUserCanceled:
    case errSecAuthFailed:
      return @"biometric_user_cancelled";
    case errSecInteractionNotAllowed:
      return @"biometric_lockout";
    case errSecItemNotFound:
      return @"key_not_found";
    default:
      return @"secure_store_error";
  }
}

static NSString* cheng_mobile_ios_default_prompt(NSString* title, NSString* reason) {
  if (reason.length > 0) {
    return reason;
  }
  if (title.length > 0) {
    return title;
  }
  return @"Authorize biometric DID";
}

static NSString* cheng_mobile_ios_default_device_label(void) {
  UIDevice* device = [UIDevice currentDevice];
  NSString* system_name = device.systemName != nil ? device.systemName : @"iOS";
  NSString* model = device.model != nil ? device.model : @"device";
  return [NSString stringWithFormat:@"%@ %@", system_name, model];
}

static NSString* cheng_mobile_ios_sensor_id(LAContext* context) {
  if (@available(iOS 11.0, *)) {
    switch (context.biometryType) {
      case LABiometryTypeFaceID:
        return @"ios.faceid";
      case LABiometryTypeTouchID:
        return @"ios.touchid";
      default:
        return @"ios.biometric.strong";
    }
  }
  return @"ios.biometric.strong";
}

static BOOL cheng_mobile_ios_ensure_attestor_key(NSString* alias, NSString** outError) {
  NSData* tag_data = cheng_mobile_ios_utf8_data(alias);
  CFErrorRef ac_error = NULL;
  SecAccessControlRef access = SecAccessControlCreateWithFlags(
      kCFAllocatorDefault,
      kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
      kSecAccessControlPrivateKeyUsage | kSecAccessControlBiometryCurrentSet,
      &ac_error);
  if (access == NULL) {
    if (outError) {
      *outError = @"secure_store_error";
    }
    if (ac_error != NULL) {
      CFRelease(ac_error);
    }
    return NO;
  }

  NSDictionary* attrs = @{
    (__bridge id)kSecAttrKeyType : (__bridge id)kSecAttrKeyTypeECSECPrimeRandom,
    (__bridge id)kSecAttrKeySizeInBits : @256,
    (__bridge id)kSecAttrTokenID : (__bridge id)kSecAttrTokenIDSecureEnclave,
    (__bridge id)kSecPrivateKeyAttrs : @{
      (__bridge id)kSecAttrIsPermanent : @YES,
      (__bridge id)kSecAttrApplicationTag : tag_data,
      (__bridge id)kSecAttrAccessControl : (__bridge id)access,
    },
  };
  CFErrorRef key_error = NULL;
  SecKeyRef key = SecKeyCreateRandomKey((__bridge CFDictionaryRef)attrs, &key_error);
  CFRelease(access);
  if (key == NULL) {
    if (key_error != NULL && (OSStatus)CFErrorGetCode(key_error) == errSecDuplicateItem) {
      CFRelease(key_error);
      if (outError) {
        *outError = @"";
      }
      return YES;
    }
    if (outError) {
      *outError = @"secure_store_error";
    }
    if (key_error != NULL) {
      CFRelease(key_error);
    }
    return NO;
  }
  CFRelease(key);
  if (outError) {
    *outError = @"";
  }
  return YES;
}

static SecKeyRef cheng_mobile_ios_copy_private_key(NSString* alias,
                                                   LAContext* context,
                                                   NSString* prompt,
                                                   OSStatus* outStatus) {
  NSData* tag_data = cheng_mobile_ios_utf8_data(alias);
  NSMutableDictionary* query = [@{
    (__bridge id)kSecClass : (__bridge id)kSecClassKey,
    (__bridge id)kSecAttrApplicationTag : tag_data,
    (__bridge id)kSecReturnRef : @YES,
  } mutableCopy];
  if (context != nil) {
    query[(__bridge id)kSecUseAuthenticationContext] = context;
    if (@available(iOS 11.0, *)) {
      context.localizedReason = prompt.length > 0 ? prompt : @"Authorize biometric DID";
    }
  }
  CFTypeRef item = NULL;
  OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &item);
  if (outStatus) {
    *outStatus = status;
  }
  if (status != errSecSuccess || item == NULL) {
    if (item != NULL) {
      CFRelease(item);
    }
    return NULL;
  }
  return (SecKeyRef)item;
}

static NSData* cheng_mobile_ios_public_key_raw65(SecKeyRef private_key) {
  if (private_key == NULL) {
    return nil;
  }
  SecKeyRef public_key = SecKeyCopyPublicKey(private_key);
  if (public_key == NULL) {
    return nil;
  }
  CFErrorRef pub_error = NULL;
  CFDataRef pub_ref = SecKeyCopyExternalRepresentation(public_key, &pub_error);
  CFRelease(public_key);
  if (pub_ref == NULL) {
    if (pub_error != NULL) {
      CFRelease(pub_error);
    }
    return nil;
  }
  NSData* pub = CFBridgingRelease(pub_ref);
  if (pub.length == 65u) {
    return pub;
  }
  if (pub.length == 64u) {
    NSMutableData* out = [NSMutableData dataWithLength:65u];
    uint8_t* bytes = (uint8_t*)out.mutableBytes;
    bytes[0] = 0x04u;
    memcpy(bytes + 1u, pub.bytes, 64u);
    return out;
  }
  return nil;
}

static NSData* cheng_mobile_ios_der_to_raw64(NSData* der) {
  if (der == nil || der.length < 8u) {
    return nil;
  }
  const uint8_t* p = (const uint8_t*)der.bytes;
  NSUInteger len = der.length;
  if (p[0] != 0x30u) {
    return nil;
  }
  NSUInteger idx = 1u;
  if (idx >= len) {
    return nil;
  }
  if ((p[idx] & 0x80u) != 0u) {
    NSUInteger count = (NSUInteger)(p[idx] & 0x7fu);
    idx += 1u + count;
  } else {
    idx += 1u;
  }
  if (idx + 2u >= len || p[idx] != 0x02u) {
    return nil;
  }
  idx += 1u;
  NSUInteger r_len = 0u;
  if ((p[idx] & 0x80u) != 0u) {
    NSUInteger count = (NSUInteger)(p[idx] & 0x7fu);
    idx += 1u;
    for (NSUInteger i = 0u; i < count; i += 1u) {
      r_len = (r_len << 8u) | p[idx + i];
    }
    idx += count;
  } else {
    r_len = p[idx];
    idx += 1u;
  }
  if (idx + r_len + 2u > len) {
    return nil;
  }
  NSData* r = [NSData dataWithBytes:p + idx length:r_len];
  idx += r_len;
  if (p[idx] != 0x02u) {
    return nil;
  }
  idx += 1u;
  NSUInteger s_len = 0u;
  if ((p[idx] & 0x80u) != 0u) {
    NSUInteger count = (NSUInteger)(p[idx] & 0x7fu);
    idx += 1u;
    for (NSUInteger i = 0u; i < count; i += 1u) {
      s_len = (s_len << 8u) | p[idx + i];
    }
    idx += count;
  } else {
    s_len = p[idx];
    idx += 1u;
  }
  if (idx + s_len > len) {
    return nil;
  }
  NSData* s = [NSData dataWithBytes:p + idx length:s_len];
  const uint8_t* r_bytes = (const uint8_t*)r.bytes;
  const uint8_t* s_bytes = (const uint8_t*)s.bytes;
  NSUInteger r_trim = (r.length > 1u && r_bytes[0] == 0u) ? 1u : 0u;
  NSUInteger s_trim = (s.length > 1u && s_bytes[0] == 0u) ? 1u : 0u;
  NSData* r_fixed = cheng_mobile_ios_fixed32([NSData dataWithBytes:r_bytes + r_trim length:r.length - r_trim]);
  NSData* s_fixed = cheng_mobile_ios_fixed32([NSData dataWithBytes:s_bytes + s_trim length:s.length - s_trim]);
  NSMutableData* out = [NSMutableData dataWithLength:64u];
  memcpy((uint8_t*)out.mutableBytes, r_fixed.bytes, 32u);
  memcpy((uint8_t*)out.mutableBytes + 32u, s_fixed.bytes, 32u);
  return out;
}

static NSString* cheng_mobile_ios_attestation_signable(NSString* request_id,
                                                       int32_t purpose,
                                                       NSString* sensor_id,
                                                       NSString* root_key_id,
                                                       NSString* device_label,
                                                       NSData* cert_chain_cid,
                                                       NSData* liveness_cid,
                                                       NSData* device_binding_seed_hash,
                                                       NSData* feature_commitment,
                                                       NSData* attestor_pub_key) {
  NSMutableString* out = [NSMutableString string];
  [out appendString:@"v3_biometric_attestation_version=2\n"];
  [out appendFormat:@"request_id=%@\n", cheng_mobile_ios_hex_from_utf8(request_id)];
  [out appendFormat:@"purpose=%d\n", (int)purpose];
  [out appendFormat:@"sensor_id=%@\n", cheng_mobile_ios_hex_from_utf8(sensor_id)];
  [out appendString:@"platform=2\n"];
  [out appendString:@"attestation_kind=1\n"];
  [out appendFormat:@"root_key_id=%@\n", cheng_mobile_ios_hex_from_utf8(root_key_id)];
  [out appendFormat:@"cert_chain_cid=%@\n", cheng_mobile_ios_hex_from_data(cert_chain_cid)];
  [out appendFormat:@"liveness_cid=%@\n", cheng_mobile_ios_hex_from_data(liveness_cid)];
  [out appendFormat:@"device_binding_seed_hash=%@\n", cheng_mobile_ios_hex_from_data(device_binding_seed_hash)];
  [out appendFormat:@"device_label=%@\n", cheng_mobile_ios_hex_from_utf8(device_label)];
  [out appendFormat:@"feature_commitment=%@\n", cheng_mobile_ios_hex_from_data(feature_commitment)];
  [out appendFormat:@"attestor_pub_key=%@\n", cheng_mobile_ios_hex_from_data(attestor_pub_key)];
  return out;
}

static void cheng_mobile_ios_log_print(int32_t level, const char* msg) {
  NSLog(@"[cheng:%d] %s", (int)level, msg != NULL ? msg : "");
}

static int32_t cheng_mobile_ios_biometric_fingerprint_authorize(
    const char* request_id,
    int32_t purpose,
    const char* did_text,
    const char* prompt_title,
    const char* prompt_reason,
    const char* device_binding_seed_hint,
    const char* device_label_hint,
    char* out_feature32_hex,
    int32_t out_feature32_cap,
    char* out_device_binding_seed,
    int32_t out_device_binding_seed_cap,
    char* out_device_label,
    int32_t out_device_label_cap,
    char* out_sensor_id,
    int32_t out_sensor_id_cap,
    char* out_hardware_attestation,
    int32_t out_hardware_attestation_cap,
    char* out_error,
    int32_t out_error_cap) {
  @autoreleasepool {
    cheng_mobile_ios_safe_copy(out_feature32_hex, out_feature32_cap, "");
    cheng_mobile_ios_safe_copy(out_device_binding_seed, out_device_binding_seed_cap, "");
    cheng_mobile_ios_safe_copy(out_device_label, out_device_label_cap, "");
    cheng_mobile_ios_safe_copy(out_sensor_id, out_sensor_id_cap, "");
    cheng_mobile_ios_safe_copy(out_hardware_attestation, out_hardware_attestation_cap, "");
    cheng_mobile_ios_safe_copy(out_error, out_error_cap, "");

    NSString* alias = @"cheng.bio.did.attestor.v1";
    NSString* root_key_id = @"ios.secureenclave.biometric.v1";
    NSString* request_id_text = cheng_mobile_ios_string_or_empty(request_id);
    NSString* did_text_value = cheng_mobile_ios_string_or_empty(did_text);
    NSString* prompt = cheng_mobile_ios_default_prompt(
        cheng_mobile_ios_string_or_empty(prompt_title),
        cheng_mobile_ios_string_or_empty(prompt_reason));
    NSString* device_binding_seed = cheng_mobile_ios_string_or_empty(device_binding_seed_hint);
    if (device_binding_seed.length == 0) {
      device_binding_seed = @"ios.device.binding.v1";
    }
    NSString* device_label = cheng_mobile_ios_string_or_empty(device_label_hint);
    if (device_label.length == 0) {
      device_label = cheng_mobile_ios_default_device_label();
    }

    NSString* key_error = nil;
    if (!cheng_mobile_ios_ensure_attestor_key(alias, &key_error)) {
      cheng_mobile_ios_safe_copy(out_error, out_error_cap, key_error.UTF8String);
      return 0;
    }

    LAContext* context = [[LAContext alloc] init];
    context.localizedFallbackTitle = @"";
    NSError* can_error = nil;
    if (![context canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics error:&can_error]) {
      NSString* mapped = cheng_mobile_ios_map_biometric_error(can_error);
      cheng_mobile_ios_safe_copy(out_error, out_error_cap, mapped.UTF8String);
      return 0;
    }

    OSStatus key_status = errSecSuccess;
    SecKeyRef private_key = cheng_mobile_ios_copy_private_key(alias, context, prompt, &key_status);
    if (private_key == NULL) {
      NSString* mapped = cheng_mobile_ios_map_sec_status(key_status);
      cheng_mobile_ios_safe_copy(out_error, out_error_cap, mapped.UTF8String);
      return 0;
    }

    NSData* pub_raw = cheng_mobile_ios_public_key_raw65(private_key);
    if (pub_raw == nil || pub_raw.length != 65u) {
      CFRelease(private_key);
      cheng_mobile_ios_safe_copy(out_error, out_error_cap, "secure_store_error");
      return 0;
    }

    NSString* sensor_id = cheng_mobile_ios_sensor_id(context);
    NSString* cert_chain_text = [NSString stringWithFormat:@"pub_b64=%@",
                                 [pub_raw base64EncodedStringWithOptions:0]];
    int64_t now_ms = (int64_t)([[NSDate date] timeIntervalSince1970] * 1000.0);
    NSString* liveness_text = [NSString stringWithFormat:@"ios-biometric|request_id=%@|purpose=%d|did=%@|ts_ms=%lld",
                                                         request_id_text,
                                                         (int)purpose,
                                                         cheng_mobile_ios_sanitize_for_text(did_text_value),
                                                         (long long)now_ms];
    NSData* feature32 = cheng_mobile_ios_sha256_parts(@[
      cheng_mobile_ios_utf8_data(@"v3.biometric.ios.feature"),
      pub_raw,
    ]);
    NSData* device_binding_seed_hash = cheng_mobile_ios_sha256_parts(@[
      cheng_mobile_ios_utf8_data(@"v3.biometric.device.binding.seed"),
      cheng_mobile_ios_utf8_data(device_binding_seed),
    ]);
    NSData* feature_commitment = cheng_mobile_ios_sha256_parts(@[
      cheng_mobile_ios_utf8_data(@"v3.biometric.feature.commitment"),
      feature32,
    ]);
    NSData* cert_chain_cid = cheng_mobile_ios_sha256_parts(@[
      cheng_mobile_ios_utf8_data(@"v3.biometric.platform.cert.chain"),
      cheng_mobile_ios_utf8_data(@"2"),
      cheng_mobile_ios_utf8_data(@"|"),
      cheng_mobile_ios_utf8_data(@"1"),
      cheng_mobile_ios_utf8_data(@"|"),
      cheng_mobile_ios_utf8_data(root_key_id),
      cheng_mobile_ios_utf8_data(@"|"),
      cheng_mobile_ios_utf8_data(cert_chain_text),
    ]);
    NSData* liveness_cid = cheng_mobile_ios_sha256_parts(@[
      cheng_mobile_ios_utf8_data(@"v3.biometric.liveness"),
      cheng_mobile_ios_utf8_data(liveness_text),
    ]);
    NSString* signable = cheng_mobile_ios_attestation_signable(request_id_text,
                                                               purpose,
                                                               sensor_id,
                                                               root_key_id,
                                                               device_label,
                                                               cert_chain_cid,
                                                               liveness_cid,
                                                               device_binding_seed_hash,
                                                               feature_commitment,
                                                               pub_raw);

    CFErrorRef sign_error = NULL;
    CFDataRef der_ref = SecKeyCreateSignature(private_key,
                                              kSecKeyAlgorithmECDSASignatureMessageX962SHA256,
                                              (__bridge CFDataRef)cheng_mobile_ios_utf8_data(signable),
                                              &sign_error);
    CFRelease(private_key);
    if (der_ref == NULL) {
      NSString* mapped = @"secure_store_error";
      if (sign_error != NULL) {
        NSError* ns_error = CFBridgingRelease(sign_error);
        if ([ns_error.domain isEqualToString:NSOSStatusErrorDomain]) {
          mapped = cheng_mobile_ios_map_sec_status((OSStatus)ns_error.code);
        }
      }
      cheng_mobile_ios_safe_copy(out_error, out_error_cap, mapped.UTF8String);
      return 0;
    }

    NSData* der = CFBridgingRelease(der_ref);
    NSData* raw64 = cheng_mobile_ios_der_to_raw64(der);
    if (raw64 == nil || raw64.length != 64u) {
      cheng_mobile_ios_safe_copy(out_error, out_error_cap, "secure_store_error");
      return 0;
    }

    NSString* attestation_text = [signable stringByAppendingFormat:@"signature=%@\n",
                                  cheng_mobile_ios_hex_from_data(raw64)];
    cheng_mobile_ios_safe_copy(out_feature32_hex, out_feature32_cap, cheng_mobile_ios_hex_from_data(feature32).UTF8String);
    cheng_mobile_ios_safe_copy(out_device_binding_seed, out_device_binding_seed_cap, device_binding_seed.UTF8String);
    cheng_mobile_ios_safe_copy(out_device_label, out_device_label_cap, device_label.UTF8String);
    cheng_mobile_ios_safe_copy(out_sensor_id, out_sensor_id_cap, sensor_id.UTF8String);
    cheng_mobile_ios_safe_copy(out_hardware_attestation, out_hardware_attestation_cap, attestation_text.UTF8String);
    return 1;
  }
}

static void cheng_mobile_ios_try_inject_host_api(void) {
  if (cheng_mobile_ios_host_api_injected || !cheng_mobile_ios_has_app_abi_v2()) {
    return;
  }
  ChengHostPlatformAPI api = {0};
  api.log_print = cheng_mobile_ios_log_print;
  api.biometric_fingerprint_authorize = cheng_mobile_ios_biometric_fingerprint_authorize;
  cheng_app_inject_host_api(&api);
  cheng_mobile_ios_host_api_injected = 1;
}

static int cheng_mobile_ios_has_app_abi_v2(void) {
  return cheng_app_init != NULL && cheng_app_set_window != NULL;
}

static int cheng_mobile_ios_has_resize_abi(void) {
  return cheng_app_on_resize != NULL;
}

static void cheng_mobile_ios_try_init_app_v2(void) {
  if (!cheng_mobile_ios_has_app_abi_v2() || cheng_mobile_app_id_v2 != 0u) {
    return;
  }
  cheng_mobile_ios_try_inject_host_api();
  cheng_mobile_app_id_v2 = cheng_app_init();
}

static CGColorSpaceRef cheng_mobile_ios_color_space(void) {
  if (cheng_mobile_present_color_space == NULL) {
    cheng_mobile_present_color_space = CGColorSpaceCreateDeviceRGB();
  }
  return cheng_mobile_present_color_space;
}

static void cheng_mobile_ios_reset_metal(void) {
  cheng_mobile_metal_layer = nil;
  cheng_mobile_metal_device = nil;
  cheng_mobile_metal_queue = nil;
  cheng_mobile_metal_drawable_size = CGSizeMake(0, 0);
}

static void cheng_mobile_ios_set_resource_root(NSString* path) {
  if (cheng_mobile_resource_root != NULL) {
    free(cheng_mobile_resource_root);
    cheng_mobile_resource_root = NULL;
  }
  if (path == nil) {
    return;
  }
  const char* utf = [path UTF8String];
  if (utf == NULL) {
    return;
  }
  size_t len = strlen(utf);
  char* copy = (char*)malloc(len + 1);
  if (copy == NULL) {
    return;
  }
  memcpy(copy, utf, len);
  copy[len] = '\0';
  cheng_mobile_resource_root = copy;
}

static void cheng_mobile_ios_init_resource_root(void) {
  NSString* resourcePath = [[NSBundle mainBundle] resourcePath];
  if (resourcePath == nil) {
    return;
  }
  NSString* assetsPath = [resourcePath stringByAppendingPathComponent:@"Assets"];
  BOOL isDir = NO;
  if ([[NSFileManager defaultManager] fileExistsAtPath:assetsPath isDirectory:&isDir] && isDir) {
    cheng_mobile_ios_set_resource_root(assetsPath);
  } else {
    cheng_mobile_ios_set_resource_root(resourcePath);
  }
}

static void cheng_mobile_ios_setup_metal_layer(CAMetalLayer* layer) {
  if (layer == nil) {
    return;
  }
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (device == nil) {
    return;
  }
  layer.device = device;
  layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  layer.framebufferOnly = NO;
  layer.contentsScale = [UIScreen mainScreen].scale;
  cheng_mobile_metal_layer = layer;
  cheng_mobile_metal_device = device;
  cheng_mobile_metal_queue = [device newCommandQueue];
  cheng_mobile_metal_drawable_size = CGSizeMake(0, 0);
}

static void cheng_mobile_ios_update_drawable_size(int width, int height) {
  if (cheng_mobile_metal_layer == nil) {
    return;
  }
  CGFloat scale = cheng_mobile_metal_layer.contentsScale > 0.0 ? cheng_mobile_metal_layer.contentsScale : 1.0;
  CGSize bounds = cheng_mobile_metal_layer.bounds.size;
  if (bounds.width <= 0 || bounds.height <= 0) {
    bounds = CGSizeMake(width, height);
  }
  CGSize desired = CGSizeMake(bounds.width * scale, bounds.height * scale);
  if (desired.width <= 0 || desired.height <= 0) {
    return;
  }
  if (!CGSizeEqualToSize(desired, cheng_mobile_metal_drawable_size)) {
    cheng_mobile_metal_drawable_size = desired;
    cheng_mobile_metal_layer.drawableSize = desired;
  }
  cheng_mobile_ios_try_init_app_v2();
  if (cheng_mobile_ios_has_app_abi_v2() && cheng_mobile_app_id_v2 != 0u) {
    cheng_app_set_window(
        cheng_mobile_app_id_v2,
        (uint64_t)(uintptr_t)cheng_mobile_present_layer,
        (int)desired.width,
        (int)desired.height,
        (float)scale);
    if (cheng_mobile_ios_has_resize_abi()) {
      cheng_app_on_resize(cheng_mobile_app_id_v2, (int)desired.width, (int)desired.height, (float)scale);
    }
  }
}

void cheng_mobile_ios_set_present_layer(void* layer) {
  cheng_mobile_present_layer = (__bridge CALayer*)layer;
  cheng_mobile_ios_reset_metal();
  if (cheng_mobile_present_layer != nil) {
    cheng_mobile_present_layer.contentsGravity = kCAGravityResizeAspect;
    cheng_mobile_present_layer.contentsScale = [UIScreen mainScreen].scale;
    if ([cheng_mobile_present_layer isKindOfClass:[CAMetalLayer class]]) {
      cheng_mobile_ios_setup_metal_layer((CAMetalLayer*)cheng_mobile_present_layer);
    }
    cheng_mobile_ios_try_init_app_v2();
    CGFloat scale = cheng_mobile_present_layer.contentsScale > 0.0 ? cheng_mobile_present_layer.contentsScale : [UIScreen mainScreen].scale;
    CGSize sz = cheng_mobile_present_layer.bounds.size;
    if (cheng_mobile_ios_has_app_abi_v2() && cheng_mobile_app_id_v2 != 0u) {
      cheng_app_set_window(
          cheng_mobile_app_id_v2,
          (uint64_t)(uintptr_t)cheng_mobile_present_layer,
          (int)(sz.width * scale),
          (int)(sz.height * scale),
          (float)scale);
      if (cheng_mobile_ios_has_resize_abi()) {
        cheng_app_on_resize(
            cheng_mobile_app_id_v2,
            (int)(sz.width * scale),
            (int)(sz.height * scale),
            (float)scale);
      }
    }
  }
}

int cheng_mobile_host_init(const ChengMobileConfig* cfg) {
  (void)cfg;
  cheng_mobile_host_core_reset();
  if (cheng_mobile_resource_root == NULL) {
    cheng_mobile_ios_init_resource_root();
  }
  ChengMobileEvent ev = {0};
  ev.kind = MRE_RUNTIME_STARTED;
  cheng_mobile_host_core_push(&ev);
  return 0;
}

int cheng_mobile_host_open_window(const ChengMobileConfig* cfg) {
  int windowId = 1;
  ChengMobileEvent ev = {0};
  ev.kind = MRE_WINDOW_OPENED;
  ev.windowId = windowId;
  ev.message = cfg ? cfg->title : "Cheng Mobile";
  cheng_mobile_host_core_push(&ev);
  return windowId;
}

int cheng_mobile_host_poll_event(ChengMobileEvent* outEvent) {
  return cheng_mobile_host_core_pop(outEvent);
}

static void cheng_mobile_host_present_metal(const void* pixels, int width, int height, int strideBytes) {
  if (cheng_mobile_metal_layer == nil || cheng_mobile_metal_queue == nil) {
    return;
  }
  if (pixels == NULL || width <= 0 || height <= 0 || strideBytes <= 0) {
    return;
  }
  @autoreleasepool {
    cheng_mobile_ios_update_drawable_size(width, height);
    id<CAMetalDrawable> drawable = [cheng_mobile_metal_layer nextDrawable];
    if (drawable == nil) {
      return;
    }
    id<MTLTexture> texture = drawable.texture;
    if (texture == nil) {
      return;
    }
    int texWidth = (int)texture.width;
    int texHeight = (int)texture.height;
    int copyWidth = width < texWidth ? width : texWidth;
    int copyHeight = height < texHeight ? height : texHeight;
    int maxSrcWidth = strideBytes / 4;
    if (maxSrcWidth > 0 && copyWidth > maxSrcWidth) {
      copyWidth = maxSrcWidth;
    }
    if (copyWidth <= 0 || copyHeight <= 0) {
      return;
    }
    MTLRegion region = MTLRegionMake2D(0, 0, copyWidth, copyHeight);
    [texture replaceRegion:region mipmapLevel:0 withBytes:pixels bytesPerRow:strideBytes];
    id<MTLCommandBuffer> buffer = [cheng_mobile_metal_queue commandBuffer];
    [buffer presentDrawable:drawable];
    [buffer commit];
  }
}

void cheng_mobile_host_present(const void* pixels, int width, int height, int strideBytes) {
  if (cheng_mobile_metal_layer != nil && cheng_mobile_metal_queue != nil) {
    cheng_mobile_host_present_metal(pixels, width, height, strideBytes);
    return;
  }
  if (pixels == NULL || cheng_mobile_present_layer == nil) {
    return;
  }
  if (width <= 0 || height <= 0 || strideBytes <= 0) {
    return;
  }
  size_t dataSize = (size_t)strideBytes * (size_t)height;
  CFDataRef data = CFDataCreate(kCFAllocatorDefault, pixels, dataSize);
  if (data == NULL) {
    return;
  }
  CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
  if (provider == NULL) {
    CFRelease(data);
    return;
  }
  CGColorSpaceRef colorSpace = cheng_mobile_ios_color_space();
  CGBitmapInfo info = kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst;
  CGImageRef image = CGImageCreate(width, height, 8, 32, strideBytes, colorSpace, info, provider, NULL, false, kCGRenderingIntentDefault);
  CFRelease(provider);
  CFRelease(data);
  if (image == NULL) {
    return;
  }
  if ([NSThread isMainThread]) {
    cheng_mobile_present_layer.contents = (__bridge id)image;
  } else {
    CGImageRef retained = CGImageRetain(image);
    dispatch_async(dispatch_get_main_queue(), ^{
      cheng_mobile_present_layer.contents = (__bridge id)retained;
      CGImageRelease(retained);
    });
  }
  CGImageRelease(image);
}

void cheng_mobile_host_shutdown(const char* reason) {
  (void)reason;
  cheng_mobile_present_layer = nil;
  cheng_mobile_ios_reset_metal();
  if (cheng_mobile_resource_root != NULL) {
    free(cheng_mobile_resource_root);
    cheng_mobile_resource_root = NULL;
  }
  if (cheng_mobile_present_color_space != NULL) {
    CGColorSpaceRelease(cheng_mobile_present_color_space);
    cheng_mobile_present_color_space = NULL;
  }
}

const char* cheng_mobile_host_default_resource_root(void) {
  return cheng_mobile_resource_root;
}

#endif
