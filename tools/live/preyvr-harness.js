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

    // One point of the A2b IPD sweep: symmetric frusta, eye held rather than
    // tagged, one capture per eye.
    //
    // **Why one point per call.** The sweep needs each pair attributed to the IPD
    // that produced it, and every capture lands in the same directory with a name
    // that says only its frame index. Rather than guess the mapping afterwards
    // from timestamps, the driver calls this once per IPD and moves the files out
    // between calls. A step can then also be re-run on its own without redoing
    // the sweep.
    //
    // An `ipd` of 0 disarms, which is the control point: with symmetric frusta and
    // no eye offset the two "eyes" are literally the same camera, so the pair must
    // come back at the noise floor. That row is what fails if the difference being
    // measured is really temporal AA or a scene that is not as frozen as assumed.
    function runA2bStep(ipd, halfFov, settleSeconds) {
        var out = { ipd: ipd, halfFov: halfFov || 50.0 };
        var settle = settleSeconds || 0.4;

        // Symmetric: the whole point of the step is that the eye offset is the
        // only difference between the two images. Takes no lock, so the F-009
        // abort cannot poison anything if it fires.
        out.asymmetry = callTolerant('PreyVR_SetStereoAsymmetry', 'uint32', ['float'], [1.0]);

        if (ipd > 0) {
            out.arm = callTolerant('PreyVR_SetSyntheticStereo', 'uint32', ['float', 'float'],
                                   [ipd, out.halfFov]);
            out.armStatus = Number(callTolerant('PreyVR_GetCameraEditStatus', 'uint32', []).value);
            if (out.armStatus !== 2) {
                out.aborted = 'synthetic stereo did not arm';
                return out;
            }
        } else {
            callTolerant('PreyVR_SetSyntheticStereo', 'uint32', ['float', 'float'], [0.0, 0.0]);
            out.armStatus = Number(callTolerant('PreyVR_GetCameraEditStatus', 'uint32', []).value);
        }

        out.eyes = [];
        for (var eye = 0; eye <= 1; eye++) {
            var locked = act('PreyVR_SetStereoEyeLock', ptr(eye));
            // Settle before capturing: the camera hook runs on the game thread and
            // the capture on the render thread, so the lock needs to be older than
            // the pipeline depth before a capture can be attributed to it.
            Thread.sleep(settle);
            var observed = Number(callTolerant('PreyVR_GetLastRenderedEye', 'int', []).value);
            var shot = capture(eye);
            out.eyes.push({
                requested: eye,
                lockResult: locked.ok ? locked.returned : locked.error,
                // With the lock held every frame renders this eye, so unlike the
                // per-frame tagging this comparison is meaningful.
                observedBeforeCapture: observed,
                lockHeld: ipd > 0 ? observed === eye : true,
                capture: shot
            });
        }

        act('PreyVR_SetStereoEyeLock', ptr(2)); // back to alternating
        out.status = status();
        out.bothCaptured = out.eyes.length === 2 && out.eyes[0].capture.ok && out.eyes[1].capture.ok;
        out.locksHeld = out.eyes.every(function (e) { return e.lockHeld; });
        out.verdict = (Number(out.status.restoreFailures) === 0 && out.bothCaptured && out.locksHeld)
            ? 'step clean - move the two dumps out before the next IPD'
            : 'DO NOT TRUST: restore failed, a capture was lost, or the eye lock did not hold';
        return out;
    }

    // Puts the synthetic stereo back the way the rest of the protocol expects.
    function endA2b() {
        callTolerant('PreyVR_SetSyntheticStereo', 'uint32', ['float', 'float'], [0.0, 0.0]);
        callTolerant('PreyVR_SetStereoAsymmetry', 'uint32', ['float'], [1.1]);
        act('PreyVR_SetStereoEyeLock', ptr(2));
        return status();
    }

    // ---------------------------------------------------------------------
    // H-005 producer hunt (R-079). Two hardware breakpoints, no patched bytes.
    //
    // Step 1 opens the engine log gate and puts an *execute* watch on R-077's
    // LEA, whose r12 is a live IK target. Step 2 puts a *write* watch on that
    // target's position; the faulting rip is the producer, which is the whole
    // question H-005 is stuck on.
    //
    // Run step 1 with a weapon drawn and the player moving -- a stationary
    // player is what made R-078 report the targets as static, and the same
    // mistake is available here.
    // ---------------------------------------------------------------------

    function watchHealth() {
        return {
            armedMask: Number(callTolerant('PreyVR_GetWatchArmedMask', 'uint32', []).value),
            armedThreads: Number(callTolerant('PreyVR_GetWatchArmedThreads', 'uint32', []).value),
            missedThreads: Number(callTolerant('PreyVR_GetWatchMissedThreads', 'uint32', []).value),
            foreignTraps: String(callTolerant('PreyVR_GetWatchForeignTraps', 'uint64', []).value),
            captures: Number(callTolerant('PreyVR_GetWatchCaptureCount', 'uint32', []).value),
            gateOriginal: Number(callTolerant('PreyVR_GetIkLogGateOriginal', 'uint32', []).value),
            gateCurrent: Number(callTolerant('PreyVR_GetIkLogGateCurrent', 'uint32', []).value)
        };
    }

    // PreyVRIkTargetReadout: index, valid, then four u64, then hits and three
    // int millimetres, then quatMagnitude and readable. 64 bytes.
    function readIkRow(index) {
        var buf = Memory.alloc(64);
        buf.writeU32(index);
        act('PreyVR_ReadIkTargetPtr', buf);
        return {
            valid: buf.add(4).readU32() === 1,
            target: '0x' + buf.add(8).readU64().toString(16),
            limb: '0x' + buf.add(16).readU64().toString(16),
            owner: '0x' + buf.add(24).readU64().toString(16),
            skeleton: '0x' + buf.add(32).readU64().toString(16),
            hits: buf.add(40).readU32(),
            posMm: [buf.add(44).readS32(), buf.add(48).readS32(), buf.add(52).readS32()],
            quatMagnitude: buf.add(56).readU32(),
            readable: buf.add(60).readU32() === 1
        };
    }

    function runIkCapture(seconds) {
        var out = { step: 'ik-capture', seconds: seconds || 5 };
        var armed = act('PreyVR_ArmIkCapturePtr', ptr(64));
        out.arm = armed.ok ? armed.returned : armed.error;
        if (out.arm !== 0) {
            // Non-zero is a refusal with a reason, not a hiccup to retry: 3 is a
            // busy slot, 6 is the LEA bytes not matching this binary.
            out.verdict = 'REFUSED - see the log line for the reason; nothing was armed';
            out.health = watchHealth();
            return out;
        }
        out.armedHealth = watchHealth();
        Thread.sleep(out.seconds);
        out.hits = String(callTolerant('PreyVR_GetWatchHitCountPtr', 'uint64', ['pointer'], [ptr(0)]).value);
        act('PreyVR_DisarmIkCapture');
        out.health = watchHealth();

        out.rows = [];
        var count = Number(callTolerant('PreyVR_GetIkTargetRowCount', 'uint32', []).value);
        for (var i = 0; i < count; i++) {
            out.rows.push(readIkRow(i));
        }
        // The gate must read what it read before we touched it. A restore that
        // silently failed would leave the shipped game logging every frame.
        out.gateRestored = out.health.gateCurrent === out.health.gateOriginal;
        out.verdict = (Number(out.hits) > 0 && out.rows.length > 0 && out.gateRestored)
            ? 'targets captured - pick a row with quatMagnitude ~1000 and run runIkProducerHunt on its target'
            : (Number(out.hits) === 0
                ? 'NO HITS: the gate opened but the site never ran, or no skeleton was animating'
                : 'DO NOT TRUST: hits without rows, or the log gate was not restored');
        return out;
    }

    function runIkProducerHunt(targetAddress, seconds) {
        var out = { step: 'ik-producer', target: String(targetAddress), seconds: seconds || 8 };
        var armed = act('PreyVR_ArmIkProducerWatchPtr', ptr(targetAddress));
        out.arm = armed.ok ? armed.returned : armed.error;
        if (out.arm !== 0) {
            // 2 means the address was not 4-aligned, which never fires on x86 and
            // is refused rather than armed -- see R-079.
            out.verdict = 'REFUSED - nothing armed, so an empty result here would have been a lie';
            out.health = watchHealth();
            return out;
        }
        // Move now. A target the animator is not recomputing has no producer to
        // catch, which is exactly the window R-078 measured and misread.
        Thread.sleep(out.seconds);
        out.hits = String(callTolerant('PreyVR_GetWatchHitCountPtr', 'uint64', ['pointer'], [ptr(1)]).value);
        act('PreyVR_DisarmIkProducerWatch');
        out.health = watchHealth();
        out.ripIsAfterWrite =
            Number(callTolerant('PreyVR_GetIkProducerRipIsAfterWrite', 'uint32', []).value) === 1;

        out.producers = [];
        var count = Number(callTolerant('PreyVR_GetIkProducerCount', 'uint32', []).value);
        for (var i = 0; i < count; i++) {
            out.producers.push({
                rva: '0x' + callTolerant('PreyVR_GetIkProducerRvaPtr', 'uint64', ['pointer'], [ptr(i)]).value.toString(16),
                hits: Number(callTolerant('PreyVR_GetIkProducerHitsPtr', 'uint32', ['pointer'], [ptr(i)]).value)
            });
        }
        out.verdict = out.producers.length > 0
            ? 'PRODUCER FOUND - these RVAs write the target; rip is the instruction AFTER the store'
            : (Number(out.health.foreignTraps) > 0
                ? 'INCONCLUSIVE: foreign traps seen, so something else owns the debug registers (a debugger attached?)'
                : 'no writes seen - either the target address went stale, or nothing moved during the window');
        return out;
    }

    // The full bring-up, in the order the engine requires it.
    //
    // **Every step here goes through `act`, and that is not stylistic.** Calling
    // the float-taking `PreyVR_SetSyntheticStereo` directly from Frida aborts
    // (F-009) *while holding the camera-edit mutex*, which then refuses every
    // later control call with `detail=busy` and cannot be released without
    // restarting the game. Measured 2026-09-05, and it cost a headset session.
    // The `Ptr` variants exist precisely so the whole sequence can run on a real
    // thread; use them.
    // Paths here use FORWARD slashes deliberately. Windows accepts them and JS
    // never treats them as escapes. The backslash form was silently mangled --
    // the escape sequences were eaten, the manifest was never found, and it
    // surfaced as an unavailable SESSION rather than as a bad path, which sent
    // the first diagnosis at the headset instead of at a string.
    function startVr(opts) {
        var o = opts || {};
        var ipd = o.ipd || 0.064;          // wearer-chosen after a live sweep
        var halfFov = o.halfFov || 50.0;
        var manifest = o.manifest ||
            'C:/Program Files/Virtual Desktop Streamer/OpenXR/virtualdesktop-openxr.json';
        var out = { step: 'start-vr', ipd: ipd };

        // MinHook is initialised inside the observer's enable path and nowhere
        // else, so no other hook can install until this has run.
        out.observer = enableObserver();
        if (!out.observer.enabled) {
            out.verdict = 'ABORTED: frame observer did not enable, so no hook can install';
            return out;
        }
        // Both of these must precede StartXrSession. Prey inherits Steam's
        // environment, so the runtime has to be chosen rather than assumed.
        out.runtime = act('PreyVR_SetXrRuntimeManifest', Memory.allocUtf8String(manifest)).returned;
        out.srgb = act('PreyVR_SetXrPreferSrgbFormatPtr', ptr(1)).returned;
        out.session = act('PreyVR_StartXrSession', NULL, 25).returned;
        if (out.session !== 1) {
            out.verdict = 'ABORTED: session status ' + out.session +
                ' (2 = adapter mismatch, and that needs r_overrideDXGIAdapter set BEFORE launch)';
            return out;
        }
        // **Before arming stereo, so the first eye built already uses it.**
        // Without this the eye camera's projection is REPLACED with a synthetic
        // halfFov frustum carrying mirrored asymmetry, while the submission path
        // still declares Prey's own 120x88.5 -- so we declare one frustum over an
        // image rendered with another. In a headset that reads as wall-eyed
        // divergence plus a horizontal stretch, and it also makes the dynamic sun
        // swing with head yaw. Measured 2026-09-05; all three cleared the instant
        // this was set.
        //
        // It defaults to FALSE, and the run STEREO_ROUTE records as PASSED had it
        // on -- recorded in that section's heading rather than in its settings
        // table, which is exactly how rebuilding "the known-good config" from the
        // table missed it.
        // Cumulative for the session, so baseline it: a stale count from an
        // earlier arm would either raise a false alarm or mask a real one.
        var divergeBefore = Number(callTolerant('PreyVR_GetDeclaredFovDivergeCount', 'uint64', []).value);
        // `nativeProjection: false` deliberately arms the configuration that
        // shipped broken on 2026-09-05. It exists so the guard below can be
        // shown to FIRE -- a check that has only ever returned "fine" is
        // decoration, and this project has shipped that mistake before.
        var wantNative = (o.nativeProjection === false) ? 0 : 1;
        out.nativeProjection = act('PreyVR_SetNativeProjectionPtr', ptr(wantNative)).returned;
        out.nativeProjectionRequested = wantNative;

        var args = Memory.alloc(8);
        args.writeFloat(ipd); args.add(4).writeFloat(halfFov);
        // halfFov is inert while native projection is on: the eye is built by
        // translation alone and Prey's own projection is left untouched. It is
        // still passed so the call is valid if native projection is ever off.
        out.stereo = act('PreyVR_SetSyntheticStereoPtr', args).returned;
        out.cameraEdit = Number(callTolerant('PreyVR_GetCameraEditStatus', 'uint32', []).value);
        if (out.cameraEdit !== 2) {
            out.verdict = 'ABORTED: synthetic stereo did not arm (status ' + out.cameraEdit +
                '). If the log says detail=busy, the control mutex is held and only a restart clears it.';
            return out;
        }
        out.submission = act('PreyVR_SetXrStereoSubmissionPtr', ptr(1)).returned;
        // Motion blur is a requirement, not a preference: it read as a focus
        // artifact on near geometry. TAA 3 was chosen over the other three modes.
        out.motionBlur = console_('r_MotionBlur 0').lastResult;
        out.aa = console_('r_AntialiasingMode 3').lastResult;

        // **The viewmodel FOV, matched to the world's.** Prey draws near objects
        // through a separate pass with its own field (r_DrawNearFoV), tuned for a
        // flat screen -- so by default the weapon is rendered at a different
        // projection from the world it sits in, which reads in a headset as the
        // weapon belonging to a different photograph. Confirmed 2026-09-05: the
        // weapon changes and the world does not, which is what proves the cvar
        // reaches that pass.
        //
        // **Derived, not hardcoded.** The world FOV follows the player's own
        // slider, so a fixed 88.507 is only right for the machine it was measured
        // on. Read the live frustum and match it.
        //
        // It resets to Prey's default every launch, which is why this belongs in
        // the bring-up rather than in someone's memory.
        var fovBuf = Memory.alloc(16);
        try {
            new NativeFunction(module_().getExportByName('PreyVR_ReadDeclaredFovPtr'),
                               'uint32', ['pointer'])(fovBuf);
            // The export returns TANGENT half-extents, not angles.
            var tanUp = fovBuf.add(8).readFloat(), tanDown = fovBuf.add(12).readFloat();
            var verticalDeg = (Math.atan(Math.abs(tanUp)) + Math.atan(Math.abs(tanDown))) * 180.0 / Math.PI;
            if (verticalDeg > 20.0 && verticalDeg < 170.0) {
                out.nearFovDegrees = Math.round(verticalDeg * 1000) / 1000;
                out.nearFov = console_('r_DrawNearFoV ' + out.nearFovDegrees).lastResult;
            } else {
                out.nearFov = 'skipped - derived value ' + verticalDeg + ' is not plausible';
            }
        } catch (e) {
            out.nearFov = 'unavailable: ' + String(e.message || e);
        }
        Thread.sleep(2);
        out.frames = String(callTolerant('PreyVR_GetXrSubmittedFrameCount', 'uint64', []).value);

        // **The geometry check no external tool can make.** xr-tape sees what the
        // runtime located and what we declared, never what the engine rendered
        // with -- so its checks can all be in their correct state while the image
        // is a lie. A non-zero delta here means the declared frustum does not
        // describe these pixels, and the run is not worth a person's time.
        //
        // Proven able to fail before being trusted: with native projection off it
        // reads 140 diverged in 4 s at 732 milli-tan, and freezes the moment the
        // flag goes back on (2026-09-05).
        out.fovDiverged = Number(callTolerant('PreyVR_GetDeclaredFovDivergeCount', 'uint64', []).value) - divergeBefore;
        out.fovAgreed = String(callTolerant('PreyVR_GetDeclaredFovAgreeCount', 'uint64', []).value);
        // **Session high-water mark, not this run's.** Reported under a name that
        // says so, because a worst-case number printed beside a passing verdict
        // reads as this run's and is the exact shape of metric that has misled
        // this project before.
        out.fovWorstMilliTanSession = Number(callTolerant('PreyVR_GetDeclaredFovWorstMilliTan', 'uint32', []).value);
        if (out.fovDiverged > 0) {
            out.verdict = 'STOP - declared frustum does not match what was rendered (' +
                out.fovDiverged + ' frames, ' + out.fovWorstMilliTanSession +
                ' milli-tan session worst). Do not judge depth, scale or comfort through this; fix the declaration first.';
            return out;
        }
        if (out.fovAgreed === '0') {
            // Silence is not a pass. If nothing was compared, the check proved
            // nothing -- which is the failure shape this project keeps meeting.
            out.verdict = 'stereo running, but the frustum check never ran (no eye built yet). Re-check after a few seconds.';
            return out;
        }
        out.verdict = 'stereo running, declared frustum matches rendered - now runSixDof observe/rotation/position';
        return out;
    }

    // The native input path (H-006 / R-070). Read-only: resolves and validates,
    // calls nothing. Safe to run any time, including at a menu.
    function resolveInputPath() {
        var out = { step: 'input-path' };
        var r = act('PreyVR_ResolveInputPath');
        out.result = r.ok ? r.returned : r.error;
        if (out.result === 2) {
            out.verdict = 'pInput is null - the input system is not up yet. Load into a level and rerun.';
            return out;
        }
        if (out.result !== 0) {
            out.verdict = 'REFUSED - nothing resolved';
            return out;
        }
        out.pInput = '0x' + callTolerant('PreyVR_GetInputPathPointer', 'uint64', []).value.toString(16);
        out.vtable = '0x' + callTolerant('PreyVR_GetInputPathVtable', 'uint64', []).value.toString(16);
        out.alignmentConfirmed =
            Number(callTolerant('PreyVR_GetInputPathAlignmentConfirmed', 'uint32', []).value) === 1;
        out.alignmentSlot = Number(callTolerant('PreyVR_GetInputPathAlignmentSlot', 'uint32', []).value);
        out.postInputEventRva =
            '0x' + callTolerant('PreyVR_GetInputPathPostInputEventRva', 'uint64', []).value.toString(16);
        out.slots = [];
        for (var i = 0; i < 20; i++) {
            out.slots.push('0x' + callTolerant('PreyVR_GetInputPathSlotRvaPtr', 'uint64', ['pointer'], [ptr(i)]).value.toString(16));
        }
        // **The confirmed flag is the whole point.** Unconfirmed means the RVA is
        // the CryEngine-5 header guess, and calling an unknown virtual on a live
        // engine object is exactly what this probe exists to avoid.
        out.verdict = out.alignmentConfirmed
            ? 'CONFIRMED by the setter/getter pair at slot ' + out.alignmentSlot +
              ' - PostInputEvent is slot ' + (out.alignmentSlot + 2) + '. Check the RVA in Ghidra before calling it.'
            : 'UNCONFIRMED - the alignment pair was not found, so the RVA is the header guess. Read out.slots by hand; do not call through it.';
        return out;
    }

    // The 6DoF view seam. Rotation first, then position, because they fail
    // differently and a single arming would not say which half is wrong.
    function runSixDof(step, seconds) {
        var out = { step: 'sixdof:' + step, seconds: seconds || 10 };
        if (step === 'observe') {
            out.arm = act('PreyVR_SetViewHookObservingPtr', ptr(1)).returned;
            Thread.sleep(2);
            out.observed = String(callTolerant('PreyVR_GetViewHookObservedCount', 'uint64', []).value);
            out.verdict = Number(out.observed) > 0
                ? 'seam is live - proceed to rotation'
                : 'NOT RUNNING: UpdateView never fired, so nothing downstream can be trusted';
            return out;
        }
        if (step === 'rotation') {
            out.recenter = act('PreyVR_RecenterHeadTracking').returned;
            out.arm = act('PreyVR_SetViewHookApplyingPtr', ptr(1)).returned;
            Thread.sleep(out.seconds);
            out.applied = String(callTolerant('PreyVR_GetViewHookAppliedCount', 'uint64', []).value);
            out.verdict = Number(out.applied) > 0
                ? 'rotation applied - look around, then check whether CULLING follows the view now'
                : 'ARMED BUT INERT: applied stayed 0. Head pose or recenter reference is missing.';
            return out;
        }
        if (step === 'position') {
            out.arm = act('PreyVR_SetViewPositionApplyingPtr', ptr(1)).returned;
            Thread.sleep(out.seconds);
            out.applied = String(callTolerant('PreyVR_GetViewPositionApplied', 'uint64', []).value);
            out.refused = String(callTolerant('PreyVR_GetViewPositionRefused', 'uint64', []).value);
            out.offsetMm = Number(callTolerant('PreyVR_GetViewPositionOffsetMm', 'uint32', []).value);
            // An offset pinned near zero while applied climbs is the failure this
            // project keeps meeting: every counter green, nothing happening.
            out.verdict = (Number(out.applied) > 0 && out.offsetMm > 20)
                ? 'position is live and moving - lean and watch offsetMm track you'
                : (Number(out.applied) > 0
                    ? 'SUSPECT: applied is climbing but offsetMm is ~0. Either you held still, or head translation is not reaching the seam.'
                    : 'ARMED BUT INERT: nothing applied. Rotation must be armed first.');
            return out;
        }
        if (step === 'off') {
            act('PreyVR_SetViewPositionApplyingPtr', ptr(0));
            act('PreyVR_SetViewHookApplyingPtr', ptr(0));
            out.verdict = 'both lanes disarmed';
            return out;
        }
        out.verdict = "unknown step - use 'observe', 'rotation', 'position' or 'off'";
        return out;
    }

    // Which of the producer sites writes LAST.
    //
    // `runIkProducerHunt` folds by RVA, which answers "who writes this" and throws
    // away the only thing that answers "who writes it last" -- the order. The ring
    // already carries a monotonic sequence per trap, so this reads it back in order
    // instead of aggregating it.
    //
    // **The order is the evidence; the tally is not.** A site with the most hits is
    // not the owner -- a helper called three times per frame outranks the single
    // authoritative write. What matters is which RVA the repeating pattern ENDS on.
    function runIkProducerOrder(targetAddress, seconds) {
        var out = { step: 'ik-producer-order', target: String(targetAddress), seconds: seconds || 8 };
        var armed = act('PreyVR_ArmIkProducerWatchPtr', ptr(targetAddress));
        out.arm = armed.ok ? armed.returned : armed.error;
        if (out.arm !== 0) {
            out.verdict = 'REFUSED - nothing armed, so an empty result would have been a lie';
            return out;
        }
        Thread.sleep(out.seconds);
        act('PreyVR_DisarmIkProducerWatch');
        out.health = watchHealth();

        var total = Number(callTolerant('PreyVR_GetWatchCaptureCount', 'uint32', []).value);
        var rows = [];
        for (var i = 0; i < total; i++) {
            var slot = Number(callTolerant('PreyVR_GetWatchCaptureSlotPtr', 'uint32', ['pointer'], [ptr(i)]).value);
            if (slot !== 1) { continue; }   // 1 == the producer write watch
            rows.push({
                seq: Number(callTolerant('PreyVR_GetWatchCaptureSequencePtr', 'uint64', ['pointer'], [ptr(i)]).value),
                rva: '0x' + callTolerant('PreyVR_GetWatchCaptureRipRvaPtr', 'uint64', ['pointer'], [ptr(i)]).value.toString(16),
                caller: '0x' + callTolerant('PreyVR_GetWatchCaptureStackTopRvaPtr', 'uint64', ['pointer'], [ptr(i)]).value.toString(16),
                tid: Number(callTolerant('PreyVR_GetWatchCaptureThreadIdPtr', 'uint32', ['pointer'], [ptr(i)]).value)
            });
        }
        rows.sort(function (a, b) { return a.seq - b.seq; });
        out.ordered = rows;
        out.sequence = rows.map(function (r) { return r.rva; });
        out.threads = rows.map(function (r) { return r.tid; })
            .filter(function (v, i, a) { return a.indexOf(v) === i; });
        out.verdict = rows.length > 1
            ? 'read out.sequence as a cycle: the RVA the pattern ENDS on is the owner, not the one with most hits'
            : 'too few captures to order - lengthen the window or move so the animator recomputes';
        return out;
    }

    // The hand takeover. Arms the write watch so the site traps, then applies a
    // bounded offset immediately after PreyDll+0x87BC36 stores the position.
    //
    // **Why this works where six bump attempts did not.** Bumping raced the
    // animator and lost: 0x87BC36 (R-082) writes last in every cycle, so anything
    // written earlier is overwritten before use. Applying inside the trap for that
    // instruction lands AFTER its store retires, which is the one point in the
    // frame nothing overwrites.
    // `matchRva` selects which write site the offset is applied after. It is a
    // parameter and not a constant because there is more than one: measured
    // 2026-09-05, this target is written by TWO alternating code paths, and the
    // steady-state one ends on 0x87BBA0 while 0x87BC36 ends a much rarer variant.
    // Matching the wrong one applies nothing and looks identical to a broken hook.
    function runIkTakeover(targetAddress, dx, dy, dz, seconds, matchRva) {
        var out = { step: 'ik-takeover', quatTBase: String(targetAddress),
                    offset: [dx, dy, dz], seconds: seconds || 20 };
        // The watch must be armed first -- ArmApplyOffset refuses a slot that is
        // not trapping, because an override on a silent slot would report success
        // and never run.
        // **`targetAddress` is the QuatT BASE, not the position.** The DLL adds
        // the 0x10 position offset itself, so passing an already-offset address
        // watches base+0x20 -- which, at a 0x1C stride, is the NEXT entry's
        // rotation. Deriving both uses from the base here is what stops the watch
        // and the write from disagreeing about what they are pointed at.
        var base = ptr(targetAddress);
        var vec3 = base.add(0x10);
        var watch = act('PreyVR_ArmIkProducerWatchPtr', base);
        out.watch = watch.ok ? watch.returned : watch.error;
        if (out.watch !== 0) {
            out.verdict = 'REFUSED - the write watch did not arm, so nothing would trap';
            return out;
        }
        var args = Memory.alloc(40);
        // writePointer, not writeU64: Frida's writeU64 wants a UInt64 object and
        // rejects a NativePointer outright rather than coercing it.
        args.writePointer(vec3);                           // vec3 address (pos.x)
        args.add(8).writePointer(ptr(matchRva || 0x87BBA0));   // steady-state last writer
        args.add(16).writeFloat(dx);
        args.add(20).writeFloat(dy);
        args.add(24).writeFloat(dz);
        args.add(28).writeU32(out.seconds);
        args.add(32).writeU32(1);                          // producer slot
        var armed = act('PreyVR_ArmIkApplyOffsetPtr', args);
        out.arm = armed.ok ? armed.returned : armed.error;
        if (out.arm !== 0) {
            out.verdict = 'REFUSED code ' + out.arm +
                ' (2 unaligned/null, 3 offset > 10m, 4 bad duration, 5 slot not armed)';
            act('PreyVR_DisarmIkProducerWatch');
            return out;
        }
        // **Precondition: the animator must actually be running.**
        // A menu, a pause, or the game sitting in the background all freeze it,
        // and every one of those produces a confident zero that looks exactly
        // like "the override does not work". That mistake has now been made three
        // times in one session, so it is a refusal rather than a caution.
        Thread.sleep(1.0);
        var earlyTraps = Number(callTolerant('PreyVR_GetIkApplySkippedCount', 'uint64', []).value) +
                         Number(callTolerant('PreyVR_GetIkApplyAppliedCount', 'uint64', []).value);
        if (earlyTraps === 0) {
            act('PreyVR_DisarmIkApplyOffsetPtr', ptr(1));
            act('PreyVR_DisarmIkProducerWatch');
            out.verdict = 'REFUSED - the animator is not running (menu, paused, or the game is in ' +
                'the background). Nothing was measured, because a zero here would have been ' +
                'indistinguishable from the override failing.';
            out.animatorLive = false;
            return out;
        }
        out.animatorLive = true;
        Thread.sleep(2);
        out.applied = String(callTolerant('PreyVR_GetIkApplyAppliedCount', 'uint64', []).value);
        out.skipped = String(callTolerant('PreyVR_GetIkApplySkippedCount', 'uint64', []).value);
        out.health = watchHealth();
        // Applied climbing is the only evidence the override is running. Skipped
        // climbing while applied stays 0 means the match address never matched --
        // a different fault from not being armed, and worth saying so.
        out.verdict = Number(out.applied) > 0
            ? 'APPLYING - offset is landing after the final write; look now, it should HOLD rather than flicker'
            : (Number(out.skipped) > 0
                ? 'NOT MATCHING: traps happen but never at the match site - try the other path (0x87BC36 / 0x87BBA0)'
                : 'NO TRAPS at all - the watch is armed but the site is not being written');
        return out;
    }

    function stopIkTakeover() {
        act('PreyVR_DisarmIkApplyOffsetPtr', ptr(1));
        act('PreyVR_DisarmIkProducerWatch');
        return { stopped: true, note: 'the animator restores the pose on its own next frame' };
    }

    return {
        status: status,
        watchHealth: watchHealth,
        startVr: startVr,
        resolveInputPath: resolveInputPath,
        runSixDof: runSixDof,
        runIkCapture: runIkCapture,
        runIkProducerHunt: runIkProducerHunt,
        runIkProducerOrder: runIkProducerOrder,
        runIkTakeover: runIkTakeover,
        stopIkTakeover: stopIkTakeover,
        enableObserver: enableObserver,
        console: console_,
        capture: capture,
        freezeScene: freezeScene,
        thawScene: thawScene,
        runNoiseFloor: runNoiseFloor,
        runA1: runA1,
        runA2: runA2,
        runA2bStep: runA2bStep,
        endA2b: endA2b,
        runA3: runA3
    };
})();

JSON.stringify(PreyVR.status(), null, 1);
