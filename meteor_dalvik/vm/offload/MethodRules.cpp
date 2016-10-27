#include "Dalvik.h"

#define METHOD_FLAG_OFFLOADABLE   0x1
#define METHOD_FLAG_METHWRITE     0X2
#define METHOD_FLAG_METHLOG       0X4

#define ALLOW_REFLECT

enum {
  VERSION_V0 = 1,
};

typedef struct MethodRule {
  const char* name;
  const char* definingClass;
  const char* shorty;
  u4 flags;
} MethodRule;

u4 hashMethodRule(MethodRule* mr) {
  const char* s;
  u4 ret = 0;
  for(s = mr->name; *s; ++s)          ret = ret * 997 + *s;
  for(s = mr->definingClass; *s; ++s) ret = ret * 991 + *s;
  for(s = mr->shorty; *s; ++s)        ret = ret * 983 + *s;
  return ret;
}

int compareMethodRules(const void* pmra, const void* pmrb) {
  const MethodRule* mra = (const MethodRule*)pmra;
  const MethodRule* mrb = (const MethodRule*)pmrb;
  int res = strcmp(mra->name, mrb->name);
  if(!res) res = strcmp(mra->definingClass, mrb->definingClass);
  if(!res) res = strcmp(mra->shorty, mrb->shorty);
  return res;
}

static MethodRule* lookupMethodRules(Method* method) {
  MethodRule mr;
  mr.name = method->name;
  mr.definingClass = method->clazz->descriptor;
  mr.shorty = method->shorty;
  return (MethodRule*)dvmHashTableLookup(gDvm.offRulesHash, hashMethodRule(&mr),
                                         &mr, compareMethodRules, false);
}

void offLoadNativeMethod(Method* method) {
  if(gDvm.optimizing) return;
  MethodRule* mr = lookupMethodRules(method);
  if(mr) {
    if(mr->flags & METHOD_FLAG_OFFLOADABLE) {
      method->accessFlags |= ACC_OFFLOADABLE;
    }
    if(mr->flags & METHOD_FLAG_METHWRITE) {
      gDvm.offMethWriteImpl = method;
    }
    if(mr->flags & METHOD_FLAG_METHLOG) {
      gDvm.offMethLogNative = method;
    }
  }
}

static void makeRule(const char* name, const char* definingClass,
                     const char* shorty, u4 flags) {
  MethodRule* mr = (MethodRule*)malloc(sizeof(MethodRule));
  mr->name = name;
  mr->definingClass = definingClass;
  mr->shorty = shorty;
  mr->flags = flags;
  if(mr != dvmHashTableLookup(gDvm.offRulesHash, hashMethodRule(mr), mr,
                              compareMethodRules, true)) {
    ALOGW("Method rule already present in hash?");
    free(mr);
  }
}

#define SETCLASS(clazz) \
  if((className = (clazz)))
#define METHIDEAL(name, proto) do { \
    makeRule((name), className, (proto), METHOD_FLAG_OFFLOADABLE); \
  } while(0)

