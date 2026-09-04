# WebKit stack-format fork: build and verification plan

## Repository state

- Upstream: `https://github.com/servo/mozjs`
- Fork: `https://github.com/khongtrunght/mozjs`
- Working branch: `webkit-stack-format`
- SpiderMonkey snapshot: ESR 153 (`mozjs_sys` `153.0.0-0`)

The branch adds `js::StackFormat::WebKit`. It formats ordinary saved frames like JavaScriptCore:

```text
function@source:line:column
global code@source:line:column
```

Unlike SpiderMonkey format, the final frame has no trailing newline. Unlike V8 format, there is no `Error: message` prefix.

The implementation is based on:

- WebKit `Source/JavaScriptCore/runtime/StackFrame.cpp`, `StackFrame::toString`
- WebKit `Source/JavaScriptCore/interpreter/Interpreter.cpp`, `Interpreter::stackTraceAsString`
- WebKit `Source/JavaScriptCore/runtime/ErrorInstance.cpp`, `ErrorInstance::computeErrorInfo`

## Known fidelity boundary

SpiderMonkey `SavedFrame` does not retain all JavaScriptCore frame metadata. The formatter cannot yet exactly distinguish every JSC `eval code`, `module code`, or native `[native code]` frame. It uses `global code` for an unnamed bottom frame and matches ordinary named-function/global-script formatting. Real Safari oracle captures are still required before claiming complete Safari equivalence.

## Remote-machine source build

Use a machine with ample CPU, RAM, and at least 20 GB of free disk space.

```bash
git clone --branch webkit-stack-format \
  git@github.com:khongtrunght/mozjs.git
cd mozjs

# Required: compile this fork's bundled SpiderMonkey instead of downloading
# Servo's stock prebuilt archive.
MOZJS_FROM_SOURCE=1 cargo test -p mozjs --test capture_stack
```

For a full optimized build and archive:

```bash
MOZJS_CREATE_ARCHIVE=1 cargo build --release
find target -name 'libmozjs-*.tar.gz' -print
```

`MOZJS_CREATE_ARCHIVE=1` implies a source build and produces the static-library archive consumed by `mozjs_sys`.

Optional full Rust test suite:

```bash
MOZJS_FROM_SOURCE=1 cargo test --tests --examples
```

## GitHub Actions prebuilt build

The existing workflow already sets `MOZJS_CREATE_ARCHIVE=1` and uploads prebuilt archives for Linux, macOS, Windows, Android, OpenHarmony, and WASI.

Trigger it manually without pushing to `main`:

```bash
gh workflow run main.yml \
  --repo khongtrunght/mozjs \
  --ref webkit-stack-format

# Watch runs.
gh run list --repo khongtrunght/mozjs \
  --workflow main.yml \
  --branch webkit-stack-format

gh run watch RUN_ID --repo khongtrunght/mozjs

# Download successful artifacts.
gh run download RUN_ID \
  --repo khongtrunght/mozjs \
  --dir ./mozjs-artifacts
```

Do not merge directly to `main` yet. The inherited `publish.yml` runs on pushes to `main` and includes a crates.io publishing job intended for the upstream Servo repository.

## Consume a downloaded prebuilt archive

For an exact local artifact path:

```bash
MOZJS_ARCHIVE=/absolute/path/to/libmozjs-TARGET.tar.gz \
  cargo build
```

To host reusable archives, create a GitHub release named for the current `mozjs_sys` version and upload the CI archives:

```bash
gh release create mozjs-sys-v153.0.0-0 \
  ./mozjs-artifacts/**/*.tar.gz \
  --repo khongtrunght/mozjs \
  --title 'mozjs_sys 153.0.0-0 with WebKit stack format'
```

Consumers can then use:

```bash
MOZJS_ARCHIVE=https://github.com/khongtrunght/mozjs/releases \
  cargo build
```

Leave `MOZJS_ATTESTATION` disabled for fork artifacts for now: the current attestation verification code hard-codes `servo/mozjs` as the repository.

## Servo integration after the archive passes

Point Servo at the fork or a local checkout and select the format after each runtime is created:

```rust
use js::jsapi::{SetStackFormat, StackFormat};

let cx = runtime.cx();
unsafe {
    SetStackFormat(&cx, StackFormat::WebKit);
}
```

The insertion point is `components/script/runtime/script_runtime.rs`, immediately after `let cx = runtime.cx();` in `Runtime::new_with_parent()`. This path covers the main script runtime and child worker runtimes.

## Verification checklist

1. `cargo fmt --all -- --check` passes.
2. `MOZJS_FROM_SOURCE=1 cargo test -p mozjs --test capture_stack` passes.
3. GitHub Actions creates the expected target archives.
4. Build Servo using one of the fork archives.
5. Confirm `new Error("foo").stack` has no error-message prefix and no final newline.
6. Compare named, anonymous, global, eval, module, async, native-host, and WebAssembly frames with a genuine Safari/WebKit oracle.
7. Run the PX auditor stack probes in Servo and genuine Safari and compare the serialized values byte-for-byte.
