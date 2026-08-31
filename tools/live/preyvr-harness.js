// PreyVR live test harness, for Frida.
//
// Paste into a Frida session attached to Prey, then call the run* functions.
// Everything returns a plain object so the result can be read directly rather
// than scraped out of a log.
//
// Why this exists: the A1/A2/A3 protocols are sequences of export calls with
// waits and checks between them, and doing that by hand is both slow and the
// kind of thing where a missed check turns a null result into a false positive.
// The harness makes a protocol run reproducible and its acceptance criteria
// non-optional.
//
// Two behaviours are worked around deliberately:
//
//   * **F-009 is not what it was recorded as.** It was attributed to Frida's
//     exception handler reacting to MinHook writing into PreyDll's .text. That
//     is wrong: exports touching no hook at all fail identically. What actually
//     happens is that Frida's NativeFunction aborts the call with a bare "system
//     error", and if the abort lands while our code holds a lock, that lock is
//     never released -- poisoning every later control call. So anything that
//     *does work* is invoked through `act`, on a real Windows thread, where Frida
//     marshals nothing and the abort cannot happen. Getters stay on
//     `callTolerant`; they take no locks, so an abort there costs nothing.
//
//   * The observer's disable path has never been observed restoring the target
//     prologue on a live host, so nothing here depends on disabling it.

'use strict';

