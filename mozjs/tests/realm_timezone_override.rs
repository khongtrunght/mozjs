/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#![cfg(feature = "intl")]

use std::ptr::{self, NonNull};

use mozjs::conversions::jsstr_to_string;
use mozjs::jsapi::{GetCurrentRealmOrNull, OnNewGlobalHookOption, SetRealmTimezoneOverride};
use mozjs::jsval::UndefinedValue;
use mozjs::realm::AutoRealm;
use mozjs::rooted;
use mozjs::rust::wrappers2::JS_NewGlobalObject;
use mozjs::rust::{
    evaluate_script, CompileOptionsWrapper, JSEngine, RealmOptions, Runtime, SIMPLE_GLOBAL_CLASS,
};

#[test]
fn realm_timezone_override_preserves_intl_identifier() {
    let engine = JSEngine::init().unwrap();
    let mut runtime = Runtime::new(engine.handle());
    let context = runtime.cx();
    let realm_options = RealmOptions::default();

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
        let raw_realm = GetCurrentRealmOrNull(context.raw_cx());
        rooted!(&in(context) let mut result = UndefinedValue());
        let options =
            CompileOptionsWrapper::new(context, c"realm_timezone_override.js".to_owned(), 1);
        evaluate_script(
            context,
            global.handle(),
            "var systemZone = new Intl.DateTimeFormat().resolvedOptions().timeZone;",
            result.handle_mut(),
            options,
        )
        .unwrap();

        for (zone, expected) in [
            (c"Asia/Saigon", "Asia/Saigon|-420|Asia/Ho_Chi_Minh"),
            (c"Etc/UTC", "Etc/UTC|0|Asia/Ho_Chi_Minh"),
            (c"Asia/Saigon", "Asia/Saigon|-420|Asia/Ho_Chi_Minh"),
        ] {
            SetRealmTimezoneOverride(raw_realm, zone.as_ptr());
            let options =
                CompileOptionsWrapper::new(context, c"realm_timezone_override.js".to_owned(), 1);
            evaluate_script(
                context,
                global.handle(),
                r#"[
                    new Intl.DateTimeFormat().resolvedOptions().timeZone,
                    new Date('2025-01-01T00:00:00Z').getTimezoneOffset(),
                    new Intl.DateTimeFormat('en', {timeZone: 'Asia/Saigon'}).resolvedOptions().timeZone
                ].join('|')"#,
                result.handle_mut(),
                options,
            ).unwrap();
            let actual = jsstr_to_string(context, NonNull::new(result.get().to_string()).unwrap());
            assert_eq!(actual, expected);
            println!("realm override: {actual}");
        }

        SetRealmTimezoneOverride(raw_realm, ptr::null());
        let options =
            CompileOptionsWrapper::new(context, c"realm_timezone_override.js".to_owned(), 1);
        evaluate_script(
            context,
            global.handle(),
            "new Intl.DateTimeFormat().resolvedOptions().timeZone === systemZone",
            result.handle_mut(),
            options,
        )
        .unwrap();
        assert!(result.get().to_boolean());
    }
}
