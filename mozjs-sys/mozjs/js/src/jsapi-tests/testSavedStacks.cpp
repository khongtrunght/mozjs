/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Utf8.h"  // mozilla::Utf8Unit

#include "builtin/TestingFunctions.h"
#include "js/ColumnNumber.h"  // JS::LimitedColumnNumberOneOrigin, JS::TaggedColumnNumberOneOrigin
#include "js/CompilationAndEvaluation.h"  // JS::Evaluate
#include "js/Exception.h"
#include "js/SavedFrameAPI.h"
#include "js/SourceText.h"  // JS::Source{Ownership,Text}
#include "js/Stack.h"
#include "jsapi-tests/tests.h"
#include "util/Text.h"
#include "vm/ArrayObject.h"
#include "vm/Realm.h"
#include "vm/SavedStacks.h"

BEGIN_TEST(testSavedStacks_withNoStack) {
  JS::Realm* realm = cx->realm();
  realm->setAllocationMetadataBuilder(&js::SavedStacks::metadataBuilder);
  JS::RootedObject obj(cx, js::NewDenseEmptyArray(cx));
  realm->setAllocationMetadataBuilder(nullptr);
  return true;
}
END_TEST(testSavedStacks_withNoStack)

BEGIN_TEST(testSavedStacks_ApiDefaultValues) {
  JS::Rooted<js::SavedFrame*> savedFrame(cx, nullptr);

  JSPrincipals* principals = cx->realm()->principals();

  // Source
  JS::RootedString str(cx);
  JS::SavedFrameResult result =
      JS::GetSavedFrameSource(cx, principals, savedFrame, &str);
  CHECK(result == JS::SavedFrameResult::AccessDenied);
  CHECK(str.get() == cx->runtime()->emptyString);

  // Line
  uint32_t line = 123;
  result = JS::GetSavedFrameLine(cx, principals, savedFrame, &line);
  CHECK(result == JS::SavedFrameResult::AccessDenied);
  CHECK(line == 0);

  // Column
  JS::TaggedColumnNumberOneOrigin column(JS::LimitedColumnNumberOneOrigin(123));
  result = JS::GetSavedFrameColumn(cx, principals, savedFrame, &column);
  CHECK(result == JS::SavedFrameResult::AccessDenied);
  CHECK(column == JS::TaggedColumnNumberOneOrigin());

  // Function display name
  result =
      JS::GetSavedFrameFunctionDisplayName(cx, principals, savedFrame, &str);
  CHECK(result == JS::SavedFrameResult::AccessDenied);
  CHECK(str.get() == nullptr);

  // Parent
  JS::RootedObject parent(cx);
  result = JS::GetSavedFrameParent(cx, principals, savedFrame, &parent);
  CHECK(result == JS::SavedFrameResult::AccessDenied);
  CHECK(parent.get() == nullptr);

  // Stack string
  CHECK(JS::BuildStackString(cx, principals, savedFrame, &str));
  CHECK(str.get() == cx->runtime()->emptyString);

  return true;
}
END_TEST(testSavedStacks_ApiDefaultValues)

