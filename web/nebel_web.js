// Nebel v3 Web Runtime Bridge (MIT License)
// Loads loose assets/ and scripts/ beside nebel.wasm into MEMFS (never baked into WASM),
// distinguishes Web Desktop vs Web Mobile, bridges file I/O, detects locale (EN/DE/PL),
// and runs aspect-preserving PatchFusion Depth Anything V2 Small (Apache 2.0) via WebGPU/WASM.

(function () {
  const ORT_BASE = "https://cdn.jsdelivr.net/npm/onnxruntime-web@1.20.1/dist/";
  const ORT_CDN = ORT_BASE + "ort.min.js";
  const DEPTH_MODEL_URL =
    "https://huggingface.co/onnx-community/depth-anything-v2-small/resolve/main/onnx/model_quantized.onnx";
  const DEPTH_MODEL_SHA256 =
    "fcf51f1b230362b28690bb9d1809bf0431f29cad20534e3f589bd7285547f20d";

  let ortSessionPromise = null;
  let ortActiveBackend = "WebGL2 + WASM";
  let depthJobSeq = 0;

  function setLoaderStatus(msg) {
    const el = document.getElementById("loader-status");
    if (el) el.textContent = msg;
  }

  function hideLoader() {
    const el = document.getElementById("loader");
    if (el) el.classList.add("hidden");
  }

  function ensureDir(fs, filepath) {
    const parts = filepath.split("/").slice(0, -1);
    let cur = "";
    for (const p of parts) {
      if (!p) continue;
      cur += "/" + p;
      try { fs.mkdir(cur); } catch (_) { /* already exists */ }
    }
  }

  function detectMobileWeb() {
    const ua = navigator.userAgent || "";
    const uaMobile = /Android|iPhone|iPad|iPod|Mobile|webOS/i.test(ua);
    const coarse = window.matchMedia && window.matchMedia("(pointer: coarse)").matches;
    const narrowTouch = (navigator.maxTouchPoints || 0) > 0 && window.innerWidth < 900;
    return uaMobile || coarse || narrowTouch;
  }

  function detectBrowserLang() {
    const params = new URLSearchParams(window.location.search);
    const q = (params.get("lang") || "").toLowerCase();
    if (q === "de" || q === "pl" || q === "en") return q;
    const raw = (navigator.language || "en").toLowerCase();
    if (raw.startsWith("de")) return "de";
    if (raw.startsWith("pl")) return "pl";
    return "en";
  }

  function allocCString(mod, str) {
    const len = mod.lengthBytesUTF8(str) + 1;
    const ptr = mod._malloc(len);
    if (ptr) mod.stringToUTF8(str, ptr, len);
    return ptr;
  }

  window.nebelWebInitEnv = function () {
    const mod = window.Module;
    if (!mod || !mod._nebel_web_set_env) return;
    const isMobile = detectMobileWeb() ? 1 : 0;
    const lang = detectBrowserLang();
    const hasWebGpu = typeof navigator !== "undefined" && !!navigator.gpu;
    ortActiveBackend = hasWebGpu ? "WebGL2 + WebGPU" : "WebGL2 + WASM";
    const langPtr = allocCString(mod, lang);
    const backPtr = allocCString(mod, ortActiveBackend);
    mod._nebel_web_set_env(isMobile, langPtr, backPtr);
    if (langPtr) mod._free(langPtr);
    if (backPtr) mod._free(backPtr);
  };

  async function preloadLooseAssets(mod) {
    mod.addRunDependency("nebel_loose_assets");
    try {
      const res = await fetch("assets/manifest.json", { cache: "no-cache" });
      if (!res.ok) throw new Error("manifest fetch failed: " + res.status);
      const manifest = await res.json();
      const files = Array.isArray(manifest.files) ? manifest.files : [];
      let done = 0;
      await Promise.all(
        files.map(async (rel) => {
          if (!rel || rel.includes("..") || rel.startsWith("/")) return;
          const r = await fetch(rel);
          if (!r.ok) throw new Error("asset missing: " + rel);
          const buf = new Uint8Array(await r.arrayBuffer());
          ensureDir(mod.FS, rel);
          mod.FS.writeFile("/" + rel, buf);
          done++;
          setLoaderStatus(`Loaded ${done}/${files.length} external files...`);
        })
      );
    } catch (err) {
      console.error("[nebel] loose asset preload error:", err);
      setLoaderStatus("Failed to load external assets: " + err.message);
    } finally {
      mod.removeRunDependency("nebel_loose_assets");
    }
  }

  function feedFileBytesToWasm(bytes) {
    const mod = window.Module;
    if (!mod || !mod._malloc || !mod._nebel_web_on_file_loaded) return;
    const ptr = mod._malloc(bytes.length);
    if (!ptr) return;
    mod.HEAPU8.set(bytes, ptr);
    mod._nebel_web_on_file_loaded(ptr, bytes.length);
    mod._free(ptr);
  }

  function setupFileHandlers() {
    const input = document.getElementById("file-input");
    if (input) {
      input.addEventListener("change", async () => {
        const file = input.files && input.files[0];
        if (!file) return;
        const buf = new Uint8Array(await file.arrayBuffer());
        feedFileBytesToWasm(buf);
        input.value = "";
      });
    }

    const hint = document.getElementById("drop-hint");
    window.addEventListener("dragover", (e) => {
      e.preventDefault();
      if (hint) hint.classList.add("show");
    });
    window.addEventListener("dragleave", (e) => {
      if (e.relatedTarget === null && hint) hint.classList.remove("show");
    });
    window.addEventListener("drop", async (e) => {
      e.preventDefault();
      if (hint) hint.classList.remove("show");
      const file = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
      if (!file) return;
      const buf = new Uint8Array(await file.arrayBuffer());
      feedFileBytesToWasm(buf);
    });
  }

  window.nebelWebOpenFile = function () {
    const input = document.getElementById("file-input");
    if (input) input.click();
  };

  window.nebelWebDownloadFile = function (ptr, len, filename, mime) {
    const mod = window.Module;
    const uptr = ptr >>> 0;
    const ulen = len >>> 0;
    if (!mod || !uptr || !ulen) return;
    const copy = new Uint8Array(mod.HEAPU8.buffer, uptr, ulen).slice();
    const blob = new Blob([copy], { type: mime || "application/octet-stream" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = filename || "nebel_export.png";
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 4000);
  };

  function loadScript(src) {
    return new Promise((resolve, reject) => {
      const s = document.createElement("script");
      s.src = src;
      s.async = true;
      s.onload = resolve;
      s.onerror = () => reject(new Error("Failed to load " + src));
      document.head.appendChild(s);
    });
  }

  async function verifySha256(bytes, expectedHex) {
    if (!expectedHex || !window.crypto || !window.crypto.subtle) return true;
    const digest = await window.crypto.subtle.digest("SHA-256", bytes);
    const hex = Array.from(new Uint8Array(digest))
      .map((b) => b.toString(16).padStart(2, "0"))
      .join("");
    return hex === expectedHex;
  }

  async function fetchCachedModel(url) {
    if (typeof caches !== "undefined") {
      const cache = await caches.open("nebel-onnx-v1");
      const hit = await cache.match(url);
      if (hit) {
        const bytes = new Uint8Array(await hit.arrayBuffer());
        if (await verifySha256(bytes, DEPTH_MODEL_SHA256)) return bytes;
        await cache.delete(url);
      }
      const res = await fetch(url);
      if (!res.ok) throw new Error("Model HTTP " + res.status);
      const bytes = new Uint8Array(await res.clone().arrayBuffer());
      if (!(await verifySha256(bytes, DEPTH_MODEL_SHA256))) {
        throw new Error("ONNX model SHA-256 mismatch");
      }
      await cache.put(url, res);
      return bytes;
    }
    const res = await fetch(url);
    if (!res.ok) throw new Error("Model HTTP " + res.status);
    const bytes = new Uint8Array(await res.arrayBuffer());
    if (!(await verifySha256(bytes, DEPTH_MODEL_SHA256))) {
      throw new Error("ONNX model SHA-256 mismatch");
    }
    return bytes;
  }

  async function getDepthSession() {
    if (!ortSessionPromise) {
      ortSessionPromise = (async () => {
        if (!window.ort) await loadScript(ORT_CDN);
        window.ort.env.wasm.wasmPaths = ORT_BASE;
        window.ort.env.wasm.numThreads = 1;
        window.ort.env.wasm.simd = true;
        const modelBytes = await fetchCachedModel(DEPTH_MODEL_URL);

        // Prefer WebGPU on desktop browsers when available, fall back seamlessly to WASM SIMD
        if (typeof navigator !== "undefined" && navigator.gpu) {
          try {
            const sess = await window.ort.InferenceSession.create(modelBytes, {
              executionProviders: ["webgpu", "wasm"],
            });
            ortActiveBackend = "WebGL2 + WebGPU";
            return sess;
          } catch (_) {
            /* Fall back to WASM */
          }
        }
        ortActiveBackend = "WebGL2 + WASM";
        return window.ort.InferenceSession.create(modelBytes, {
          executionProviders: ["wasm"],
        });
      })().catch((err) => {
        ortSessionPromise = null;
        throw err;
      });
    }
    return ortSessionPromise;
  }

  function roundMult14(v) {
    return Math.max(224, Math.min(728, Math.round(v / 14) * 14));
  }

  function calcAspectDims(w, h, targetLong) {
    if (w >= h) {
      const ow = roundMult14(targetLong);
      const oh = roundMult14((ow * h) / w);
      return [ow, oh];
    }
    const oh = roundMult14(targetLong);
    const ow = roundMult14((oh * w) / h);
    return [ow, oh];
  }

  function preprocessCropBilinear(rgba, w, h, u0, v0, u1, v1, dw, dh) {
    const out = new Float32Array(3 * dw * dh);
    const mean = [0.485, 0.456, 0.406];
    const invStd = [1 / 0.229, 1 / 0.224, 1 / 0.225];
    const plane = dw * dh;

    for (let y = 0; y < dh; y++) {
      const tv = dh > 1 ? y / (dh - 1) : 0;
      const sy = (v0 + (v1 - v0) * tv) * (h - 1);
      const y0 = Math.max(0, Math.min(h - 1, Math.floor(sy)));
      const y1 = Math.min(h - 1, y0 + 1);
      const fy = sy - y0;
      for (let x = 0; x < dw; x++) {
        const tu = dw > 1 ? x / (dw - 1) : 0;
        const sx = (u0 + (u1 - u0) * tu) * (w - 1);
        const x0 = Math.max(0, Math.min(w - 1, Math.floor(sx)));
        const x1 = Math.min(w - 1, x0 + 1);
        const fx = sx - x0;

        const i00 = (y0 * w + x0) * 4;
        const i10 = (y0 * w + x1) * 4;
        const i01 = (y1 * w + x0) * 4;
        const i11 = (y1 * w + x1) * 4;
        const di = y * dw + x;

        for (let c = 0; c < 3; c++) {
          const top = rgba[i00 + c] + (rgba[i10 + c] - rgba[i00 + c]) * fx;
          const bot = rgba[i01 + c] + (rgba[i11 + c] - rgba[i01 + c]) * fx;
          const v255 = top + (bot - top) * fy;
          out[c * plane + di] = (v255 / 255 - mean[c]) * invStd[c];
        }
      }
    }
    return out;
  }

  async function inferDepthCrop(session, rgba, w, h, u0, v0, u1, v1, dw, dh) {
    const tensorData = preprocessCropBilinear(rgba, w, h, u0, v0, u1, v1, dw, dh);
    const inputName = session.inputNames[0];
    const feeds = {};
    feeds[inputName] = new window.ort.Tensor("float32", tensorData, [1, 3, dh, dw]);
    const outMap = await session.run(feeds);
    const outTensor = outMap[session.outputNames[0]];
    const raw = outTensor.data;
    const dims = outTensor.dims;
    const oh = dims[dims.length - 2];
    const ow = dims[dims.length - 1];

    const inv = new Float32Array(ow * oh);
    let lo = Infinity, hi = -Infinity;
    for (let i = 0; i < raw.length; i++) {
      if (raw[i] < lo) lo = raw[i];
      if (raw[i] > hi) hi = raw[i];
    }
    const span = Math.max(1e-5, hi - lo);
    for (let i = 0; i < ow * oh; i++) {
      inv[i] = 1.0 - (raw[i] - lo) / span;
    }
    return { data: inv, w: ow, h: oh };
  }

  window.nebelWebRunAutoDepth = async function (rgbaPtr, w, h, targetEdge, tileGrid) {
    const mod = window.Module;
    if (!mod) return;
    const seq = ++depthJobSeq;
    const uptr = rgbaPtr >>> 0;
    const copy = new Uint8Array(mod.HEAPU8.buffer, uptr, w * h * 4).slice();
    const edge = targetEdge > 200 ? targetEdge : 420;
    const grid = tileGrid > 1 ? Math.min(tileGrid, 2) : 1;

    try {
      const session = await getDepthSession();
      if (seq !== depthJobSeq) return;

      if (mod._nebel_web_on_depth_progress) mod._nebel_web_on_depth_progress(0.15);
      const [gwIn, ghIn] = calcAspectDims(w, h, edge);
      const globalRes = await inferDepthCrop(session, copy, w, h, 0, 0, 1, 1, gwIn, ghIn);
      if (seq !== depthJobSeq) return;

      if (grid <= 1) {
        const ptr = mod._malloc(globalRes.data.byteLength);
        if (!ptr) throw new Error("WASM OOM for depth map");
        mod.HEAPF32.set(globalRes.data, ptr >>> 2);
        mod._nebel_web_on_depth_ready(ptr, globalRes.w, globalRes.h);
        mod._free(ptr);
        return;
      }

      // 2x2 overlapping PatchFusion tiles on Web Desktop
      const [twIn, thIn] = calcAspectDims(w, h, Math.min(392, edge));
      const overlap = 0.16;
      const span = (1.0 + overlap * (grid - 1)) / grid;
      const step = (1.0 - span) / (grid - 1);
      const tileList = [];
      const boxes = [];
      let done = 0;
      const total = grid * grid;

      for (let ty = 0; ty < grid; ty++) {
        for (let tx = 0; tx < grid; tx++) {
          const u0 = tx * step;
          const v0 = ty * step;
          const u1 = Math.min(1.0, u0 + span);
          const v1 = Math.min(1.0, v0 + span);
          const tRes = await inferDepthCrop(session, copy, w, h, u0, v0, u1, v1, twIn, thIn);
          if (seq !== depthJobSeq) return;
          tileList.push(tRes);
          boxes.push(u0, v0, u1, v1);
          done++;
          if (mod._nebel_web_on_depth_progress) {
            mod._nebel_web_on_depth_progress(0.2 + 0.75 * (done / total));
          }
        }
      }

      const tw = tileList[0].w;
      const th = tileList[0].h;
      const flatTiles = new Float32Array(tileList.length * tw * th);
      for (let i = 0; i < tileList.length; i++) {
        flatTiles.set(tileList[i].data, i * tw * th);
      }
      const boxArr = new Float32Array(boxes);

      const gPtr = mod._malloc(globalRes.data.byteLength);
      const tPtr = mod._malloc(flatTiles.byteLength);
      const bPtr = mod._malloc(boxArr.byteLength);
      if (!gPtr || !tPtr || !bPtr) {
        if (gPtr) mod._free(gPtr);
        if (tPtr) mod._free(tPtr);
        if (bPtr) mod._free(bPtr);
        throw new Error("WASM OOM for PatchFusion tiles");
      }
      mod.HEAPF32.set(globalRes.data, gPtr >>> 2);
      mod.HEAPF32.set(flatTiles, tPtr >>> 2);
      mod.HEAPF32.set(boxArr, bPtr >>> 2);
      mod._nebel_web_on_depth_fused(gPtr, globalRes.w, globalRes.h, tPtr, bPtr, tw, th, tileList.length);
      mod._free(gPtr);
      mod._free(tPtr);
      mod._free(bPtr);
    } catch (err) {
      console.warn("[nebel] Auto depth fell back to classical CV:", err);
      if (seq === depthJobSeq && mod._nebel_web_on_depth_failed) {
        mod._nebel_web_on_depth_failed();
      }
    }
  };

  window.Module = {
    canvas: (function () {
      const c = document.getElementById("canvas");
      c.addEventListener("webglcontextlost", (e) => {
        e.preventDefault();
        alert("WebGL context lost. Please reload the page.");
      });
      return c;
    })(),
    preRun: [
      function () {
        preloadLooseAssets(window.Module);
      },
    ],
    onRuntimeInitialized: function () {
      setupFileHandlers();
      hideLoader();
    },
    printErr: function (text) {
      console.warn("[nebel]", text);
    },
  };
})();
