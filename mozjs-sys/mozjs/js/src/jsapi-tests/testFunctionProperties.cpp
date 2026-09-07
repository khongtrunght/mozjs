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