BEGIN_TEST(testSavedStacks_RangeBasedForLoops) {
  CHECK(js::DefineTestingFunctions(cx, global, false, false));

  JS::RootedValue val(cx);
  CHECK(
      evaluate("(function one() {                      \n"   // 1
               "  return (function two() {             \n"   // 2
               "    return (function three() {         \n"   // 3
               "      return saveStack();              \n"   // 4
               "    }());                              \n"   // 5
               "  }());                                \n"   // 6
               "}());                                  \n",  // 7
               "filename.js", 1, &val));

  CHECK(val.isObject());
  JS::RootedObject obj(cx, &val.toObject());

  CHECK(obj->is<js::SavedFrame>());
  JS::Rooted<js::SavedFrame*> savedFrame(cx, &obj->as<js::SavedFrame>());

  JS::Rooted<js::SavedFrame*> rf(cx, savedFrame);
  for (JS::Handle<js::SavedFrame*> frame :
       js::SavedFrame::RootedRange(cx, rf)) {
    JS_GC(cx);
    CHECK(frame == rf);
    rf = rf->getParent();
  }
  CHECK(rf == nullptr);

  // Stack string
  static const char SpiderMonkeyStack[] =
      "three@filename.js:4:14\n"
      "two@filename.js:5:6\n"
      "one@filename.js:6:4\n"
      "@filename.js:7:2\n";
  static const char V8Stack[] =
      "    at three (filename.js:4:14)\n"
      "    at two (filename.js:5:6)\n"
      "    at one (filename.js:6:4)\n"
      "    at filename.js:7:2";
  static const char WebKitStack[] =
      "three@filename.js:4:14\n"
      "two@filename.js:5:6\n"
      "one@filename.js:6:4\n"
      "global code@filename.js:7:2";
  struct {
    js::StackFormat format;
    const char* expected;
  } expectations[] = {{js::StackFormat::Default, SpiderMonkeyStack},
                      {js::StackFormat::SpiderMonkey, SpiderMonkeyStack},
                      {js::StackFormat::V8, V8Stack},
                      {js::StackFormat::WebKit, WebKitStack}};
  auto CheckStacks = [&]() {
    for (auto& expectation : expectations) {
      JS::RootedString str(cx);
      JSPrincipals* principals = cx->realm()->principals();
      CHECK(JS::BuildStackString(cx, principals, savedFrame, &str, 0,
                                 expectation.format));
      JSLinearString* lin = str->ensureLinear(cx);
      CHECK(lin);
      CHECK(js::StringEqualsAscii(lin, expectation.expected));
    }
    return true;
  };

  CHECK(CheckStacks());

  js::SetStackFormat(cx, js::StackFormat::V8);
  expectations[0].expected = V8Stack;

  CHECK(CheckStacks());

  return true;
}
END_TEST(testSavedStacks_RangeBasedForLoops)

BEGIN_TEST(testSavedStacks_ErrorStackSpiderMonkey) {
  JS::RootedValue val(cx);
  CHECK(
      evaluate("(function one() {                      \n"   // 1
               "  return (function two() {             \n"   // 2
               "    return (function three() {         \n"   // 3
               "      return new Error('foo');         \n"   // 4
               "    }());                              \n"   // 5
               "  }());                                \n"   // 6
               "}()).stack                             \n",  // 7
               "filename.js", 1, &val));

  CHECK(val.isString());
  JS::RootedString stack(cx, val.toString());

  // Stack string
  static const char SpiderMonkeyStack[] =
      "three@filename.js:4:14\n"
      "two@filename.js:5:6\n"
      "one@filename.js:6:4\n"
      "@filename.js:7:2\n";
  JSLinearString* lin = stack->ensureLinear(cx);
  CHECK(lin);
  CHECK(js::StringEqualsLiteral(lin, SpiderMonkeyStack));

  return true;
}
END_TEST(testSavedStacks_ErrorStackSpiderMonkey)

BEGIN_TEST(testSavedStacks_ErrorStackV8) {
  js::SetStackFormat(cx, js::StackFormat::V8);

  JS::RootedValue val(cx);
  CHECK(
      evaluate("(function one() {                      \n"   // 1
               "  return (function two() {             \n"   // 2
               "    return (function three() {         \n"   // 3
               "      return new Error('foo');         \n"   // 4
               "    }());                              \n"   // 5
               "  }());                                \n"   // 6
               "}()).stack                             \n",  // 7
               "filename.js", 1, &val));

  CHECK(val.isString());
  JS::RootedString stack(cx, val.toString());

  // Stack string
  static const char V8Stack[] =
      "Error: foo\n"
      "    at three (filename.js:4:14)\n"
      "    at two (filename.js:5:6)\n"
      "    at one (filename.js:6:4)\n"
      "    at filename.js:7:2";
  JSLinearString* lin = stack->ensureLinear(cx);
  CHECK(lin);
  CHECK(js::StringEqualsLiteral(lin, V8Stack));

  return true;
}
END_TEST(testSavedStacks_ErrorStackV8)