bool offMethodRulesStartup() {
  gDvm.offRulesHash = dvmHashTableCreate(256, free);
  gDvm.offMethWriteImpl = gDvm.offMethLogNative = NULL;

  /* Custom rules. */
  makeRule("write", "Lorg/apache/harmony/luni/platform/OSFileSystem;",
           "JILII", METHOD_FLAG_OFFLOADABLE | METHOD_FLAG_METHWRITE);
  makeRule("println_native", "Landroid/util/Log;",
           "IIILL", METHOD_FLAG_OFFLOADABLE | METHOD_FLAG_METHLOG);

  /* Generic ideal rules. */
  const char* className;
/*
  SETCLASS("Lcom/ibm/icu4jni/charset/NativeConverter;") {
    METHIDEAL("canEncode", "ZJI");
    METHIDEAL("charsetForName", "LL");
    METHIDEAL("closeConverter", "VJ");
    METHIDEAL("contains", "ZLL");
    METHIDEAL("decode", "IJLILILZ");
    METHIDEAL("encode", "IJLILILZ");
    METHIDEAL("flushByteToChar", "IJLIL");
    METHIDEAL("flushCharToByte", "IJLIL");
    METHIDEAL("getAvailableCharsetNames", "L");
    METHIDEAL("getAveBytesPerChar", "FJ");
    METHIDEAL("getAveCharsPerByte", "FJ");
    METHIDEAL("getMaxCharsPerByte", "IJ");
    METHIDEAL("getMaxBytesPerChar", "IJ");
    METHIDEAL("getSubstitutionBytes", "LJ");
    METHIDEAL("openConverter", "JL");
    METHIDEAL("resetByteToChar", "VJ");
    METHIDEAL("resetCharToByte", "VJ");
    METHIDEAL("setCallbackDecode", "IJIIL");
    METHIDEAL("setCallbackEncode", "IJIIL");
  }
*/
  SETCLASS("Ljava/lang/Math;") {
    METHIDEAL("acos", "DD");
    METHIDEAL("asin", "DD");
    METHIDEAL("atan", "DD");
    METHIDEAL("atan2", "DDD");
    METHIDEAL("cbrt", "DD");
    METHIDEAL("ceil", "DD");
    METHIDEAL("cos", "DD");
    METHIDEAL("cosh", "DD");
    METHIDEAL("exp", "DD");
    METHIDEAL("expm1", "DD");
    METHIDEAL("floor", "DD");
    METHIDEAL("hypot", "DDD");
    METHIDEAL("IEEEremainder", "DDD");
    METHIDEAL("log", "DD");
    METHIDEAL("log10", "DD");
    METHIDEAL("log1p", "DD");
    METHIDEAL("pow", "DDD");
    METHIDEAL("rint", "DD");
    METHIDEAL("sin", "DD");
    METHIDEAL("sinh", "DD");
    METHIDEAL("sqrt", "DD");
    METHIDEAL("tan", "DD");
    METHIDEAL("tanh", "DD");
    METHIDEAL("nextafter", "DDD");
    METHIDEAL("nextafterf", "FFF");
  }
  SETCLASS("Ljava/lang/Float;") {
    METHIDEAL("floatToIntBits", "IF");
  }
  SETCLASS("Ljava/lang/Double;") {
    METHIDEAL("doubleToLongBits", "JD");
  }
/*
  SETCLASS("Ljava/util/zip/Inflater;") {
    METHIDEAL("createStream", "JZ");
    METHIDEAL("endImpl", "VJ");
    METHIDEAL("getAdlerImpl", "IJ");
    METHIDEAL("getTotalInImpl", "JJ");
    METHIDEAL("getTotalOutImpl", "JJ");
    METHIDEAL("inflateImpl", "ILIIJ");
    METHIDEAL("resetImpl", "VJ");
    METHIDEAL("setDictionaryImpl", "VLIIJ");
    METHIDEAL("setInputImpl", "VLIIJ");
    // METHIDEAL("setFileInputImpl", "ILJIJ");
  }
  SETCLASS("Ljava/util/zip/Deflater;") {
    METHIDEAL("createStream", "JIIZ");
    METHIDEAL("deflateImpl", "ILIIJI");
    METHIDEAL("endImpl", "VJ");
    METHIDEAL("getAdlerImpl", "IJ");
    METHIDEAL("getTotalInImpl", "JJ");
    METHIDEAL("getTotalOutImpl", "JJ");
    METHIDEAL("resetImpl", "VJ");
    METHIDEAL("setDictionaryImpl", "VLIIJ");
    METHIDEAL("setInputImpl", "VLIIJ");
    METHIDEAL("setLevelsImpl", "VIIJ");
  }
  SETCLASS("Ljava/util/zip/CRC32;") {
    METHIDEAL("updateImpl", "JLIIJ");
    METHIDEAL("updateByteImpl", "JBJ");
  }
*/
  SETCLASS("Ljava/lang/Character;") {
    METHIDEAL("digitImpl", "III");
    METHIDEAL("getTypeImpl", "II");
    METHIDEAL("isDefinedImpl", "ZI");
    METHIDEAL("isDigitImpl", "ZI");
    METHIDEAL("isIdentifierIgnorableImpl", "ZI");
    METHIDEAL("isLetterImpl", "ZI");
    METHIDEAL("isLetterOrDigitImpl", "ZI");
    METHIDEAL("isLowerCaseImpl", "ZI");
    METHIDEAL("isMirroredImpl", "ZI");
    METHIDEAL("isSpaceCharImpl", "ZI");
    METHIDEAL("isTitleCaseImpl", "ZI");
    METHIDEAL("isUnicodeIdentifierPartImpl", "ZI");
    METHIDEAL("isUnicodeIdentifierStartImpl", "ZI");
    METHIDEAL("isUpperCaseImpl", "ZI");
    METHIDEAL("isWhitespaceImpl", "ZI");
    METHIDEAL("ofImpl", "II");
    METHIDEAL("toLowerCaseImpl", "II");
    METHIDEAL("toTitleCaseImpl", "II");
    METHIDEAL("toUpperCaseImpl", "II");
  }
  SETCLASS("Lcom/ibm/icu4jni/util/ICU;") {
    METHIDEAL("getAvailableBreakIteratorLocalesNative", "L");
    METHIDEAL("getAvailableCalendarLocalesNative", "L");
    METHIDEAL("getAvailableCollatorLocalesNative", "L");
    METHIDEAL("getAvailableDateFormatLocalesNative", "L");
    METHIDEAL("getAvailableLocalesNative", "L");
    METHIDEAL("getAvailableNumberFormatLocalesNative", "L");
    METHIDEAL("getCurrencyCodeNative", "LL");
    METHIDEAL("getCurrencySymbolNative", "LLL");
    METHIDEAL("getDisplayCountryNative", "LLL");
    METHIDEAL("getDisplayLanguageNative", "LLL");
    METHIDEAL("getDisplayVariantNative", "LLL");
    METHIDEAL("getISO3CountryNative", "LL");
    METHIDEAL("getISO3LanguageNative", "LL");
    METHIDEAL("getISOCountriesNative", "L");
    METHIDEAL("getISOLanguagesNative", "L");
    METHIDEAL("initLocaleDataImpl", "ZLL");
    METHIDEAL("toLowerCase", "LLL");
    METHIDEAL("toUpperCase", "LLL");
  }
  /* Add internal native calls missing from replay. */
  SETCLASS("Ljava/lang/Object;") {
    METHIDEAL("internalClone", "LL");
    METHIDEAL("hashCode", "I");
    METHIDEAL("getClass", "L");

    /* These three are handled specially within the VM */
    METHIDEAL("notify", "V");
    METHIDEAL("notifyAll", "V");
    METHIDEAL("wait", "VJI");
  }

  SETCLASS("Ljava/lang/Class;") {
    METHIDEAL("getComponentType", "L");
    METHIDEAL("getSignatureAnnotation", "L");
    METHIDEAL("getDeclaredClasses", "LLZ");
    METHIDEAL("getDeclaredConstructors", "LLZ");
    METHIDEAL("getDeclaredFields", "LLZ");
    METHIDEAL("getDeclaredMethods", "LLZ");
    METHIDEAL("getInterfaces", "L");
    METHIDEAL("getModifiers", "ILZ");
    METHIDEAL("getNameNative", "L");
    METHIDEAL("getSuperclass", "L");
    METHIDEAL("isAssignableFrom", "ZL");
    METHIDEAL("isInstance", "ZL");
    METHIDEAL("isInterface", "Z");
    METHIDEAL("isPrimitive", "Z");
    METHIDEAL("newInstanceImpl", "L");
    METHIDEAL("getDeclaringClass", "L");
    METHIDEAL("getEnclosingClass", "L");
    METHIDEAL("getEnclosingConstructor", "L");
    METHIDEAL("getEnclosingMethod", "L");
    METHIDEAL("isAnonymousClass", "Z");
    METHIDEAL("getDeclaredAnnotations", "L");
    METHIDEAL("getInnerClassName", "L");
  }

  SETCLASS("Ljava/lang/VMThread;") {
    METHIDEAL("currentThread", "L");
    METHIDEAL("getStatus", "I");
    METHIDEAL("holdsLock", "ZL");
    METHIDEAL("interrupt", "V");
    METHIDEAL("interrupted", "Z");
    METHIDEAL("isInterrupted", "Z");
    METHIDEAL("sleep", "VJI");
    METHIDEAL("yield", "V");
  }

  SETCLASS("Ljava/lang/reflect/Method;") {
    METHIDEAL("getMethodModifiers", "ILI");
#ifdef ALLOW_REFLECT
    METHIDEAL("invokeNative", "LLLLLLIZ");
#endif
    METHIDEAL("getDeclaredAnnotations", "LLI");
    METHIDEAL("getParameterAnnotations", "LLI");
    METHIDEAL("getDefaultValue", "LLI");
    METHIDEAL("getSignatureAnnotation", "LLI");
  }

  SETCLASS("Ljava/lang/reflect/Field;") {
    METHIDEAL("getFieldModifiers", "ILI");
    METHIDEAL("getField", "LLLLIZ");
    METHIDEAL("getBField", "BLLL");
    METHIDEAL("getCField", "CLLL");
    METHIDEAL("getDField", "DLLL");
    METHIDEAL("getFField", "FLLL");
    METHIDEAL("getIField", "ILLL");
    METHIDEAL("getJField", "JLLL");
    METHIDEAL("getSField", "SLLL");
    METHIDEAL("getZField", "ZLLL");
    METHIDEAL("setField", "VLLLIZL");
    METHIDEAL("setBField", "VLLLIZB");
    METHIDEAL("setCField", "VLLLIZC");
    METHIDEAL("setDField", "VLLLIZD");
    METHIDEAL("setFField", "VLLLIZF");
    METHIDEAL("setIField", "VLLLIZI");
    METHIDEAL("setJField", "VLLLIZJ");
    METHIDEAL("setSField", "VLLLIZS");
    METHIDEAL("setZField", "VLLLIZZ");
    METHIDEAL("getDeclaredAnnotations", "LLI");
    METHIDEAL("getSignatureAnnotation", "LLI");
  }

  SETCLASS("Ljava/lang/reflect/Constructor;") {
#ifdef ALLOW_REFLECT
    METHIDEAL("constructNative", "LLLLIZ");
#endif
    METHIDEAL("getConstructorModifiers", "ILI");
    METHIDEAL("getDeclaredAnnotations", "LLI");
    METHIDEAL("getParameterAnnotations", "LLI");
    METHIDEAL("getSignatureAnnotation", "LLI");
  }

  SETCLASS("Ljava/lang/reflect/Array;") {
    METHIDEAL("createObjectArray", "LLI");
    METHIDEAL("createMultiArray", "LLL");
  }

  SETCLASS("Ljava/lang/reflect/AccessibleObject;") {
    METHIDEAL("getClassSignatureAnnotation", "LL");
  }

  SETCLASS("Lsun/misc/Unsafe;") {
    METHIDEAL("objectFieldOffset0", "JL");
    METHIDEAL("arrayBaseOffset0", "IL");
    METHIDEAL("arrayIndexScale0", "IL");
    METHIDEAL("compareAndSwapInt", "ZLJII");
    METHIDEAL("compareAndSwapLong", "ZLJJJ");
    METHIDEAL("compareAndSwapObject", "ZLJLL");
    METHIDEAL("getIntVolatile", "ILJ");
    METHIDEAL("putIntVolatile", "VLJI");
    METHIDEAL("getLongVolatile", "JLJ");
    METHIDEAL("putLongVolatile", "VLJJ");
    METHIDEAL("getObjectVolatile", "LLJ");
    METHIDEAL("putObjectVolatile", "VLJL");
    METHIDEAL("getInt", "ILJ");
    METHIDEAL("putInt", "VLJI");
    METHIDEAL("getLong", "JLJ");
    METHIDEAL("putLong", "VLJJ");
    METHIDEAL("getObject", "LLJ");
    METHIDEAL("putObject", "VLJL");
  }

  SETCLASS("Ldalvik/system/VMStack;") {
    METHIDEAL("getCallingClassLoader", "L");
    METHIDEAL("getCallingClassLoader2", "L");
    METHIDEAL("getStackClass2", "L");
    METHIDEAL("getClasses", "LIZ");
    METHIDEAL("getThreadStackTrace", "LL");
  }

  /* Add non-native functions that are OK to offload though SA may think
   * otherwise. */
  SETCLASS("Ljava/lang/IntegralToString;") {
    METHIDEAL("convertInt", "LLI");
    METHIDEAL("intToString", "LII");
    METHIDEAL("longToString", "LJI");
    METHIDEAL("convertLong", "LLJ");
  }

  SETCLASS("Ljava/lang/Long;") {
    METHIDEAL("valueOf", "LJ");
  }
  SETCLASS("Ljava/lang/Integer;") {
    METHIDEAL("valueOf", "LI");
  }
  SETCLASS("Ljava/lang/Short;") {
    METHIDEAL("valueOf", "LS");
  }
  SETCLASS("Ljava/lang/Character;") {
    METHIDEAL("valueOf", "LC");
  }
  SETCLASS("Ljava/lang/Byte;") {
    METHIDEAL("valueOf", "LB");
  }
  SETCLASS("Ljava/lang/Boolean;") {
    METHIDEAL("valueOf", "LZ");
  }

  SETCLASS("Ljava/lang/String;") {
    METHIDEAL("intern", "L");
  }

  SETCLASS("Ljava/lang/System;") {
    METHIDEAL("arraycopy", "VLILII");
    METHIDEAL("currentTimeMillis", "J"); /* This is sort of a lie. */
    METHIDEAL("nanoTime", "J");
    METHIDEAL("identityHashCode", "IL");
  }

  SETCLASS("Ljava/lang/Throwable;") {
    METHIDEAL("fillInStackTrace", "L"); /* Touches a volatile but it's OK. */
    METHIDEAL("nativeFillInStackTrace", "L");
    METHIDEAL("nativeGetStackTrace", "LL");
  }

  SETCLASS("Ljava/nio/charset/Charsets;") {
    METHIDEAL("asciiBytesToChars", "VLIIL");
    METHIDEAL("isoLatin1BytesToChars", "VLIIL");
    METHIDEAL("toAsciiBytes", "LLII");
    METHIDEAL("toIsoLatin1Bytes", "LLII");
    METHIDEAL("toUtf8Bytes", "LLII");
  }

  SETCLASS("Lorg/apache/harmony/luni/util/FloatingPointParser;") {
    METHIDEAL("parseFltImpl", "FLI");
    METHIDEAL("parseDblImpl", "DLI");
  }

  return true;
}