var PreyVR = (function () {
    var mod = null;
    var fn = {};

    function module_() {
        if (mod === null) {
            mod = Process.getModuleByName('PreyVR.dll');
        }
        return mod;
    }

    function bind(name, ret, args) {
        if (!(name in fn)) {
            fn[name] = new NativeFunction(module_().getExportByName(name), ret, args);
        }
        return fn[name];
    }

    // Calls through and reports whether the call itself threw, without letting a
    // throw abort the sequence -- see F-009.
    //
    // **Use this only for getters.** For anything that does work, use `act`.
    function callTolerant(name, ret, args, argv) {
        try {
            return { threw: false, value: bind(name, ret, args).apply(null, argv || []) };
        } catch (e) {
            return { threw: true, error: String(e.message || e) };
        }
    }

    // Runs an export on a **real Windows thread** instead of through Frida.
    //
    // This is the fix for F-009, and it is not cosmetic. Diagnosed live on
    // 2026-08-31: Frida's NativeFunction aborts these calls with a bare "system
    // error", and when the abort lands while our code holds a lock, that lock is
    // never released -- so every later control call reports busy and the whole
    // session is poisoned. The symptom that F-009 recorded as "raises an error
    // but takes effect anyway" is that abort, and the damage it leaves behind is
    // what made the next call look like a different bug.
    //
    // The control exports all match LPTHREAD_START_ROUTINE -- one pointer in, a
    // DWORD out -- so a thread can call them directly with Frida marshalling
    // nothing. The thread's exit code IS the return value.
    var k32 = Process.getModuleByName('kernel32.dll');
    var CreateThread = new NativeFunction(k32.getExportByName('CreateThread'),
        'pointer', ['pointer', 'size_t', 'pointer', 'pointer', 'uint32', 'pointer']);
    var GetExitCodeThread = new NativeFunction(k32.getExportByName('GetExitCodeThread'),
        'int', ['pointer', 'pointer']);
    var CloseHandle = new NativeFunction(k32.getExportByName('CloseHandle'), 'int', ['pointer']);
    var STILL_ACTIVE = 259;

    function act(name, param, timeoutSeconds) {
        var handle = CreateThread(NULL, 0, module_().getExportByName(name),
                                  param || NULL, 0, NULL);
        if (handle.isNull()) {
            return { ok: false, error: 'CreateThread failed for ' + name };
        }
        // Polled in short sleeps rather than WaitForSingleObject, because a long
        // blocking wait inside a Frida call trips its RPC timeout and loses the
        // result even when the call itself succeeded.
        var code = Memory.alloc(4);
        var waited = 0;
        var limit = timeoutSeconds || 5;
        do {
            Thread.sleep(0.15);
            waited += 0.15;
            GetExitCodeThread(handle, code);
        } while (code.readU32() === STILL_ACTIVE && waited < limit);

        var value = code.readU32();
        var running = value === STILL_ACTIVE;
        CloseHandle(handle);
        return running
            ? { ok: false, name: name, error: 'still running after ' + limit + 's' }
            : { ok: true, name: name, returned: value };
    }

    function status() {
        return {
            smoke: callTolerant('PreyVR_GetSmokeStatus', 'uint32', []).value,
            observer: callTolerant('PreyVR_GetFrameObserverStatus', 'uint32', []).value,
            frames: callTolerant('PreyVR_GetObservedFrameCount', 'uint64', []).value,
            captures: callTolerant('PreyVR_GetCompletedFrameCaptureCount', 'uint32', []).value,
            captureResult: callTolerant('PreyVR_GetLastFrameCaptureResult', 'uint32', []).value,
            cameraEdit: callTolerant('PreyVR_GetCameraEditStatus', 'uint32', []).value,
            applied: callTolerant('PreyVR_GetCameraEditAppliedCount', 'uint64', []).value,
            restoreFailures: callTolerant('PreyVR_GetCameraEditRestoreFailureCount', 'uint64', []).value,
            lastEye: callTolerant('PreyVR_GetLastRenderedEye', 'int', []).value,
            doubleRendered: callTolerant('PreyVR_GetDoubleRenderedFrameCount', 'uint64', []).value,
            consoleResult: callTolerant('PreyVR_GetLastConsoleResult', 'uint32', []).value,
            consoleSubmitted: callTolerant('PreyVR_GetSubmittedConsoleCommandCount', 'uint64', []).value
        };
    }

    function enableObserver() {
        // The return value is unreliable here (F-009), so the *status* is the
        // answer. 2 == enabled.
        act('PreyVR_SetFrameObserverEnabled', ptr(1));
        Thread.sleep(0.1);
        var s = Number(callTolerant('PreyVR_GetFrameObserverStatus', 'uint32', []).value);
        return { observerStatus: s, enabled: s === 2 };
    }

    function console_(command) {
        var text = Memory.allocUtf8String(command);
        var r = act('PreyVR_QueueConsoleCommand', text);
        Thread.sleep(0.15); // let the observer service the queue and the engine drain it
        return {
            command: command,
            queueResult: r.ok ? r.returned : r.error,
            lastResult: Number(callTolerant('PreyVR_GetLastConsoleResult', 'uint32', []).value),
            submitted: String(callTolerant('PreyVR_GetSubmittedConsoleCommandCount', 'uint64', []).value)
        };
    }

    // Arms a capture and waits for the completed count to actually move, rather
    // than sleeping a fixed time and hoping. A capture that never lands must be
    // reported, not silently treated as one that did.
    function capture(tag, timeoutSeconds) {
        var before = Number(callTolerant('PreyVR_GetCompletedFrameCaptureCount', 'uint32', []).value);
        var armed = act('PreyVR_RequestFrameCapture', ptr(tag));

        var waited = 0.0;
        var step = 0.05;
        var limit = timeoutSeconds || 3.0;
        while (waited < limit) {
            Thread.sleep(step);
            waited += step;
            var now = Number(callTolerant('PreyVR_GetCompletedFrameCaptureCount', 'uint32', []).value);
            if (now > before) {
                return {
                    ok: true,
                    tag: tag,
                    completed: now,
                    eye: Number(callTolerant('PreyVR_GetLastRenderedEye', 'int', []).value),
                    waitedSeconds: waited
                };
            }
        }
        return {
            ok: false,
            tag: tag,
            reason: 'capture did not complete within ' + limit + 's',
            lastResult: Number(callTolerant('PreyVR_GetLastFrameCaptureResult', 'uint32', []).value)
        };
    }

    // Freezes the scene. Frame comparison is meaningless without this: temporal
    // AA and motion blur make even a static scene differ frame to frame.
    function freezeScene() {
        return [
            console_('t_Scale 0'),
            console_('r_AntialiasingMode 0'),
            console_('r_MotionBlur 0')
        ];
    }

    function thawScene() {
        return [
            console_('t_Scale 1'),
            console_('r_AntialiasingMode 3'),
            console_('r_MotionBlur 2')
        ];
    }

    // --- protocols ---------------------------------------------------------

    // Step 0 of A1. Two captures with nothing armed; the diff between them is the
    // noise floor, and without it the A1 result has nothing to be compared to.
    function runNoiseFloor() {
        return { a: capture(0), b: capture(0), status: status() };
    }

    // A1: does writing m_ViewCamera change the image?
    function runA1(yawDegrees) {
        var yaw = yawDegrees || 10.0;
        var out = { yawDegrees: yaw };
        out.before = capture(0);
        // Float args cannot go through the thread route, so this one still uses
        // NativeFunction -- it takes no lock, so an abort cannot poison anything.
        out.arm = callTolerant('PreyVR_SetCameraYawEdit', 'uint32', ['float'], [yaw]);
        out.armStatus = Number(callTolerant('PreyVR_GetCameraEditStatus', 'uint32', []).value);
        if (out.armStatus !== 2) {
            out.aborted = 'camera edit did not arm';
            return out;
        }
        out.after = capture(1);
        callTolerant('PreyVR_SetCameraYawEdit', 'uint32', ['float'], [0.0]);
        out.status = status();
        // The acceptance criterion that must never be skipped.
        out.restoreFailures = String(out.status.restoreFailures);
        out.applied = String(out.status.applied);
        out.verdict = (Number(out.status.restoreFailures) === 0 && Number(out.status.applied) > 0)
            ? 'ran cleanly - now diff the two dumps'
            : 'DO NOT TRUST: restore failed or the edit never applied';
        return out;
    }

    // A2: alternating-eye stereo. Captures until it has one dump of each eye,
    // rather than assuming two consecutive captures land on different eyes.
    function runA2(ipd, halfFov) {
        var out = { ipd: ipd || 0.064, halfFov: halfFov || 50.0 };
        out.arm = callTolerant('PreyVR_SetSyntheticStereo', 'uint32', ['float', 'float'],
                               [out.ipd, out.halfFov]);
        out.armStatus = Number(callTolerant('PreyVR_GetCameraEditStatus', 'uint32', []).value);
        if (out.armStatus !== 2) {
            out.aborted = 'synthetic stereo did not arm';
            return out;
        }

        out.captures = [];
        var seen = {};
        for (var i = 0; i < 6 && Object.keys(seen).length < 2; i++) {
            var c = capture(0);
            out.captures.push(c);
            if (c.ok && (c.eye === 0 || c.eye === 1)) {
                seen[c.eye] = true;
            }
        }
        callTolerant('PreyVR_SetSyntheticStereo', 'uint32', ['float', 'float'], [0.0, 0.0]);
        out.status = status();
        out.bothEyesCaptured = Object.keys(seen).length === 2;
        out.verdict = (Number(out.status.restoreFailures) === 0 && out.bothEyesCaptured)
            ? 'got a pair - now run New-StereoView -Mode Anaglyph'
            : 'DO NOT TRUST: restore failed or only one eye was captured';
        return out;
    }

    // A3: the double render. Defaults to a budget of ONE frame -- the first
    // question is only whether the engine survives it.
    function runA3(frameBudget, ipd, halfFov) {
        var budget = frameBudget || 1;
        var out = { frameBudget: budget };
        var before = Number(callTolerant('PreyVR_GetDoubleRenderedFrameCount', 'uint64', []).value);

        out.arm = callTolerant('PreyVR_SetDoubleRenderStereo', 'uint32',
                               ['float', 'float', 'uint32'],
                               [ipd || 0.064, halfFov || 50.0, budget]);
        out.armStatus = Number(callTolerant('PreyVR_GetCameraEditStatus', 'uint32', []).value);
        if (out.armStatus !== 2) {
            out.aborted = 'double render did not arm';
            return out;
        }

        Thread.sleep(Math.min(5.0, 0.5 + budget / 100.0));
        callTolerant('PreyVR_SetDoubleRenderStereo', 'uint32', ['float', 'float', 'uint32'],
                     [0.0, 0.0, 0]);

        out.status = status();
        out.doubleRenderedDelta = Number(out.status.doubleRendered) - before;
        out.verdict = (Number(out.status.restoreFailures) === 0 && out.doubleRenderedDelta > 0)
            ? 'the engine rendered twice in a frame and survived'
            : (out.doubleRenderedDelta === 0
                ? 'no double-rendered frames completed - check the log for detail=double_render_'
                : 'DO NOT TRUST: restore failed');
        return out;
    }

    return {
        status: status,
        enableObserver: enableObserver,
        console: console_,
        capture: capture,
        freezeScene: freezeScene,
        thawScene: thawScene,
        runNoiseFloor: runNoiseFloor,
        runA1: runA1,
        runA2: runA2,
        runA3: runA3
    };
})();

JSON.stringify(PreyVR.status(), null, 1);