BEGIN_TEST(testSavedStacks_ErrorStackWebKit) {
  js::SetStackFormat(cx, js::StackFormat::WebKit);

  JS::RootedValue val(cx);
  CHECK(
      evaluate("(function one() {                      \n"   // 1
               "  return (function two() {             \n"   // 2
               "    return (function three() {         \n"   // 3
               "      return new Error('foo');         \n"   // 4
               "    }());                              \n"   // 5
               "  }());                                \n"   // 6
               "}()).stack                             \n",  // 7
               "filename.js", 1, &val));

  CHECK(val.isString());
  JS::RootedString stack(cx, val.toString());

  static const char WebKitStack[] =
      "three@filename.js:4:14\n"
      "two@filename.js:5:6\n"
      "one@filename.js:6:4\n"
      "global code@filename.js:7:2";
  JSLinearString* lin = stack->ensureLinear(cx);
  CHECK(lin);
  CHECK(js::StringEqualsLiteral(lin, WebKitStack));

  return true;
}
END_TEST(testSavedStacks_ErrorStackWebKit)

BEGIN_TEST(testSavedStacks_ErrorStackWebKitNames) {
  js::SetStackFormat(cx, js::StackFormat::WebKit);

  JS::RootedValue val(cx);
  // A var-assigned function keeps its ECMAScript-inferred name; an anonymous
  // callback prints an empty name (a bare '@'); the top-level frame prints
  // "global code".
  CHECK(
      evaluate("var inferred = function () {          \n"  // 1
               "  return timer(function () {          \n"  // 2
               "    return new Error('foo');          \n"  // 3
               "  });                                 \n"  // 4
               "};                                    \n"  // 5
               "function timer(fn) { return fn(); }   \n"  // 6
               "inferred();                           \n"  // 7
               , "filename.js", 1, &val));

  CHECK(val.isString());
  JS::RootedString stack(cx, val.toString());

  static const char WebKitStack[] =
      "@filename.js:3:12\n"
      "timer@filename.js:6:29\n"
      "inferred@filename.js:2:11\n"
      "global code@filename.js:7:1";
  JSLinearString* lin = stack->ensureLinear(cx);
  CHECK(lin);
  CHECK(js::StringEqualsLiteral(lin, WebKitStack));

  return true;
}
END_TEST(testSavedStacks_ErrorStackWebKitNames)

BEGIN_TEST(testSavedStacks_ErrorStackWebKitEval) {
  js::SetStackFormat(cx, js::StackFormat::WebKit);

  JS::RootedValue val(cx);
  // The top-level frame of a direct eval prints "eval code".
  CHECK(evaluate("eval(\"new Error('foo').stack\")", "filename.js", 1, &val));

  CHECK(val.isString());
  JS::RootedString stack(cx, val.toString());
  JSLinearString* lin = stack->ensureLinear(cx);
  CHECK(lin);

  JS::AutoCheckCannotGC nogc;
  const char* chars =
      reinterpret_cast<const char*>(JS_GetLinearStringChars(lin));
  CHECK(strstr(chars, "eval code@filename.js:1:"));

  return true;
}
END_TEST(testSavedStacks_ErrorStackWebKitEval)

