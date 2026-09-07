/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "js/LocaleSensitive.h"
#include "js/Realm.h"
#include "js/RealmOptions.h"
#include "jsapi-tests/tests.h"

BEGIN_TEST(testDateToLocaleString) {
  JSRuntime* rt = JS_GetRuntime(cx);

  // This test should only attempt to run if we have Intl support: necessary
  // to properly assume that changes to the default locale will predictably
  // affect the behavior of the locale-sensitive Date methods tested here.
  JS::Rooted<JS::Value> haveIntl(cx);
  EVAL("typeof Intl !== 'undefined'", &haveIntl);
  if (!haveIntl.toBoolean()) {
    return true;
  }

  // Pervasive assumption: our Intl support includes "de" (German) and
  // "en" (English) and treats them differently for purposes of
  // Date.prototype.toLocale{,Date,Time}String behavior.

  // Start with German.
  CHECK(JS_SetDefaultLocale(rt, "de"));

  // The (constrained) Date object we'll use to test behavior.
  EXEC("var d = new Date(Date.UTC(2015, 9 - 1, 17));");

  // Test that toLocaleString behavior changes with default locale changes.
  EXEC("var deAll = d.toLocaleString();");

  CHECK(JS_SetDefaultLocale(rt, "en"));
  EXEC(
      "if (d.toLocaleString() === deAll) \n"
      "  throw 'toLocaleString results should have changed with system locale "
      "change';");

  // Test that toLocaleDateString behavior changes with default locale changes.
  EXEC("var enDate = d.toLocaleDateString();");

  CHECK(JS_SetDefaultLocale(rt, "de"));
  EXEC(
      "if (d.toLocaleDateString() === enDate) \n"
      "  throw 'toLocaleDateString results should have changed with system "
      "locale change';");

  // Test that toLocaleTimeString behavior changes with default locale changes.
  EXEC("var deTime = d.toLocaleTimeString();");

  CHECK(JS_SetDefaultLocale(rt, "en"));
  EXEC(
      "if (d.toLocaleTimeString() === deTime) \n"
      "  throw 'toLocaleTimeString results should have changed with system "
      "locale change';");

  JS_ResetDefaultLocale(rt);
  return true;
}
END_TEST(testDateToLocaleString)

BEGIN_TEST(testIntlRealmTimeZoneOverride) {
  JS::RootedValue result(cx);
  EVAL("typeof Intl !== 'undefined'", &result);
  if (!result.toBoolean()) {
    return true;
  }

  EXEC(
      "var systemZone = new Intl.DateTimeFormat().resolvedOptions().timeZone;");
  auto* realm = JS::GetCurrentRealmOrNull(cx);
  JS::SetRealmTimezoneOverride(realm, "Asia/Saigon");
  EVAL(
      "new Intl.DateTimeFormat().resolvedOptions().timeZone === 'Asia/Saigon' "
      "&& new Date('2025-01-01T00:00:00Z').getTimezoneOffset() === -420 "
      "&& new Intl.DateTimeFormat('en', {timeZone: 'Asia/Saigon'})"
      ".resolvedOptions().timeZone === 'Asia/Ho_Chi_Minh'",
      &result);
  CHECK(result.toBoolean());

  JS::SetRealmTimezoneOverride(realm, "Etc/UTC");
  EVAL(
      "new Intl.DateTimeFormat().resolvedOptions().timeZone === 'Etc/UTC' "
      "&& new Date('2025-01-01T00:00:00Z').getTimezoneOffset() === 0",
      &result);
  CHECK(result.toBoolean());

  JS::SetRealmTimezoneOverride(realm, nullptr);
  EVAL("new Intl.DateTimeFormat().resolvedOptions().timeZone === systemZone",
       &result);
  CHECK(result.toBoolean());
  return true;
}
END_TEST(testIntlRealmTimeZoneOverride)
