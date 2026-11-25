// Nebel Web Runtime Bridge (MIT License)
// Loads loose assets/ and scripts/ beside nebel.wasm into MEMFS (never baked into WASM),
// bridges browser file upload/download, and runs Depth Anything V2 Small (ONNX) in WASM.

(function () {
  const ORT_BASE = "https://cdn.jsdelivr.net/npm/onnxruntime-web@1.20.1/dist/";
  const ORT_CDN = ORT_BASE + "ort.min.js";
  const DEPTH_MODEL_URL =
    "https://huggingface.co/onnx-community/depth-anything-v2-small/resolve/main/onnx/model_quantized.onnx";
  const DEPTH_MODEL_SHA256 =
    "fcf51f1b230362b28690bb9d1809bf0431f29cad20534e3f589bd7285547f20d";

  let ortSessionPromise = null;
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
        // Point ORT to CDN wasm binaries and single-thread mode (no COOP/COEP needed on GitHub Pages)
        window.ort.env.wasm.wasmPaths = ORT_BASE;
        window.ort.env.wasm.numThreads = 1;
        window.ort.env.wasm.simd = true;
        const modelBytes = await fetchCachedModel(DEPTH_MODEL_URL);
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

  window.nebelWebRunAutoDepth = async function (rgbaPtr, w, h) {
    const mod = window.Module;
    if (!mod) return;
    const seq = ++depthJobSeq;
    const copy = new Uint8Array(mod.HEAPU8.buffer, rgbaPtr, w * h * 4).slice();
    try {
      const session = await getDepthSession();
      if (seq !== depthJobSeq) return; // Superseded by newer photo

      const size = 392; // Multiple of ViT patch 14 (~3s in WASM, crisp antlers/trees)
      const tensorData = new Float32Array(3 * size * size);
      const mean = [0.485, 0.456, 0.406];
      const std = [0.229, 0.224, 0.225];
      const plane = size * size;

      for (let y = 0; y < size; y++) {
        const sy = Math.min(h - 1, Math.floor((y * h) / size));
        for (let x = 0; x < size; x++) {
          const sx = Math.min(w - 1, Math.floor((x * w) / size));
          const si = (sy * w + sx) * 4;
          const di = y * size + x;
          tensorData[0 * plane + di] = (copy[si + 0] / 255 - mean[0]) / std[0];
          tensorData[1 * plane + di] = (copy[si + 1] / 255 - mean[1]) / std[1];
          tensorData[2 * plane + di] = (copy[si + 2] / 255 - mean[2]) / std[2];
        }
      }

      const inputName = session.inputNames[0];
      const feeds = {};
      feeds[inputName] = new window.ort.Tensor("float32", tensorData, [1, 3, size, size]);
      const outMap = await session.run(feeds);
      if (seq !== depthJobSeq) return;

      const outTensor = outMap[session.outputNames[0]];
      const raw = outTensor.data;
      const dims = outTensor.dims;
      const oh = dims[dims.length - 2];
      const ow = dims[dims.length - 1];

      // Depth Anything outputs disparity (higher = closer); invert so higher = farther
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

      const ptr = mod._malloc(inv.byteLength);
      if (!ptr) throw new Error("WASM OOM for depth map");
      mod.HEAPF32.set(inv, ptr >> 2);
      mod._nebel_web_on_depth_ready(ptr, ow, oh);
      mod._free(ptr);
    } catch (err) {
      console.warn("[nebel] Auto depth fell back to classical CV:", err);
      if (seq === depthJobSeq && mod._nebel_web_on_depth_failed) {
        mod._nebel_web_on_depth_failed();
      }
    }
  };

  window.Module = {
    canvas: document.getElementById("canvas"),
    preRun: [
      function () {
        setupFileHandlers();
        preloadLooseAssets(window.Module);
      },
    ],
    onRuntimeInitialized: function () {
      hideLoader();
    },
    printErr: function (text) {
      console.error(text);
    },
  };
})();
