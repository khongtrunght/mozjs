/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr::{self, NonNull};

use mozjs::capture_stack;
use mozjs::conversions::jsstr_to_string;
use mozjs::jsapi::{CallArgs, JSContext, OnNewGlobalHookOption, StackFormat, Value};
use mozjs::jsval::UndefinedValue;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{JS_DefineFunction, JS_NewGlobalObject};
use mozjs::rust::wrappers2::{RunJobs, SetStackFormat, UseInternalJobQueues};
use mozjs::rust::{
    evaluate_script, CompileOptionsWrapper, JSEngine, RealmOptions, SIMPLE_GLOBAL_CLASS,
};

#[test]
fn capture_stack() {
    unsafe extern "C" fn print_stack(context: *mut JSContext, argc: u32, vp: *mut Value) -> bool {
        let mut context = mozjs::context::JSContext::from_ptr(NonNull::new(context).unwrap());
        let cx = &mut context;
        let args = CallArgs::from_vp(vp, argc);

        capture_stack!(&in(cx) let stack);
        let mut stack = stack.unwrap();
        let spider_monkey_stack = stack.as_string(None, StackFormat::SpiderMonkey).unwrap();
        println!("{}", spider_monkey_stack);
        assert_eq!(
            "bar@test.js:3:21\nfoo@test.js:5:17\n@test.js:8:16\n",
            spider_monkey_stack
        );

        let webkit_stack = stack.as_string(None, StackFormat::WebKit).unwrap();
        assert_eq!(
            "bar@test.js:3:21\nfoo@test.js:5:17\nglobal code@test.js:8:16",
            webkit_stack
        );

        args.rval().set(UndefinedValue());
        true
    }

    // Internal job queues must be installed before self-hosting, earlier than
    // Runtime::new permits. Keep this test's low-level context RAII-owned.
    struct TestContext(NonNull<JSContext>);
    impl Drop for TestContext {
        fn drop(&mut self) {
            unsafe { mozjs::jsapi::JS_DestroyContext(self.0.as_ptr()) };
        }
    }
    let _engine = JSEngine::init().unwrap();
    let owned_context = TestContext(
        NonNull::new(unsafe { mozjs::jsapi::JS_NewContext(32 * 1024 * 1024, ptr::null_mut()) })
            .unwrap(),
    );
    let mut context = unsafe { mozjs::context::JSContext::from_ptr(owned_context.0) };
    let context = &mut context;
    unsafe {
        assert!(UseInternalJobQueues(context));
        mozjs::jsapi::JS_SetNativeStackQuota(context.raw_cx(), 1024 * 1024, 0, 0);
        assert!(mozjs::jsapi::InitSelfHostedCode(
            context.raw_cx(),
            Default::default(),
            None
        ));
    }
    #[cfg(feature = "debugmozjs")]
    unsafe {
        mozjs::jsapi::SetGCZeal(context.raw_cx(), 2, 1);
    }
    let h_option = OnNewGlobalHookOption::FireOnNewGlobalHook;
    let c_option = RealmOptions::default();

    unsafe {
        rooted!(&in(context) let global = JS_NewGlobalObject(
            context,
            &SIMPLE_GLOBAL_CLASS,
            ptr::null_mut(),
            h_option,
            &*c_option,
        ));
        let mut realm = AutoRealm::new_from_handle(context, global.handle());
        let context = &mut realm;

        let function = JS_DefineFunction(
            context,
            global.handle().into(),
            c"print_stack".as_ptr(),
            Some(print_stack),
            0,
            0,
        );
        assert!(!function.is_null());

        let javascript = "
            function foo(arg1) {
                var bar = function() {
                    print_stack();
                };
                bar();
            }

            foo(\"arg1-value\");
        ";
        rooted!(&in(context) let mut rval = UndefinedValue());
        let options = CompileOptionsWrapper::new(&context, c"test.js".to_owned(), 0);
        assert!(evaluate_script(
            context,
            global.handle(),
            javascript,
            rval.handle_mut(),
            options
        )
        .is_ok());

        SetStackFormat(context, StackFormat::V8);
        let cases = [
            (
                "constructor",
                "function C() { this.stack = new Error().stack; }\nnew C().stack",
                "    at new C (v8_frames.js:1:29)",
            ),
            (
                "anonymous_constructor",
                "new (function() { this.stack = new Error().stack; })().stack",
                "    at new <anonymous> (v8_frames.js:1:32)",
            ),
            (
                "iife",
                "var stack = (function() { return new Error().stack; })(); stack",
                "    at v8_frames.js:1:34",
            ),
            (
                "variable",
                "var f = function() { return new Error().stack; };\nf()",
                "    at f (v8_frames.js:1:29)",
            ),
            (
                "property",
                "var o = {}; o.m = function() { return new Error().stack; };\no.m()",
                "    at o.m (v8_frames.js:1:39)",
            ),
            (
                "nested_property",
                "function outer() { var o = {}; o.m = function() { return new Error().stack; }; return o.m(); } outer()",
                "    at o.m (v8_frames.js:1:58)",
            ),
            (
                "returned_anonymous",
                "var f = (function() { return function() { return new Error().stack; }; })(); f()",
                "    at v8_frames.js:1:50",
            ),
            (
                "plain_constructor_call",
                "function C() { return new Error().stack; } C()",
                "    at C (v8_frames.js:1:23)",
            ),
            // Receiver-derived "Type." prefixes are captured eagerly and
            // mirror v8's method-call output.
            (
                "class_method",
                "{ class C { m() { return new Error().stack; } } new C().m(); }",
                "    at C.m (v8_frames.js:1:26)",
            ),
            (
                "object_method",
                "var o = { m() { return new Error().stack; } };\no.m()",
                "    at Object.m (v8_frames.js:1:24)",
            ),
            (
                "async_direct",
                "var stack; async function fn() { stack = new Error().stack; } fn(); stack",
                "    at fn (v8_frames.js:1:42)",
            ),
        ];
        for eager in [false, true] {
            for (label, script, expected) in cases {
                let options = CompileOptionsWrapper::new(context, c"v8_frames.js".to_owned(), 1);
                if eager {
                    (*options.ptr)._base.eagerDelazificationStrategy_ =
                        mozjs::jsapi::DelazificationOption::ParseEverythingEagerly;
                }
                evaluate_script(context, global.handle(), script, rval.handle_mut(), options)
                    .unwrap();
                let stack = jsstr_to_string(context, NonNull::new(rval.get().to_string()).unwrap());
                let frame = stack.lines().nth(1).unwrap();
                println!("V8 {label} (eager={eager}): {frame:?}");
                assert_eq!(frame, expected, "{label}, eager={eager}");
            }
        }

        let options = CompileOptionsWrapper::new(context, c"v8_async.js".to_owned(), 1);
        evaluate_script(context, global.handle(),
            "async function leaf() { await 0; return new Error().stack; }\nasync function fn() { return await leaf(); }\nvar asyncStack; fn().then(s => { asyncStack = s; });",
            rval.handle_mut(), options).unwrap();
        RunJobs(context);
        let options = CompileOptionsWrapper::new(context, c"read_async.js".to_owned(), 1);
        evaluate_script(
            context,
            global.handle(),
            "asyncStack",
            rval.handle_mut(),
            options,
        )
        .unwrap();
        let stack = jsstr_to_string(context, NonNull::new(rval.get().to_string()).unwrap());
        println!("V8 async continuation: {stack:?}");
        // SavedFrame records the call-site column and additional async ancestry,
        // not V8's reconstructed await position. Assert the async naming contract.
        assert!(stack
            .lines()
            .any(|line| line.starts_with("    at async fn (v8_async.js:2:")));

        // JavaScriptCore names a frame only by the function's own or
        // spec-inferred name, and prints its URL stripped for a report: no
        // credentials, query or fragment.
        SetStackFormat(context, StackFormat::WebKit);
        let options = CompileOptionsWrapper::new(
            context,
            c"https://user:pw@example.com/a.js?v=1#frag".to_owned(),
            1,
        );
        evaluate_script(
            context,
            global.handle(),
            "var self = {}; self.f = function() { return new Error().stack; };\n\
             var o = {func: function() { return self.f(); }};\no.func()",
            rval.handle_mut(),
            options,
        )
        .unwrap();
        let stack = jsstr_to_string(context, NonNull::new(rval.get().to_string()).unwrap());
        println!("WebKit: {stack:?}");
        let frames: Vec<&str> = stack.lines().collect();
        assert_eq!(frames.len(), 3, "{stack}");
        assert!(frames[0].starts_with("@https://example.com/a.js:1:"), "{stack}");
        assert!(frames[1].starts_with("func@https://example.com/a.js:2:"), "{stack}");
        assert!(frames[2].starts_with("global code@https://example.com/a.js:3:"), "{stack}");
    }
}
