<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { MyObject } from './generated/MyObject';
  import { TestObject } from './generated/TestObject';
  import bildUrl from './assets/bild.jpg';

  // State
  let obj: MyObject | null = null;
  let logs: string[] = [];
  let errorModalOpen = false;
  let errorModalMessage = '';

  // Store subscriptions
  $: aBool = obj?.aBool;
  $: strProp = obj?.strProp;
  $: counter = obj?.counter;
  $: numbers = obj?.numbers;
  $: status = obj?.status;
  $: pod = obj?.pod;

  function log(msg: string, type: 'info' | 'success' | 'error' = 'info') {
    const timestamp = new Date().toLocaleTimeString();
    const prefix = type === 'error' ? '❌' : type === 'success' ? '✅' : 'ℹ️';
    logs = [...logs, `[${timestamp}] ${prefix} ${msg}`];
    console.log(msg);
  }

  async function createObject() {
    try {
      log('Creating object...', 'info');

      log(`Static constant appversion: ${MyObject.appversion}`, 'info');
      const newObj = await MyObject.create('fab');
      log(`Object created: ${newObj.handle}`, 'success');
      newObj.aEvent.on((intVal, boolVal) => {
        log(`🔔 Event received: int=${intVal}, bool=${boolVal}`);
      });

      // Log constants (instance and static)
      log(`Instance constant aversion: ${newObj.version}`, 'info');
      log(`Instance constant CPP_VERSION: ${MyObject.CPP_VERSION}`, 'info');

      obj = newObj;
    } catch (error) {
      log(`Failed to create object: ${error}`, 'error');
    }
  }

  function destroyObject() {
    if (!obj) {
      log('No object to destroy!', 'error');
      return;
    }

    try {
      const objToDestroy = obj;
      obj = null;
      objToDestroy.destroy();
      log('Object destroyed', 'success');
    } catch (error) {
      log(`Failed to destroy object: ${error}`, 'error');
    }
  }

  // ============ WEBGL2 ============
  let glCanvas: HTMLCanvasElement;
  let gl: WebGL2RenderingContext | null = null;
  let glError: string | null = null;
  let glAnimFrame: number = 0;
  let renderFrame: (() => void) | null = null;

  // WebGL2 resources
  let glProgram: WebGLProgram | null = null;
  let glTexture: WebGLTexture | null = null;
  let glPbo: WebGLBuffer | null = null;
  let glTexWidth = 0;
  let glTexHeight = 0;
  let hasFrame = false;

  // GLSL shaders – fullscreen triangle (no vertex buffer) + texture sampling
  const VERT_SRC = `#version 300 es
out vec2 vUV;
void main() {
  float x = float(gl_VertexID & 1) * 4.0 - 1.0;
  float y = float(gl_VertexID >> 1) * 4.0 - 1.0;
  gl_Position = vec4(x, y, 0.0, 1.0);
  vUV = vec2((x + 1.0) * 0.5, (1.0 - y) * 0.5);
}`;

  const FRAG_SRC = `#version 300 es
precision mediump float;
uniform sampler2D uTexture;
in vec2 vUV;
out vec4 fragColor;
void main() {
  fragColor = texture(uTexture, vUV);
}`;

  function compileShader(glCtx: WebGL2RenderingContext, type: number, src: string): WebGLShader {
    const shader = glCtx.createShader(type)!;
    glCtx.shaderSource(shader, src);
    glCtx.compileShader(shader);
    if (!glCtx.getShaderParameter(shader, glCtx.COMPILE_STATUS)) {
      throw new Error(glCtx.getShaderInfoLog(shader) ?? 'Shader compile error');
    }
    return shader;
  }

  function initWebGL() {
    const context = glCanvas.getContext('webgl2');
    if (!context) {
      glError = 'WebGL2 is not supported in this browser.';
      return;
    }
    gl = context;
    try {
      const vert = compileShader(gl, gl.VERTEX_SHADER, VERT_SRC);
      const frag = compileShader(gl, gl.FRAGMENT_SHADER, FRAG_SRC);

      const program = gl.createProgram()!;
      gl.attachShader(program, vert);
      gl.attachShader(program, frag);
      gl.linkProgram(program);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
        throw new Error(gl.getProgramInfoLog(program) ?? 'Program link error');
      }
      gl.deleteShader(vert);
      gl.deleteShader(frag);
      glProgram = program;

      gl.useProgram(glProgram);
      gl.uniform1i(gl.getUniformLocation(glProgram, 'uTexture'), 0);

      glTexture = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, glTexture);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

      // Render on demand – called only when a new frame is available
      renderFrame = () => {
        glAnimFrame = 0;
        if (!gl || !glProgram || !hasFrame) return;
        gl.viewport(0, 0, gl.canvas.width, gl.canvas.height);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.useProgram(glProgram);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, glTexture);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
      };

      glPbo = gl.createBuffer();
    } catch (e: any) {
      glError = `WebGL2 init failed: ${e.message ?? e}`;
    }
  }

  /** Upload RGBA pixel data as a GL texture via PBO and trigger rendering. */
  function uploadFrameToGL(pixels: Uint8Array, width: number, height: number) {
    if (!gl || !glTexture || !glPbo) return;

    // Copy pixel data into the PBO (async DMA to GPU, frees the main thread sooner)
    gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, glPbo);
    gl.bufferData(gl.PIXEL_UNPACK_BUFFER, pixels, gl.STREAM_DRAW);

    gl.bindTexture(gl.TEXTURE_2D, glTexture);

    if (glTexWidth !== width || glTexHeight !== height) {
      // Last arg is a byte offset into the PBO (not a pointer)
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, 0);
      glTexWidth = width;
      glTexHeight = height;
      glCanvas.width = width;
      glCanvas.height = height;
    } else {
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, 0);
    }

    gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);

    hasFrame = true;

    // Schedule a single render – no-op if one is already queued
    if (renderFrame && !glAnimFrame) {
      glAnimFrame = requestAnimationFrame(renderFrame);
    }
  }

  let frameCount = 0;
  let lastFrameNr = -1;
  let skippedTotal = 0;
  function onSharedBufferReceived(event: any) {
    const meta = event.additionalData;
    const buffer = event.getBuffer();
    const pixels = new Uint8Array(buffer, 0, meta.byteLength);

    uploadFrameToGL(pixels, meta.width, meta.height);

    // Track skipped frames via C++ frameNr in metadata
    const cppFrameNr = meta.frameNr ?? -1;
    if (lastFrameNr >= 0 && cppFrameNr > lastFrameNr + 1) {
      skippedTotal += (cppFrameNr - lastFrameNr - 1);
    }
    lastFrameNr = cppFrameNr;

    frameCount++;
    if (frameCount % 60 === 0) {
      log(`Received ${frameCount} frames, skipped ${skippedTotal} (C++ frame #${cppFrameNr})`, 'info');
    }

    // Buffer nach Verwendung freigeben
    //event.source.close();
  }

  onMount(() => {
    initWebGL();
    (window as any).chrome?.webview?.addEventListener('sharedbufferreceived', onSharedBufferReceived);
  });

  onDestroy(() => {
    (window as any).chrome?.webview?.removeEventListener('sharedbufferreceived', onSharedBufferReceived);
    if (glAnimFrame) cancelAnimationFrame(glAnimFrame);
    if (obj) {
      obj.destroy();
    }
  });

  async function callBar() {
    if (!obj) {
      log('No object!', 'error');
      return;
    }

    try {
      log('Calling bar()...', 'info');
      const result = await obj.bar();
      log(`bar() returned: ${result}`, 'success');
    } catch (error) {
      log(`bar() failed: ${error}`, 'error');
    }
  }

  async function callFoo() {
    if (!obj) {
      log('No object!', 'error');
      return;
    }

    try {
      log('Calling foo() (async)...', 'info');
      await obj.foo('Hello from Svelte! 🎉');
      log('foo() completed', 'success');
    } catch (error) {
      log(`foo() failed: ${error}`, 'error');
    }
  }

  async function callFile() {
    if (!obj) {
      log('No object!', 'error');
      return;
    }

    try {
      log('Opening file dialog...', 'info');
      await obj.file();
      log('File dialog completed', 'success');
    } catch (error) {
      log(`file() failed: ${error}`, 'error');
    }
  }

  async function callTestVectors() {
    if (!obj) {
      log('No object!', 'error');
      return;
    }

    try {
      log('Calling testVectors()...', 'info');
      await obj.testVectors();
      log('testVectors() completed - check properties above!', 'success');
    } catch (error) {
      log(`testVectors() failed: ${error}`, 'error');
    }
  }

  async function callThrowError() {
    if (!obj) {
      log('No object!', 'error');
      return;
    }

    try {
      log('Calling throwError()...', 'info');
      await obj.throwError();
    } catch (error) {
      const errText =
        error instanceof Error ? error.message : (typeof error === 'string' ? error : JSON.stringify(error));
      errorModalMessage = errText;
      errorModalOpen = true;
      log(`throwError() failed (expected): ${JSON.stringify(error)}`, 'error');
    }
  }

  async function callMultiParamTest() {
    if (!obj) {
      log('No object!', 'error');
      return;
    }

    await obj.bar();

    try {
      log('Calling multiParamTest() with 6 different parameter types...', 'info');
      const result = await obj.multiParamTest(
        999,                           // int
        true,                          // bool
        'Test String from JS',         // string
        [10, 20, 30, 40, 50],         // vector<int>
        MyObject.Status.Running,      // Status enum
        { a: 777, b: 888888888 }      // Pod struct
      );
      log(`multiParamTest() returned: ${result}`, 'success');
      log('✨ Check properties above - they should all be updated!', 'info');
    } catch (error) {
      log(`multiParamTest() failed: ${JSON.stringify(error)}`, 'error');
    }
  }

  async function callTransferSingleFrame() {
    if (!obj) {
      log('No object!', 'error');
      return;
    }
    try {
      log('Calling transferSingleFrame()...', 'info');
      await obj.transferSingleFrame();
      log('transferSingleFrame() completed', 'success');
    } catch (error) {
      log(`transferSingleFrame() failed: ${error}`, 'error');
    }
  }

  async function callStartVideo() {
    if (!obj) { log('No object!', 'error'); return; }
    try {
      log('Starting video...', 'info');
      await obj.startVideo();
      log('Video started', 'success');
    } catch (error) {
      log(`startVideo() failed: ${error}`, 'error');
    }
  }

  async function callStopVideo() {
    if (!obj) { log('No object!', 'error'); return; }
    try {
      log('Stopping video...', 'info');
      await obj.stopVideo();
      log('Video stopped', 'success');
    } catch (error) {
      log(`stopVideo() failed: ${error}`, 'error');
    }
  }

  function clearLog() {
    logs = [];
    log('Log cleared', 'info');
  }

  // ============ BENCHMARK FUNCTIONALITY ============

  interface BenchmarkStats {
    min: number;
    max: number;
    avg: number;
    stdev: number;
    total: number;
  }

  function calculateStats(values: number[]): BenchmarkStats {
    if (values.length === 0) {
      return { min: 0, max: 0, avg: 0, stdev: 0, total: 0 };
    }

    const min = Math.min(...values);
    const max = Math.max(...values);
    const total = values.reduce((a, b) => a + b, 0);
    const avg = total / values.length;

    // Standard deviation
    const variance = values.reduce((sum, val) => sum + Math.pow(val - avg, 2), 0) / values.length;
    const stdev = Math.sqrt(variance);

    return { min, max, avg, stdev, total };
  }

  async function runBenchmark() {
    try {
      log('=== STARTING BENCHMARK ===', 'info');

      // Create TestObject
      log('Creating TestObject...', 'info');
      const testObj = await TestObject.create() as TestObject;
      log(`TestObject created: ${testObj.handle}`, 'success');

      const iterations = 1000;
      const warmupRuns = 50;
      testObj.asyncProcess('f.a.b.');

      // ===== SYNC BENCHMARK =====
      log(`\n--- Sync Benchmark (${iterations} iterations) ---`, 'info');

      // Warmup
      for (let i = 0; i < warmupRuns; i++) {
        await testObj.benchmarkSync(i);
      }

      const syncTimes: number[] = [];
      for (let i = 0; i < iterations; i++) {
        const start = performance.now();
        await testObj.benchmarkSync(i);
        const elapsed = performance.now() - start;
        syncTimes.push(elapsed);
      }

      const syncStats = calculateStats(syncTimes);
      log(`Sync Results (${iterations}x):`, 'success');
      log(`  Total: ${syncStats.total.toFixed(2)} ms`, 'info');
      log(`  Avg:   ${syncStats.avg.toFixed(3)} ms (${(syncStats.avg * 1000).toFixed(1)} µs)`, 'info');
      log(`  Min:   ${syncStats.min.toFixed(3)} ms (${(syncStats.min * 1000).toFixed(1)} µs)`, 'info');
      log(`  Max:   ${syncStats.max.toFixed(3)} ms (${(syncStats.max * 1000).toFixed(1)} µs)`, 'info');
      log(`  StdDev: ${syncStats.stdev.toFixed(3)} ms (${(syncStats.stdev * 1000).toFixed(1)} µs)`, 'info');

      // ===== ASYNC BENCHMARK =====
      log(`\n--- Async Benchmark (${iterations} iterations) ---`, 'info');

      // Warmup
      for (let i = 0; i < warmupRuns; i++) {
        await testObj.benchmarkAsync(i);
      }

      const asyncTimes: number[] = [];
      for (let i = 0; i < iterations; i++) {
        const start = performance.now();
        await testObj.benchmarkAsync(i);
        const elapsed = performance.now() - start;
        asyncTimes.push(elapsed);
      }

      const asyncStats = calculateStats(asyncTimes);
      log(`Async Results (${iterations}x):`, 'success');
      log(`  Total: ${asyncStats.total.toFixed(2)} ms`, 'info');
      log(`  Avg:   ${asyncStats.avg.toFixed(3)} ms (${(asyncStats.avg * 1000).toFixed(1)} µs)`, 'info');
      log(`  Min:   ${asyncStats.min.toFixed(3)} ms (${(asyncStats.min * 1000).toFixed(1)} µs)`, 'info');
      log(`  Max:   ${asyncStats.max.toFixed(3)} ms (${(asyncStats.max * 1000).toFixed(1)} µs)`, 'info');
      log(`  StdDev: ${asyncStats.stdev.toFixed(3)} ms (${(asyncStats.stdev * 1000).toFixed(1)} µs)`, 'info');

      // Comparison
      log(`\n--- Comparison ---`, 'info');
      log(`  Async overhead: ${(asyncStats.avg - syncStats.avg).toFixed(3)} ms (${((asyncStats.avg - syncStats.avg) * 1000).toFixed(1)} µs)`, 'info');
      log(`  Async is ${(asyncStats.avg / syncStats.avg).toFixed(2)}x slower than Sync`, 'info');

      // Cleanup
      log('\nDestroying TestObject...', 'info');
      testObj.destroy();
      log('TestObject destroyed', 'success');
      log('=== BENCHMARK COMPLETE ===', 'success');

    } catch (error) {
      log(`Benchmark failed: ${error}`, 'error');
    }
  }

  onMount(() => {
    log('WebBridge Demo initialized 🚀', 'success');
  });
