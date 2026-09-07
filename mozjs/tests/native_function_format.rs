/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr::{self, NonNull};

use mozjs::conversions::jsstr_to_string;
use mozjs::jsapi::{NativeFunctionSourceFormat, OnNewGlobalHookOption};
use mozjs::jsval::UndefinedValue;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{JS_NewGlobalObject, SetNativeFunctionSourceFormat};
use mozjs::rust::{
    evaluate_script, CompileOptionsWrapper, JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS,
};

#[test]
fn native_function_format() {
    let engine = JSEngine::init().unwrap();
    let mut runtime = Runtime::new(engine.handle());
    let context = runtime.cx();
    let mut realm_options = RealmOptions::default();
    realm_options.creationOptions_.toSource_ = true;

    unsafe {
        rooted!(&in(context) let global = JS_NewGlobalObject(
            context,
            &SIMPLE_GLOBAL_CLASS,
            ptr::null_mut(),
            OnNewGlobalHookOption::FireOnNewGlobalHook,
            &*realm_options,
        ));
        let mut realm = AutoRealm::new_from_handle(context, global.handle());
        let context = &mut realm;
        rooted!(&in(context) let mut result = UndefinedValue());
        // Do not warm the accessor names with name/toSource/toString first.
        SetNativeFunctionSourceFormat(context, NativeFunctionSourceFormat::V8);
        for (expression, expected) in [
            (
                "Object.getOwnPropertyDescriptor(Map.prototype, 'size').get",
                "function get size() { [native code] }",
            ),
            (
                "Object.getOwnPropertyDescriptor(Object.prototype, '__proto__').set",
                "function set __proto__() { [native code] }",
            ),
        ] {
            let script = format!("var accessor = {expression}; var first = accessor.toString(); accessor.name; first + '|' + accessor.toString()");
            let options =
                CompileOptionsWrapper::new(context, c"native_function_format.js".to_owned(), 1);
            evaluate_script(
                context,
                global.handle(),
                &script,
                result.handle_mut(),
                options,
            )
            .unwrap();
            let actual = jsstr_to_string(context, NonNull::new(result.get().to_string()).unwrap());
            assert_eq!(actual, format!("{expected}|{expected}"));
            println!("V8 first-call accessor: {actual:?}");
        }
        SetNativeFunctionSourceFormat(context, NativeFunctionSourceFormat::SpiderMonkey);
        let setup = r#"
            var bound = (function named() {}).bind(null);
            var getter = Object.getOwnPropertyDescriptor(Map.prototype, 'size').get;
            var setter = Object.getOwnPropertyDescriptor(Object.prototype, '__proto__').set;
            var asyncNext = Object.getPrototypeOf(Object.getPrototypeOf((async function*(){})())).next;
            var proxy = new Proxy(eval, {});
            var realSource = function realSource(a) { return a + 1; };
            var originalSource = realSource.toString();
            var originalToSource = [eval, bound, getter, setter, proxy, realSource]
                .map(f => f.toSource()).join('|');
        "#;
        let options =
            CompileOptionsWrapper::new(context, c"native_function_format.js".to_owned(), 1);
        evaluate_script(
            context,
            global.handle(),
            setup,
            result.handle_mut(),
            options,
        )
        .unwrap();

        let cases = [
            (
                "eval",
                "function eval() {\n    [native code]\n}",
                "function eval() { [native code] }",
            ),
            (
                "bound",
                "function() {\n    [native code]\n}",
                "function () { [native code] }",
            ),
            (
                "getter",
                "function size() {\n    [native code]\n}",
                "function get size() { [native code] }",
            ),
            (
                "setter",
                "function __proto__() {\n    [native code]\n}",
                "function set __proto__() { [native code] }",
            ),
            (
                "asyncNext",
                "function next() {\n    [native code]\n}",
                "function next() { [native code] }",
            ),
            (
                "proxy",
                "function () {\n    [native code]\n}",
                "function () { [native code] }",
            ),
        ];

        // First exercise the untouched default, then opt in, then switch back.
        for mode in [
            None,
            Some(NativeFunctionSourceFormat::V8),
            Some(NativeFunctionSourceFormat::SpiderMonkey),
        ] {
            if let Some(mode) = mode {
                SetNativeFunctionSourceFormat(context, mode);
            }
            for (expression, spider_monkey, v8) in cases {
                let script = format!("({expression}).toString()");
                let options =
                    CompileOptionsWrapper::new(context, c"native_function_format.js".to_owned(), 1);
                evaluate_script(
                    context,
                    global.handle(),
                    &script,
                    result.handle_mut(),
                    options,
                )
                .unwrap();
                let actual =
                    jsstr_to_string(context, NonNull::new(result.get().to_string()).unwrap());
                let expected = if mode == Some(NativeFunctionSourceFormat::V8) {
                    v8
                } else {
                    spider_monkey
                };
                assert_eq!(actual, expected, "{expression}, {mode:?}");
                println!("{mode:?}: {expression}: {actual:?}");
            }
            let unchanged = r#"
                realSource.toString() === originalSource &&
                [eval, bound, getter, setter, proxy, realSource].map(f => f.toSource()).join('|') === originalToSource
            "#;
            let options =
                CompileOptionsWrapper::new(context, c"native_function_format.js".to_owned(), 1);
            evaluate_script(
                context,
                global.handle(),
                unchanged,
                result.handle_mut(),
                options,
            )
            .unwrap();
            assert!(
                result.get().to_boolean(),
                "source text and toSource must be unchanged"
            );
        }

        #[cfg(feature = "jit")]
        {
            SetNativeFunctionSourceFormat(context, NativeFunctionSourceFormat::V8);
            let wasm = r#"
                new WebAssembly.Instance(new WebAssembly.Module(new Uint8Array([
                    0,97,115,109,1,0,0,0,1,4,1,96,0,0,3,2,1,0,
                    7,5,1,1,102,0,0,10,4,1,2,0,11
                ]))).exports.f.toString()
            "#;
            let options =
                CompileOptionsWrapper::new(context, c"native_function_format.js".to_owned(), 1);
            evaluate_script(context, global.handle(), wasm, result.handle_mut(), options).unwrap();
            let actual = jsstr_to_string(context, NonNull::new(result.get().to_string()).unwrap());
            assert_eq!(actual, "function 0() { [native code] }");
            println!("V8: wasm: {actual:?}");
        }
    }
}
