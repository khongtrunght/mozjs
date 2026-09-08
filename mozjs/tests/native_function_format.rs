/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::ptr::{self, NonNull};

use mozjs::context::RawJSContext;

use mozjs::conversions::jsstr_to_string;
use mozjs::jsapi::{
    CallArgs, HandleObject, JSClass, JSClassOps, JSString, JS_NewStringCopyZ,
    NativeFunctionSourceFormat, ObjectOps, OnNewGlobalHookOption, Value,
};
use mozjs::jsval::UndefinedValue;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::{
    JS_DefineProperty3, JS_NewGlobalObject, JS_NewObject, SetNativeFunctionSourceFormat,
};
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
        for (name, class) in [
            (c"classCallable", &CLASS_CALLABLE),
            (c"bareCallable", &BARE_CALLABLE),
            (c"anonymousCallable", &ANONYMOUS_CALLABLE),
        ] {
            rooted!(&in(context) let object = JS_NewObject(context, class));
            assert!(!object.get().is_null());
            assert!(JS_DefineProperty3(
                context,
                global.handle(),
                name.as_ptr(),
                object.handle(),
                0,
            ));
        }
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
            var classProxy = new Proxy(classCallable, {});
            var classBound = Function.prototype.bind.call(classCallable, null);
            var revoked = Proxy.revocable(classCallable, {});
            revoked.revoke();
            var revokedClassProxy = revoked.proxy;
            Object.defineProperty(classCallable, 'name', {
                get() { throw new Error('toString must not read the name property'); }
            });
            var originalClassToSource = Function.prototype.toSource.call(classCallable);
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
            (
                "classCallable",
                "function Blob() {\n    [native code]\n}",
                "function Blob() { [native code] }",
            ),
            (
                "classProxy",
                "function () {\n    [native code]\n}",
                "function () { [native code] }",
            ),
            (
                "classBound",
                "function() {\n    [native code]\n}",
                "function () { [native code] }",
            ),
            (
                "revokedClassProxy",
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
                let script = format!("Function.prototype.toString.call({expression})");
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
            for (expression, v8) in [
                ("bareCallable", "function BareCallable() { [native code] }"),
                ("anonymousCallable", "function () { [native code] }"),
            ] {
                let script = format!(
                    "(() => {{ try {{ return Function.prototype.toString.call({expression}); }} catch (e) {{ if (e instanceof TypeError) return 'TypeError'; throw e; }} }})()"
                );
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
                    // Without an embedder funToString hook the default rejects
                    // class-callable objects; opting out must preserve that.
                    "TypeError"
                };
                assert_eq!(actual, expected, "{expression}, {mode:?}");
                println!("{mode:?}: {expression}: {actual:?}");
            }
            let script = "String(Object.getOwnPropertyDescriptor(Map.prototype, 'size').get)";
            let options =
                CompileOptionsWrapper::new(context, c"native_function_format.js".to_owned(), 1);
            evaluate_script(
                context,
                global.handle(),
                script,
                result.handle_mut(),
                options,
            )
            .unwrap();
            let actual = jsstr_to_string(context, NonNull::new(result.get().to_string()).unwrap());
            let expected = if mode == Some(NativeFunctionSourceFormat::V8) {
                "function get size() { [native code] }"
            } else {
                "function size() {\n    [native code]\n}"
            };
            assert_eq!(actual, expected);
            let unchanged = r#"
                realSource.toString() === originalSource &&
                Function.prototype.toSource.call(classCallable) === originalClassToSource &&
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

unsafe extern "C" fn class_call(_cx: *mut RawJSContext, argc: u32, vp: *mut Value) -> bool {
    let args = CallArgs::from_vp(vp, argc);
    let callee = *vp;
    args.rval().set(callee);
    true
}

// Model an embedder's legacy ObjectOps formatter, not a JSFunction.
unsafe extern "C" fn legacy_class_source(
    cx: *mut RawJSContext,
    _object: HandleObject,
    _is_to_source: bool,
) -> *mut JSString {
    JS_NewStringCopyZ(cx, c"function Blob() {\n    [native code]\n}".as_ptr())
}

static CLASS_OPS: JSClassOps = JSClassOps {
    addProperty: None,
    delProperty: None,
    enumerate: None,
    newEnumerate: None,
    resolve: None,
    mayResolve: None,
    finalize: None,
    call: Some(class_call),
    construct: Some(class_call),
    trace: None,
};

static OBJECT_OPS: ObjectOps = ObjectOps {
    lookupProperty: None,
    defineProperty: None,
    hasProperty: None,
    getProperty: None,
    setProperty: None,
    getOwnPropertyDescriptor: None,
    deleteProperty: None,
    getElements: None,
    funToString: Some(legacy_class_source),
};

static CLASS_CALLABLE: JSClass = JSClass {
    name: c"Blob".as_ptr(),
    flags: 0,
    cOps: &CLASS_OPS,
    spec: ptr::null(),
    ext: ptr::null(),
    oOps: &OBJECT_OPS,
};

static BARE_CALLABLE: JSClass = JSClass {
    name: c"BareCallable".as_ptr(),
    oOps: ptr::null(),
    ..CLASS_CALLABLE
};

static ANONYMOUS_CALLABLE: JSClass = JSClass {
    name: c"".as_ptr(),
    ..BARE_CALLABLE
};