</script>

<div class="min-h-screen bg-gradient-to-br from-purple-600 to-indigo-800 p-5">
  <div class="max-w-5xl mx-auto">
    <!-- Hero Section -->
    <div class="card bg-base-100 shadow-2xl mb-6">
      <div class="card-body">
        <h1 class="card-title text-4xl font-bold text-center justify-center mb-4">
          🌉 WebBridge Svelte Demo
        </h1>

        <!-- Status Badge -->
        <div class="flex justify-center mb-4">
          {#if obj !== null}
            <div class="badge badge-success badge-lg gap-2">
              <div class="w-3 h-3 rounded-full bg-green-500 animate-pulse"></div>
              Object active
            </div>
          {:else}
            <div class="badge badge-neutral badge-lg gap-2">
              <div class="w-3 h-3 rounded-full bg-gray-400"></div>
              No object
            </div>
          {/if}
        </div>

        <!-- Control Buttons -->
        <div class="flex flex-wrap gap-2 justify-center">
          <button class="btn btn-primary" on:click={createObject} disabled={obj !== null}>
            Create Object
          </button>
          <button class="btn btn-secondary" on:click={destroyObject} disabled={obj === null}>
            Destroy Object
          </button>
          <button class="btn btn-accent" on:click={callBar} disabled={obj === null}>
            Call bar()
          </button>
          <button class="btn btn-accent" on:click={callFoo} disabled={obj === null}>
            Call foo()
          </button>
          <button class="btn btn-accent" on:click={callFile} disabled={obj === null}>
            Call file()
          </button>
          <button class="btn btn-accent" on:click={callTestVectors} disabled={obj === null}>
            Test Vectors
          </button>
          <button class="btn btn-error" on:click={callThrowError} disabled={obj === null}>
            Throw Error 💥
          </button>
          <button class="btn btn-info" on:click={callMultiParamTest} disabled={obj === null}>
            Test Multi-Params 🎯
          </button>
          <button class="btn btn-ghost" on:click={clearLog}>
            Clear Log
          </button>
          <button class="btn btn-warning font-bold" on:click={runBenchmark}>
            🚀 Run Benchmark (TestObject)
          </button>
        </div>
      </div>
    </div>

    <!-- Properties Section -->
    <div class="card bg-base-100 shadow-xl mb-6">
      <div class="card-body">
        <h2 class="card-title text-2xl">Properties (auto-synced via WebBridge)</h2>

        {#if obj && aBool && strProp && counter && numbers && status && pod}
          <div class="grid gap-3">
            <div class="stats shadow">
              <div class="stat">
                <div class="stat-title">aBool</div>
                <div class="stat-value text-primary text-2xl">{$aBool}</div>
              </div>
            </div>

            <div class="stats shadow">
              <div class="stat">
                <div class="stat-title">strProp</div>
                <div class="stat-value text-secondary text-2xl font-mono">{$strProp || '(empty)'}</div>
              </div>
            </div>

            <div class="stats shadow">
              <div class="stat">
                <div class="stat-title">counter</div>
                <div class="stat-value text-accent text-2xl">{$counter}</div>
              </div>
            </div>

            <div class="stats shadow">
              <div class="stat">
                <div class="stat-title">numbers</div>
                <div class="stat-value text-xl font-mono">[{$numbers ? $numbers.join(', ') : ''}]</div>
              </div>
            </div>

            <div class="stats shadow">
              <div class="stat">
                <div class="stat-title">status</div>
                <div class="stat-value text-xl">
                  {#if $status === 'Idle'}
                    <span class="text-gray-500">{$status}</span>
                  {:else if $status === 'Running'}
                    <span class="text-warning">{$status}</span>
                  {:else if $status === 'Completed'}
                    <span class="text-success">{$status}</span>
                  {:else if $status === 'Error'}
                    <span class="text-error">{$status}</span>
                  {:else}
                    <span>{$status || 'Idle'}</span>
                  {/if}
                </div>
              </div>
            </div>

            {#if $pod}
            <div class="stats shadow">
              <div class="stat">
                <div class="stat-title">pod</div>
                <div class="stat-value text-xl font-mono">{`{ a: ${$pod.a}, b: ${$pod.b} }`}</div>
              </div>
            </div>
            {/if}
          </div>
        {:else}
          <div class="alert">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" class="stroke-info shrink-0 w-6 h-6">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z"></path>
            </svg>
            <span>No object created yet.</span>
          </div>
        {/if}
      </div>
    </div>

    <!-- WebGL2 Section -->
    <div class="card bg-base-100 shadow-xl mb-6">
      <div class="card-body">
        <h2 class="card-title text-2xl">🎮 WebGL2 Scene</h2>
        <div class="flex gap-2 mb-4">
          <button class="btn btn-primary" on:click={callTransferSingleFrame} disabled={obj === null}>Transfer single frame</button>
          <button class="btn btn-success" on:click={callStartVideo} disabled={obj === null}>Begin video</button>
          <button class="btn btn-error" on:click={callStopVideo} disabled={obj === null}>End video</button>
        </div>
        {#if glError}
          <div class="alert alert-error">
            <span>{glError}</span>
          </div>
        {:else}
          <canvas bind:this={glCanvas} width="800" height="600" class="rounded-lg shadow-lg w-full"></canvas>
        {/if}
      </div>
    </div>

    <!-- Image Section -->
    <div class="card bg-base-100 shadow-xl mb-6">
      <div class="card-body">
        <h2 class="card-title text-2xl">🖼️ Embedded Image</h2>
        <figure>
          <img src={bildUrl} alt="Beispielbild" class="rounded-lg shadow-lg" />
        </figure>
        <div class="alert alert-info mt-4">
          <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" class="stroke-current shrink-0 w-6 h-6">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z"></path>
          </svg>
          <span>This image is bundled with the app and served from embedded resources.</span>
        </div>
      </div>
    </div>

    <!-- Log Section -->
    <div class="card bg-base-100 shadow-xl">
      <div class="card-body">
        <h2 class="card-title text-2xl">Console Log</h2>
        <div class="mockup-code max-h-96 overflow-y-auto">
          {#each logs as logEntry}
            <pre data-prefix=">" class="text-sm"><code>{logEntry}</code></pre>
          {/each}
        </div>
      </div>
    </div>
  </div>
</div>

<!-- Error Modal -->
{#if errorModalOpen}
  <div class="modal modal-open">
    <div class="modal-box">
      <h3 class="font-bold text-lg text-error">❌ Fehler</h3>
      <p class="py-4">{errorModalMessage}</p>
      <div class="modal-action">
        <button class="btn btn-primary" on:click={() => errorModalOpen = false}>OK</button>
      </div>
    </div>
  </div>
{/if}
