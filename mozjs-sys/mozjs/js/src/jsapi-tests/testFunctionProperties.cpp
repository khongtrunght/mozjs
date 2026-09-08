/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "js/PropertyAndElement.h"  // JS_GetProperty
#include "js/Stack.h"
#include "jsapi-tests/tests.h"

BEGIN_TEST(testFunctionProperties) {
  JS::RootedValue x(cx);
  EVAL("(function f() {})", &x);

  JS::RootedObject obj(cx, x.toObjectOrNull());

  JS::RootedValue y(cx);
  CHECK(JS_GetProperty(cx, obj, "arguments", &y));
  CHECK(y.isNull());

  CHECK(JS_GetProperty(cx, obj, "caller", &y));
  CHECK(y.isNull());

  return true;
}
END_TEST(testFunctionProperties)

BEGIN_TEST(testNativeFunctionSourceFormat) {
  JS::RootedValue result(cx);
  JS::SetNativeFunctionSourceFormat(cx, JS::NativeFunctionSourceFormat::V8);
  EVAL(
      "var getter = Object.getOwnPropertyDescriptor(Map.prototype, 'size').get;"
      "var setter = Object.getOwnPropertyDescriptor(Object.prototype, '__proto__').set;"
      "var firstGetter = getter.toString(), firstSetter = setter.toString();"
      "getter.name; setter.name;"
      "firstGetter === 'function get size() { [native code] }' && "
      "firstSetter === 'function set __proto__() { [native code] }' && "
      "firstGetter === getter.toString() && firstSetter === setter.toString()",
      &result);
  CHECK(result.toBoolean());
  JS::SetNativeFunctionSourceFormat(
      cx, JS::NativeFunctionSourceFormat::SpiderMonkey);
  EVAL(
      "eval.toString() === 'function eval() {\\n    [native code]\\n}' && "
      "(function(){}).bind(null).toString() === "
      "'function() {\\n    [native code]\\n}' && "
      "Object.getOwnPropertyDescriptor(Map.prototype, 'size').get.toString() "
      "=== 'function size() {\\n    [native code]\\n}'",
      &result);
  CHECK(result.toBoolean());

  EXEC(
      "var originalNativeSource = eval.toSource();"
      "var originalBoundSource = (function(){}).bind(null).toSource();");
  JS::SetNativeFunctionSourceFormat(cx, JS::NativeFunctionSourceFormat::V8);
  EVAL(
      "eval.toString() === 'function eval() { [native code] }' && "
      "(function(){}).bind(null).toString() === "
      "'function () { [native code] }' && "
      "Object.getOwnPropertyDescriptor(Map.prototype, 'size').get.toString() "
      "=== 'function get size() { [native code] }' && "
      "eval.toSource() === originalNativeSource && "
      "(function(){}).bind(null).toSource() === originalBoundSource && "
      "(function real(a) { return a; }).toString() === "
      "'function real(a) { return a; }'",
      &result);
  CHECK(result.toBoolean());

  JS::SetNativeFunctionSourceFormat(
      cx, JS::NativeFunctionSourceFormat::SpiderMonkey);
  EVAL("eval.toString() === 'function eval() {\\n    [native code]\\n}'",
       &result);
  CHECK(result.toBoolean());
  return true;
}

JSObject* createGlobal(JSPrincipals* principals = nullptr) override {
  JS::RealmOptions options;
  options.creationOptions().setToSourceEnabled(true);
  global = JS_NewGlobalObject(cx, getGlobalClass(), principals,
                              JS::FireOnNewGlobalHook, options);
  return global;
}

END_TEST(testNativeFunctionSourceFormat)

static bool ClassCallableHook(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  args.rval().setObject(args.callee());
  return true;
}

static JSString* LegacyClassSource(JSContext* cx, JS::HandleObject obj,
                                   bool isToSource) {
  return JS_NewStringCopyZ(cx, "function Blob() {\n    [native code]\n}");
}