BEGIN_TEST(testSavedStacks_selfHostedFrames) {
  CHECK(js::DefineTestingFunctions(cx, global, false, false));

  JS::RootedValue val(cx);
  //             0         1         2         3
  //             0123456789012345678901234567890123456789
  CHECK(
      evaluate("(function one() {                      \n"   // 1
               "  try {                                \n"   // 2
               "    [1].map(function two() {           \n"   // 3
               "      throw saveStack();               \n"   // 4
               "    });                                \n"   // 5
               "  } catch (stack) {                    \n"   // 6
               "    return stack;                      \n"   // 7
               "  }                                    \n"   // 8
               "}())                                   \n",  // 9
               "filename.js", 1, &val));

  CHECK(val.isObject());
  JS::RootedObject obj(cx, &val.toObject());

  CHECK(obj->is<js::SavedFrame>());
  JS::Rooted<js::SavedFrame*> savedFrame(cx, &obj->as<js::SavedFrame>());

  JS::Rooted<js::SavedFrame*> selfHostedFrame(cx, savedFrame->getParent());
  CHECK(selfHostedFrame->isSelfHosted(cx));

  JSPrincipals* principals = cx->realm()->principals();

  // Source
  JS::RootedString str(cx);
  JS::SavedFrameResult result = JS::GetSavedFrameSource(
      cx, principals, selfHostedFrame, &str, JS::SavedFrameSelfHosted::Exclude);
  CHECK(result == JS::SavedFrameResult::Ok);
  JSLinearString* lin = str->ensureLinear(cx);
  CHECK(lin);
  CHECK(js::StringEqualsLiteral(lin, "filename.js"));

  // Source, including self-hosted frames
  result = JS::GetSavedFrameSource(cx, principals, selfHostedFrame, &str,
                                   JS::SavedFrameSelfHosted::Include);
  CHECK(result == JS::SavedFrameResult::Ok);
  lin = str->ensureLinear(cx);
  CHECK(lin);
  CHECK(js::StringEqualsLiteral(lin, "self-hosted"));

  // Line
  uint32_t line = 123;
  result = JS::GetSavedFrameLine(cx, principals, selfHostedFrame, &line,
                                 JS::SavedFrameSelfHosted::Exclude);
  CHECK(result == JS::SavedFrameResult::Ok);
  CHECK_EQUAL(line, 3U);

  // Column
  JS::TaggedColumnNumberOneOrigin column(JS::LimitedColumnNumberOneOrigin(123));
  result = JS::GetSavedFrameColumn(cx, principals, selfHostedFrame, &column,
                                   JS::SavedFrameSelfHosted::Exclude);
  CHECK(result == JS::SavedFrameResult::Ok);
  CHECK_EQUAL(column.oneOriginValue(), 9U);

  // Function display name
  result = JS::GetSavedFrameFunctionDisplayName(
      cx, principals, selfHostedFrame, &str, JS::SavedFrameSelfHosted::Exclude);
  CHECK(result == JS::SavedFrameResult::Ok);
  lin = str->ensureLinear(cx);
  CHECK(lin);
  CHECK(js::StringEqualsLiteral(lin, "one"));

  // Parent
  JS::RootedObject parent(cx);
  result = JS::GetSavedFrameParent(cx, principals, savedFrame, &parent,
                                   JS::SavedFrameSelfHosted::Exclude);
  CHECK(result == JS::SavedFrameResult::Ok);
  // JS::GetSavedFrameParent does this super funky and potentially unexpected
  // thing where it doesn't return the next subsumed parent but any next
  // parent. This so that callers can still get the "asyncParent" property
  // which is only on the first frame of the async parent stack and that frame
  // might not be subsumed by the caller. It is expected that callers will
  // still interact with the frame through the JSAPI accessors, so this should
  // be safe and should not leak privileged info to unprivileged
  // callers. However, because of that, we don't test that the parent we get
  // here is the selfHostedFrame's parent (because, as just explained, it
  // isn't) and instead check that asking for the source property gives us the
  // expected value.
  result = JS::GetSavedFrameSource(cx, principals, parent, &str,
                                   JS::SavedFrameSelfHosted::Exclude);
  CHECK(result == JS::SavedFrameResult::Ok);
  lin = str->ensureLinear(cx);
  CHECK(lin);
  CHECK(js::StringEqualsLiteral(lin, "filename.js"));

  return true;
}
END_TEST(testSavedStacks_selfHostedFrames)

