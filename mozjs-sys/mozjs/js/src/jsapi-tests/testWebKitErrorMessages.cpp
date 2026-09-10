/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "js/Exception.h"
#include "js/Stack.h"
#include "jsapi-tests/tests.h"

// JavaScriptCore-shaped error messages, selected with
// JS::SetErrorMessageFormat. The '(evaluating ...)' clauses carry the
// bytecode-decompiled expression, which for these simple cases matches the
// text JavaScriptCore prints from the original source.

static bool CompareExceptionMessage(JSContext* cx, const char* expected) {
  JS::ExceptionStack exnStack(cx);
  if (!JS::StealPendingExceptionStack(cx, &exnStack)) {
    return false;
  }
  JS::ErrorReportBuilder report(cx);
  if (!report.init(cx, exnStack, JS::ErrorReportBuilder::WithSideEffects)) {
    return false;
  }
  const char* message = report.report()->message().c_str();
  if (strcmp(message, expected) != 0) {
    printf("expected: %s\n     got: %s\n", expected, message);
    return false;
  }
  return true;
}

static bool ExpectThrowMessage(JSContext* cx, bool thrown,
                               const char* expected) {
  if (thrown) {
    JS_ReportErrorASCII(cx, "script was expected to throw");
    return false;
  }
  if (!JS_IsExceptionPending(cx)) {
    return false;
  }
  return CompareExceptionMessage(cx, expected);
}

BEGIN_TEST(testWebKitErrorMessages) {
  JS::SetErrorMessageFormat(cx, JS::ErrorMessageFormat::WebKit);
  CHECK(JS::GetErrorMessageFormat(cx) == JS::ErrorMessageFormat::WebKit);

  JS::RootedValue rval(cx);

  CHECK(ExpectThrowMessage(
      cx, evaluate("null.x", "test.js", 1, &rval),
      "null is not an object (evaluating 'null.x')"));

  CHECK(ExpectThrowMessage(
      cx, evaluate("(void 0)[\"width\"]", "test.js", 1, &rval),
      "undefined is not an object (evaluating '(void 0)[\"width\"]')"));

  CHECK(ExpectThrowMessage(
      cx, evaluate("({}).foo()", "test.js", 1, &rval),
      "({}).foo is not a function. (In '({}).foo()', '({}).foo' is undefined)"));

  CHECK(ExpectThrowMessage(
      cx, evaluate("var o = {foo: 1}; o.foo()", "test.js", 1, &rval),
      "o.foo is not a function. (In 'o.foo()', 'o.foo' is 1)"));

  CHECK(ExpectThrowMessage(cx, evaluate("new 1()", "test.js", 1, &rval),
                           "1 is not a constructor (evaluating '(new 1())')"));

  CHECK(ExpectThrowMessage(cx, evaluate("1 instanceof 1", "test.js", 1, &rval),
                           "Right hand side of instanceof is not an object"));

  CHECK(ExpectThrowMessage(cx, evaluate("'a' in 1", "test.js", 1, &rval),
                           "1 is not an Object. (evaluating '(\"a\" in 1)')"));

  JS::SetErrorMessageFormat(cx, JS::ErrorMessageFormat::SpiderMonkey);
  CHECK(JS::GetErrorMessageFormat(cx) == JS::ErrorMessageFormat::SpiderMonkey);

  return true;
}
END_TEST(testWebKitErrorMessages)