BEGIN_TEST(testNativeFunctionSourceFormat_ClassCallable) {
  static const JSClassOps classOps = {
      .call = ClassCallableHook,
      .construct = ClassCallableHook,
  };
  static const js::ObjectOps objectOps = {
      .funToString = LegacyClassSource,
  };
  static const JSClass classCallable = {
      "Blob", 0, &classOps, nullptr, nullptr, &objectOps};
  static const JSClass bareCallable = {"BareCallable", 0, &classOps};
  static const JSClass anonymousCallable = {"", 0, &classOps};
  const struct {
    const char* name;
    const JSClass* clasp;
  } classes[] = {
      {"classCallable", &classCallable},
      {"bareCallable", &bareCallable},
      {"anonymousCallable", &anonymousCallable},
  };
  for (const auto& test : classes) {
    JS::RootedObject object(cx, JS_NewObject(cx, test.clasp));
    CHECK(object);
    CHECK(JS_DefineProperty(cx, global, test.name, object, 0));
  }
  EXEC(
      "var classProxy = new Proxy(classCallable, {});"
      "var classBound = Function.prototype.bind.call(classCallable, null);"
      "var revoked = Proxy.revocable(classCallable, {}); revoked.revoke();"
      "Object.defineProperty(classCallable, 'name', {"
      "  get() { throw new Error('unexpected name lookup'); }"
      "});"
      "var originalClassSource = Function.prototype.toSource.call(classCallable);"
      "function sourceOf(f) {"
      "  try { return Function.prototype.toString.call(f); }"
      "  catch (e) { if (e instanceof TypeError) return 'TypeError'; throw e; }"
      "}");

  const struct {
    const char* expression;
    const char* spiderMonkey;
    const char* v8;
  } cases[] = {
      {"sourceOf(classCallable)", "function Blob() {\n    [native code]\n}",
       "function Blob() { [native code] }"},
      {"sourceOf(bareCallable)", "TypeError",
       "function BareCallable() { [native code] }"},
      {"sourceOf(anonymousCallable)", "TypeError",
       "function () { [native code] }"},
      {"sourceOf(classProxy)", "function () {\n    [native code]\n}",
       "function () { [native code] }"},
      {"sourceOf(classBound)", "function() {\n    [native code]\n}",
       "function () { [native code] }"},
      {"sourceOf(revoked.proxy)", "function () {\n    [native code]\n}",
       "function () { [native code] }"},
      {"String(Object.getOwnPropertyDescriptor(Map.prototype, 'size').get)",
       "function size() {\n    [native code]\n}",
       "function get size() { [native code] }"},
      {"Function.prototype.toSource.call(classCallable)",
       "function Blob() {\n    [native code]\n}",
       "function Blob() {\n    [native code]\n}"},
      {"sourceOf({})", "TypeError", "TypeError"},
  };

  for (auto format : {JS::NativeFunctionSourceFormat::SpiderMonkey,
                      JS::NativeFunctionSourceFormat::V8,
                      JS::NativeFunctionSourceFormat::SpiderMonkey}) {
    JS::SetNativeFunctionSourceFormat(cx, format);
    for (const auto& test : cases) {
      JS::RootedValue result(cx);
      CHECK(evaluate(test.expression, __FILE__, __LINE__, &result));
      CHECK(result.isString());
      JS::RootedString source(cx, result.toString());
      JS::UniqueChars bytes = JS_EncodeStringToUTF8(cx, source);
      CHECK(bytes);
      const char* expected = format == JS::NativeFunctionSourceFormat::V8
                                 ? test.v8
                                 : test.spiderMonkey;
      CHECK(strcmp(bytes.get(), expected) == 0);
    }
  }
  return true;
}

JSObject* createGlobal(JSPrincipals* principals = nullptr) override {
  JS::RealmOptions options;
  options.creationOptions().setToSourceEnabled(true);
  global = JS_NewGlobalObject(cx, getGlobalClass(), principals,
                              JS::FireOnNewGlobalHook, options);
  return global;
}

END_TEST(testNativeFunctionSourceFormat_ClassCallable)