BEGIN_TEST(test_GetPendingExceptionStack) {
  CHECK(js::DefineTestingFunctions(cx, global, false, false));

  JSPrincipals* principals = cx->realm()->principals();

  static const char sourceText[] =
      //          1         2         3
      // 123456789012345678901234567890123456789
      "(function one() {                      \n"   // 1
      "  (function two() {                    \n"   // 2
      "    (function three() {                \n"   // 3
      "      throw 5;                         \n"   // 4
      "    }());                              \n"   // 5
      "  }());                                \n"   // 6
      "}())                                   \n";  // 7

  JS::CompileOptions opts(cx);
  opts.setFileAndLine("filename.js", 1U);

  JS::SourceText<mozilla::Utf8Unit> srcBuf;
  CHECK(srcBuf.init(cx, sourceText, js_strlen(sourceText),
                    JS::SourceOwnership::Borrowed));

  JS::RootedValue val(cx);
  bool ok = JS::Evaluate(cx, opts, srcBuf, &val);

  CHECK(!ok);
  CHECK(JS_IsExceptionPending(cx));
  CHECK(val.isUndefined());

  JS::ExceptionStack exnStack(cx);
  CHECK(JS::GetPendingExceptionStack(cx, &exnStack));
  CHECK(exnStack.stack());
  CHECK(exnStack.stack()->is<js::SavedFrame>());
  JS::Rooted<js::SavedFrame*> savedFrameStack(
      cx, &exnStack.stack()->as<js::SavedFrame>());

  CHECK(exnStack.exception().isInt32());
  CHECK(exnStack.exception().toInt32() == 5);

  struct {
    uint32_t line;
    uint32_t column;
    const char* source;
    const char* functionDisplayName;
  } expected[] = {{4, 7, "filename.js", "three"},
                  {5, 6, "filename.js", "two"},
                  {6, 4, "filename.js", "one"},
                  {7, 2, "filename.js", nullptr}};

  size_t i = 0;
  for (JS::Handle<js::SavedFrame*> frame :
       js::SavedFrame::RootedRange(cx, savedFrameStack)) {
    CHECK(i < 4);

    // Line
    uint32_t line = 123;
    JS::SavedFrameResult result = JS::GetSavedFrameLine(
        cx, principals, frame, &line, JS::SavedFrameSelfHosted::Exclude);
    CHECK(result == JS::SavedFrameResult::Ok);
    CHECK_EQUAL(line, expected[i].line);

    // Column
    JS::TaggedColumnNumberOneOrigin column(
        JS::LimitedColumnNumberOneOrigin(123));
    result = JS::GetSavedFrameColumn(cx, principals, frame, &column,
                                     JS::SavedFrameSelfHosted::Exclude);
    CHECK(result == JS::SavedFrameResult::Ok);
    CHECK_EQUAL(column.oneOriginValue(), expected[i].column);

    // Source
    JS::RootedString str(cx);
    result = JS::GetSavedFrameSource(cx, principals, frame, &str,
                                     JS::SavedFrameSelfHosted::Exclude);
    CHECK(result == JS::SavedFrameResult::Ok);
    JSLinearString* linear = str->ensureLinear(cx);
    CHECK(linear);
    CHECK(js::StringEqualsAscii(linear, expected[i].source));

    // Function display name
    result = JS::GetSavedFrameFunctionDisplayName(
        cx, principals, frame, &str, JS::SavedFrameSelfHosted::Exclude);
    CHECK(result == JS::SavedFrameResult::Ok);
    if (auto expectedName = expected[i].functionDisplayName) {
      CHECK(str);
      linear = str->ensureLinear(cx);
      CHECK(linear);
      CHECK(js::StringEqualsAscii(linear, expectedName));
    } else {
      CHECK(!str);
    }

    i++;
  }

  return true;
}
END_TEST(test_GetPendingExceptionStack)

BEGIN_TEST(testSavedStacks_V8FrameIdentity) {
  js::SetStackFormat(cx, js::StackFormat::V8);
  JS::RootedValue value(cx);
  struct {
    const char* source;
    const char* expected;
  } cases[] = {
      {"function C() { this.stack = new Error().stack; }\nnew C().stack",
       "    at new C (v8_frames.js:1:29)"},
      {"new (function() { this.stack = new Error().stack; })().stack",
       "    at new <anonymous> (v8_frames.js:1:32)"},
      {"var stack = (function() { return new Error().stack; })(); stack",
       "    at v8_frames.js:1:34"},
      {"var f = function() { return new Error().stack; };\nf()",
       "    at f (v8_frames.js:1:29)"},
      {"var o = {}; o.m = function() { return new Error().stack; };\no.m()",
       "    at o.m (v8_frames.js:1:39)"},
      {"function outer() { var o = {}; o.m = function() { return new Error().stack; }; return o.m(); } outer()",
       "    at o.m (v8_frames.js:1:58)"},
      {"var f = (function() { return function() { return new Error().stack; }; })(); f()",
       "    at v8_frames.js:1:50"},
      {"var stack; async function fn() { stack = new Error().stack; } fn(); stack",
       "    at fn (v8_frames.js:1:42)"},
  };
  for (const auto& test : cases) {
    CHECK(evaluate(test.source, "v8_frames.js", 1, &value));
    JS::RootedString stack(cx, value.toString());
    JSLinearString* linear = stack->ensureLinear(cx);
    CHECK(linear);
    // Compare the first frame independently of the embedding's caller stack.
    JS::UniqueChars bytes = JS_EncodeStringToUTF8(cx, stack);
    CHECK(bytes);
    const char* first = strchr(bytes.get(), '\n');
    CHECK(first);
    CHECK(strncmp(first + 1, test.expected, strlen(test.expected)) == 0);
    CHECK(first[1 + strlen(test.expected)] == '\n' ||
          first[1 + strlen(test.expected)] == '\0');

    JS::CompileOptions options(cx);
    options.setFileAndLine("v8_frames.js", 1).setForceFullParse();
    JS::SourceText<mozilla::Utf8Unit> source;
    CHECK(source.init(cx, test.source, strlen(test.source),
                      JS::SourceOwnership::Borrowed));
    JS::RootedValue eager(cx);
    CHECK(JS::Evaluate(cx, options, source, &eager));
    JS::RootedString eagerStack(cx, eager.toString());
    JS::UniqueChars eagerBytes = JS_EncodeStringToUTF8(cx, eagerStack);
    CHECK(eagerBytes);
    const char* eagerFirst = strchr(eagerBytes.get(), '\n');
    CHECK(eagerFirst);
    CHECK(strncmp(eagerFirst + 1, test.expected, strlen(test.expected)) == 0);
    CHECK(eagerFirst[1 + strlen(test.expected)] == '\n' ||
          eagerFirst[1 + strlen(test.expected)] == '\0');
  }

  CHECK(evaluate(
      "async function leaf() { await 0; return new Error().stack; }\n"
      "async function fn() { return await leaf(); }\n"
      "var asyncStack; fn().then(s => { asyncStack = s; });",
      "v8_async.js", 1, &value));
  js::RunJobs(cx);
  EVAL("asyncStack.split('\\n').some(line => "
       "line.startsWith('    at async fn (v8_async.js:2:'))", &value);
  CHECK(value.toBoolean());
  CHECK(js::DefineTestingFunctions(cx, global, false, false));
  EVAL(
      "function CloneCtor() { this.error = new Error(); }"
      "var original = new CloneCtor().error;"
      "deserialize(serialize(original)).stack === original.stack",
      &value);
  CHECK(value.toBoolean());
  return true;
}
END_TEST(testSavedStacks_V8FrameIdentity)
