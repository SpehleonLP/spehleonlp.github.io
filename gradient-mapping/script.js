(function() {
  'use strict';

  // =====================================================================
  //  DOM refs
  // =====================================================================
  const playPause = document.getElementById('playPause');
  const playIcon = document.getElementById('playIcon');
  const pauseIcon = document.getElementById('pauseIcon');
  const copyShader = document.getElementById('copyShader');
  const toast = document.getElementById('toast');

  // Timeline
  const timelineTrack = document.getElementById('timelineTrack');
  const playhead = document.getElementById('playhead');
  const flagIn = document.getElementById('flagIn');
  const flagOut = document.getElementById('flagOut');
  const flagInTip = document.getElementById('flagInTip');
  const flagOutTip = document.getElementById('flagOutTip');
  const zoneIn = document.getElementById('zoneIn');
  const zoneActive = document.getElementById('zoneActive');
  const zoneOut = document.getElementById('zoneOut');

  // Time display
  const timeCurrent = document.getElementById('timeCurrent');
  const durationBtn = document.getElementById('durationBtn');
  const durationText = document.getElementById('durationText');
  const durationPopover = document.getElementById('durationPopover');
  const durationSlider = document.getElementById('durationSlider');
  const durationVal = document.getElementById('durationVal');

  // Effect stacks
  const gradientStack = document.getElementById('gradientStack');
  const erosionStack = document.getElementById('erosionStack');

  // Preview images
  const gradientPreview = document.getElementById('gradientPreview');
  const erosionPreview = document.getElementById('erosionPreview');

  // Progress modal
  const progressModal = document.getElementById('progressModal');

  // =====================================================================
  //  State
  // =====================================================================
  let duration = 2.0;
  let fadeInPct = 0.25;
  let fadeOutPct = 0.25;
  let time = 0;
  let isPlaying = false;
  let lastTimestamp = null;
  let animFrameId = null;
  let glNeedsRender = true;  // Dirty flag for GL rendering

  // Derived values
  function fadeInSeconds() { return fadeInPct * duration; }
  function fadeOutSeconds() { return fadeOutPct * duration; }
  function fadeOutFlagPct() { return 1 - fadeOutPct; }
  function timePct() { return duration > 0 ? time / duration : 0; }

  // =====================================================================
  //  Worker bridge
  // =====================================================================
  let worker = null;
  let catalog = null;        // Effect catalog from WASM (null until ready)
  let nextMsgId = 1;
  let pendingCallbacks = {}; // id -> { resolve, reject }
  let latestProcessId = { 0: 0, 1: 0 }; // stackType -> latest sent id
  let processDebounceTimers = { 0: null, 1: null };

  // Grapick color ramp instances (keyed by container element id)
  let grapickIdCounter = 0;
  let grapickInstances = {};

  const STACK_GRADIENT = 0;
  const STACK_EROSION = 1;

  // =====================================================================
  //  Uint8 pack/unpack functions for parameters
  // =====================================================================
  const packFunctions = {
    linear: (val, min, max) => Math.round(((val - min) / (max - min)) * 255),
    log: (val, min, max) => Math.round((Math.log(val / min) / Math.log(max / min)) * 255),
    angle: (val) => Math.round(((val + Math.PI) / (2 * Math.PI)) * 255),
    signed: (val) => Math.round(((val + 1) / 2) * 255),
    int: (val, min, max) => Math.round(((val - min) / (max - min)) * 255),
    seed: (val) => Math.round(val / 3922),
    enum: (val) => val
  };

  const unpackFunctions = {
    linear: (u, min, max) => min + (u / 255) * (max - min),
    log: (u, min, max) => min * Math.pow(max / min, u / 255),
    angle: (u) => (u / 255) * 2 * Math.PI - Math.PI,
    signed: (u) => (u / 255) * 2 - 1,
    int: (u, min, max) => Math.round(min + (u / 255) * (max - min)),
    seed: (u) => u * 3922,
    enum: (u) => u
  };

  // Format a uint8 slider value for display using unpack
  function formatSliderDisplay(slider) {
    const u = parseInt(slider.value);
    const unpack = slider.getAttribute('data-unpack') || 'linear';
    const min = parseFloat(slider.getAttribute('data-min')) || 0;
    const max = parseFloat(slider.getAttribute('data-max')) || 1;
    const realValue = unpackFunctions[unpack](u, min, max);

    if (unpack === 'angle') {
      return (realValue * 180 / Math.PI).toFixed(1) + '°';
    } else if (unpack === 'int' || unpack === 'seed') {
      return Math.round(realValue).toString();
    } else {
      return formatParamValue(realValue);
    }
  }

  function stackTypeFor(stackEl) {
    return stackEl === gradientStack ? STACK_GRADIENT : STACK_EROSION;
  }

  function stackElFor(stackType) {
    return stackType === STACK_GRADIENT ? gradientStack : erosionStack;
  }

  function previewElFor(stackType) {
    return stackType === STACK_GRADIENT ? gradientPreview : erosionPreview;
  }

  // Send a message to the worker and return a promise for the response.
  function workerSend(type, data) {
    return new Promise((resolve, reject) => {
      const id = nextMsgId++;
      pendingCallbacks[id] = { resolve, reject };
      const msg = Object.assign({ id, type }, data);

      // Transfer ArrayBuffers when present
      const transfer = [];
      if (data && data.fileData instanceof ArrayBuffer) {
        transfer.push(data.fileData);
      }

      worker.postMessage(msg, transfer);
    });
  }

  // Fire-and-forget message (no response expected by id, e.g. processStack
  // where we track by latestProcessId instead).
  function workerFire(type, data) {
    const id = nextMsgId++;
    const msg = Object.assign({ id, type }, data);
    worker.postMessage(msg);
    return id;
  }

  // Handle all messages from worker
  function onWorkerMessage(e) {
    const msg = e.data;

    // Unsolicited events (auto-config callbacks from WASM)
    if (msg.type === 'clearAutoEffects') {
      clearAutoEffects(msg.stackType);
      return;
    }
    if (msg.type === 'pushAutoEffect') {
      pushAutoEffect(msg.stackType, msg.effectId, msg.params);
      return;
    }
    if (msg.type === 'setSourceTiming') {
      setSourceTiming(msg.stackType, msg.fadeInTime, msg.fadeOutTime, msg.totalDuration);
      return;
    }

    // Stack result — check for staleness
    if (msg.type === 'stackResult') {
      if (msg.id < latestProcessId[msg.stackType]) return; // stale
      handleStackResult(msg);
      return;
    }

    // Init broadcast (before catalog request)
    if (msg.type === 'init') {
      if (!msg.success) {
        console.warn('Worker WASM init failed:', msg.error);
      }
      return;
    }

    // Request/response with id
    if (msg.id && pendingCallbacks[msg.id]) {
      const cb = pendingCallbacks[msg.id];
      delete pendingCallbacks[msg.id];
      if (msg.type === 'error') {
        cb.reject(new Error(msg.error));
      } else {
        cb.resolve(msg);
      }
      return;
    }

    // Errors without id
    if (msg.type === 'error') {
      console.error('Worker error:', msg.error);
    }
  }

  // Initialize the worker and fetch the catalog
  async function initWorker() {
    worker = new Worker('worker.js');
    worker.onmessage = onWorkerMessage;

    // Wait for WASM to load (worker posts { type: 'init', success } on load)
    await new Promise((resolve, reject) => {
      const original = worker.onmessage;
      worker.onmessage = function(e) {
        if (e.data.type === 'init') {
          worker.onmessage = original;
          if (e.data.success) resolve();
          else reject(new Error(e.data.error));
        }
      };
    });

    // Request catalog
    const response = await workerSend('init', {});
    catalog = response.catalog;
    buildMenusFromCatalog();
    console.log('Effect catalog loaded:', catalog);
  }

  // =====================================================================
  //  Catalog-driven UI
  // =====================================================================

  // Rebuild "Add Effect" menus from the WASM catalog
  function buildMenusFromCatalog() {
    if (!catalog) return;

    buildMenu(gradientStack, 'gradientEffectMenu', catalog.gradient);
    buildMenu(erosionStack, 'erosionEffectMenu', catalog.erosion);
  }

  function buildMenu(stackEl, menuId, stackCatalog) {
    const menu = document.getElementById(menuId);
    if (!menu || !stackCatalog) return;

    // Clear existing menu buttons
    menu.innerHTML = '';

    // Add groups
    if (stackCatalog.groups) {
      for (const group of stackCatalog.groups) {
        const btn = document.createElement('button');
        btn.textContent = group.name;
        btn.setAttribute('data-group', JSON.stringify(group));
        btn.addEventListener('click', (e) => {
          e.stopPropagation();
          menu.classList.remove('open');
          addGroupEffect(stackEl, group);
        });
        menu.appendChild(btn);
      }
    }

    // Add standalone effects
    if (stackCatalog.standalone) {
      for (const effect of stackCatalog.standalone) {
        const btn = document.createElement('button');
        btn.textContent = effect.name;
        btn.setAttribute('data-effect-id', effect.id);
        btn.addEventListener('click', (e) => {
          e.stopPropagation();
          menu.classList.remove('open');
          addStandaloneEffect(stackEl, effect);
        });
        menu.appendChild(btn);
      }
    }
  }

  // Create a standalone effect item from catalog definition
  function addStandaloneEffect(stackEl, effectDef) {
    const item = buildEffectItemFromDef(effectDef);
    const wrap = stackEl.querySelector('.add-effect-wrap');
    stackEl.insertBefore(item, wrap);
    wireEffectItem(item);
    scheduleProcessStack(stackTypeFor(stackEl));
  }

  // Create a group effect (single card with sub-sections)
  function addGroupEffect(stackEl, groupDef) {
    const item = document.createElement('div');
    item.className = 'effect-item effect-group';
    item.setAttribute('data-expanded', 'true');

    const effectIds = groupDef.effects.map(e => e.id);
    item.setAttribute('data-group-ids', JSON.stringify(effectIds));

    let bodyHTML = '';
    for (const subEffect of groupDef.effects) {
      bodyHTML += '<div class="group-section" data-effect-id="' + subEffect.id + '">';
      bodyHTML += '<div class="group-section-label">' + escapeHtml(subEffect.name) + '</div>';
      bodyHTML += buildParamsHTML(subEffect.params);
      bodyHTML += '</div>';
    }

    item.innerHTML =
      '<div class="effect-header">' +
        '<label class="toggle"><input type="checkbox" checked><span class="toggle-track"></span></label>' +
        '<span class="effect-name">' + escapeHtml(groupDef.name) + '</span>' +
        '<span class="effect-chevron"><svg viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><path d="M3 4.5l3 3 3-3"/></svg></span>' +
        '<button class="btn-icon effect-remove" title="Remove"><svg viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><path d="M3 3l6 6M9 3l-6 6"/></svg></button>' +
      '</div>' +
      '<div class="effect-body">' + bodyHTML + '</div>';

    const wrap = stackEl.querySelector('.add-effect-wrap');
    stackEl.insertBefore(item, wrap);
    wireEffectItem(item);
    scheduleProcessStack(stackTypeFor(stackEl));
  }

  function buildEffectItemFromDef(effectDef) {
    const item = document.createElement('div');
    item.className = 'effect-item';
    item.setAttribute('data-expanded', 'true');
    item.setAttribute('data-effect-id', effectDef.id);

    item.innerHTML =
      '<div class="effect-header">' +
        '<label class="toggle"><input type="checkbox" checked><span class="toggle-track"></span></label>' +
        '<span class="effect-name">' + escapeHtml(effectDef.name) + '</span>' +
        '<span class="effect-chevron"><svg viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><path d="M3 4.5l3 3 3-3"/></svg></span>' +
        '<button class="btn-icon effect-remove" title="Remove"><svg viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><path d="M3 3l6 6M9 3l-6 6"/></svg></button>' +
      '</div>' +
      '<div class="effect-body">' + buildParamsHTML(effectDef.params) + '</div>';

    return item;
  }

  function buildParamsHTML(params) {
    let html = '';
    for (const p of params) {
      if (p.type === 'float') {
        const unpack = p.unpack || 'linear';
        const defaultU8 = packFunctions[unpack](p.default, p.min, p.max);
        const displayVal = formatParamValue(p.default);
        html +=
          '<div class="param">' +
            '<div class="param-label"><label>' + escapeHtml(p.name) + '</label>' +
            '<span class="param-value">' + displayVal + '</span></div>' +
            '<input type="range"' +
              ' min="0"' +
              ' max="255"' +
              ' step="1"' +
              ' value="' + defaultU8 + '"' +
              ' data-param-name="' + escapeHtml(p.name) + '"' +
              ' data-unpack="' + unpack + '"' +
              ' data-min="' + p.min + '"' +
              ' data-max="' + p.max + '">' +
          '</div>';
      } else if (p.type === 'enum') {
        html +=
          '<div class="param">' +
            '<div class="param-label"><label>' + escapeHtml(p.name) + '</label></div>' +
            '<select data-param-name="' + escapeHtml(p.name) + '">';
        for (let i = 0; i < p.options.length; i++) {
          const sel = i === p.default ? ' selected' : '';
          html += '<option value="' + i + '"' + sel + '>' + escapeHtml(p.options[i]) + '</option>';
        }
        html += '</select></div>';
      } else if (p.type === 'color_ramp') {
        // Generate unique ID for grapick instance
        const grapickId = 'grapick-' + (++grapickIdCounter);
        html +=
          '<div class="param param-color-ramp" data-param-name="' + escapeHtml(p.name) + '">' +
            '<div class="param-label"><label>' + escapeHtml(p.name) + '</label></div>' +
            '<div class="grapick-container" id="' + grapickId + '"></div>' +
          '</div>';
      }
    }
    return html;
  }

  // =====================================================================
  //  Stack serialization (DOM -> worker message)
  // =====================================================================

  // Read the current stack DOM and serialize enabled effects for the worker.
  function serializeStack(stackEl) {
    const effects = [];
    const items = stackEl.querySelectorAll('.effect-item');

    for (const item of items) {
      // Skip disabled effects
      const toggle = item.querySelector('.effect-header input[type="checkbox"]');
      if (toggle && !toggle.checked) continue;

      // Group effects: serialize each sub-effect
      if (item.classList.contains('effect-group')) {
        const sections = item.querySelectorAll('.group-section');
        for (const section of sections) {
          const effectId = parseInt(section.getAttribute('data-effect-id'), 10);
          if (isNaN(effectId)) continue;
          const params = collectParams(section);
          effects.push({ effectId, params: new Uint8Array(params) });
        }
        continue;
      }

      // Standalone effect
      const effectId = parseInt(item.getAttribute('data-effect-id'), 10);
      if (isNaN(effectId)) continue; // no ID = hardcoded placeholder, skip
      const params = collectParams(item.querySelector('.effect-body'));
      effects.push({ effectId, params: new Uint8Array(params) });
    }

    return effects;
  }

  // Collect all param values from a container element as a flat uint8 array.
  function collectParams(container) {
    if (!container) return [];
    const values = [];

    const params = container.querySelectorAll('.param');
    for (const param of params) {
      // Float slider - value is already 0-255
      const slider = param.querySelector('input[type="range"]');
      if (slider) {
        values.push(parseInt(slider.value));
        continue;
      }

      // Enum dropdown - direct index
      const select = param.querySelector('select');
      if (select) {
        values.push(parseInt(select.value));
        continue;
      }

      // Color ramp — extract from grapick instance, convert to uint8
      if (param.classList.contains('param-color-ramp')) {
        const container = param.querySelector('.grapick-container');
        const gp = container ? grapickInstances[container.id] : null;
        if (gp) {
          const handlers = gp.getHandlers();
          values.push(handlers.length); // stop count (uint8, max 64)
          for (const h of handlers) {
            const pos = h.getPosition() / 100; // grapick uses 0-100, we need 0-1
            const rgba = parseColor(h.getColor());
            values.push(Math.round(pos * 255));
            values.push(Math.round(rgba.r * 255));
            values.push(Math.round(rgba.g * 255));
            values.push(Math.round(rgba.b * 255));
            values.push(Math.round(rgba.a * 255));
          }
        } else {
          // Fallback: 2-stop black-to-white (uint8 values)
          values.push(2);
          values.push(0, 0, 0, 0, 255);
          values.push(255, 255, 255, 255, 255);
        }
        continue;
      }
    }

    return values;
  }

  // =====================================================================
  //  Process stack (debounced)
  // =====================================================================

  function scheduleProcessStack(stackType) {
    if (!worker || !catalog) return; // WASM not ready

    clearTimeout(processDebounceTimers[stackType]);
    processDebounceTimers[stackType] = setTimeout(() => {
      sendProcessStack(stackType);
    }, 50);
  }

  function sendProcessStack(stackType) {
    const stackEl = stackElFor(stackType);
    const sourceParams = getSourceParams(stackEl);
    const effects = serializeStack(stackEl);

    // If source image is disabled AND no procedural effects, clear preview
    if (!sourceParams.enabled && effects.length === 0) {
      clearPreview(stackType);
      return;
    }

    const id = workerFire('processStack', { stackType, effects, sourceEnabled: sourceParams.enabled });
    latestProcessId[stackType] = id;
  }

  function clearPreview(stackType) {
    const preview = previewElFor(stackType);
    if (!preview) return;
    const canvas = preview.querySelector('canvas');
    if (canvas) {
      const ctx = canvas.getContext('2d');
      ctx.clearRect(0, 0, canvas.width, canvas.height);
    }
    // Clear WebGL texture too
    const glUnit = stackType === STACK_EROSION ? 0 : 1;
    if (gl && glTextures) {
      const tex = glUnit === 0 ? glTextures.erosion : glTextures.gradient;
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([0,0,0,0]));
    }
    glRequestRender();
  }

  function handleStackResult(msg) {
    const preview = previewElFor(msg.stackType);
    if (!preview) return;

    // Skip if WASM returned no image (stub implementation)
    if (!msg.width || !msg.height || !msg.imageData || msg.imageData.length === 0) {
      return;
    }

    // Create or update preview canvas
    let canvas = preview.querySelector('canvas');
    if (!canvas) {
      // Replace placeholder with canvas
      const placeholder = preview.querySelector('.preview-placeholder');
      if (placeholder) placeholder.style.display = 'none';
      canvas = document.createElement('canvas');
      canvas.style.width = '100%';
      canvas.style.height = '100%';
      canvas.style.objectFit = 'contain';
      preview.insertBefore(canvas, preview.firstChild);
    }

    canvas.width = msg.width;
    canvas.height = msg.height;
    const ctx = canvas.getContext('2d');
    const imgData = new ImageData(
      new Uint8ClampedArray(msg.imageData.buffer),
      msg.width,
      msg.height
    );
    ctx.putImageData(imgData, 0, 0);

    // Upload result to WebGL viewport
    uploadImageDataToGL(msg.stackType, msg.imageData, msg.width, msg.height);

    // Download debug files if present (from WASM virtual filesystem)
    if (msg.debugFiles && (window.location.hostname === 'localhost' || window.location.hostname === '127.0.0.1')) {
      const { data, filename } = msg.debugFiles;
      const mimeType = filename.endsWith('.zip') ? 'application/zip' : 'application/octet-stream';
      const blob = new Blob([data], { type: mimeType });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = filename;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
      console.log(`Downloaded ${filename} from WASM`);
    }
  }

  // =====================================================================
  //  Auto-config handlers (WASM -> JS via worker)
  // =====================================================================

  function clearAutoEffects(stackType) {
    const stackEl = stackElFor(stackType);
    const autoItems = stackEl.querySelectorAll('.effect-item[data-auto="true"]');
    autoItems.forEach(item => item.remove());
  }

  function pushAutoEffect(stackType, effectId, params) {
    if (!catalog) return;

    // Find effect definition in catalog
    const stackCatalog = stackType === STACK_GRADIENT ? catalog.gradient : catalog.erosion;
    const effectDef = findEffectDef(stackCatalog, effectId);
    if (!effectDef) {
      console.warn('Auto-config: unknown effect id', effectId);
      return;
    }

    const stackEl = stackElFor(stackType);
    const item = buildEffectItemFromDef(effectDef);
    item.setAttribute('data-auto', 'true');

    // Apply param values from WASM
    applyParamValues(item, effectDef.params, params);

    // Insert at position 1 (after source, which is the first child)
    const source = stackEl.querySelector('.effect-item');
    if (source && source.nextSibling) {
      // Find insertion point: after source, after any existing auto effects
      let insertBefore = source.nextSibling;
      while (insertBefore &&
             insertBefore.classList &&
             insertBefore.classList.contains('effect-item') &&
             insertBefore.getAttribute('data-auto') === 'true') {
        insertBefore = insertBefore.nextSibling;
      }
      stackEl.insertBefore(item, insertBefore);
    } else {
      const wrap = stackEl.querySelector('.add-effect-wrap');
      stackEl.insertBefore(item, wrap);
    }

    wireEffectItem(item);
  }

  function setSourceTiming(stackType, fadeInTime, fadeOutTime, totalDuration) {
    // Only update timing for erosion stack (gradient doesn't have fade timing)
    if (stackType !== STACK_EROSION) return;

    // Skip if timing values are invalid (indicated by -1)
    if (totalDuration <= 0 || fadeInTime < 0 || fadeOutTime < 0) return;

    // Update timing variables
    duration = totalDuration;
    fadeInPct = fadeInTime;    // Already a percentage (0-1)
    fadeOutPct = fadeOutTime;  // Already a percentage (0-1)

    // Clamp values to valid range
    if (fadeInPct < 0) fadeInPct = 0;
    if (fadeInPct > 1) fadeInPct = 1;
    if (fadeOutPct < 0) fadeOutPct = 0;
    if (fadeOutPct > 1) fadeOutPct = 1;

    // Update duration slider max if needed
    if (duration > parseFloat(durationSlider.max)) {
      durationSlider.max = Math.ceil(duration * 1.1);  // 10% headroom
    }

    // Update duration slider and display
    durationSlider.value = duration;
    durationVal.textContent = duration.toFixed(2);

    // Reset time if beyond new duration
    if (time > duration) time = 0;

    // Update timeline UI
    renderTimeline();
  }

  function findEffectDef(stackCatalog, effectId) {
    if (stackCatalog.standalone) {
      for (const ef of stackCatalog.standalone) {
        if (ef.id === effectId) return ef;
      }
    }
    if (stackCatalog.groups) {
      for (const group of stackCatalog.groups) {
        for (const ef of group.effects) {
          if (ef.id === effectId) return ef;
        }
      }
    }
    return null;
  }

  function applyParamValues(item, paramDefs, values) {
    const sliders = item.querySelectorAll('input[type="range"]');
    const selects = item.querySelectorAll('select');
    let vi = 0;
    let si = 0;
    let di = 0;

    for (const p of paramDefs) {
      if (p.type === 'float' && vi < values.length && si < sliders.length) {
        // Values from WASM are now uint8 (0-255), set slider directly
        sliders[si].value = values[vi];
        const display = sliders[si].closest('.param').querySelector('.param-value');
        if (display) display.textContent = formatSliderDisplay(sliders[si]);
        si++;
        vi++;
      } else if (p.type === 'enum' && vi < values.length && di < selects.length) {
        selects[di].value = Math.round(values[vi]);
        di++;
        vi++;
      } else if (p.type === 'color_ramp') {
        // Skip color ramp values for now (placeholder)
        if (vi < values.length) {
          const stopCount = Math.round(values[vi]);
          vi += 1 + stopCount * 5;
        }
      }
    }
  }

  // =====================================================================
  //  Source loading (file -> worker)
  // =====================================================================

  function loadSourceFile(stackType, file) {
    if (!worker) {
      // No WASM — just show thumbnail in the source effect
      addSourceEffectVisual(stackElFor(stackType), file);
      return;
    }

    const reader = new FileReader();
    reader.onload = async (ev) => {
      const arrayBuffer = ev.target.result;

      // Show thumbnail immediately
      addSourceEffectVisual(stackElFor(stackType), file, ev.target.result);

      // Send to worker with quantization (for erosion stack)
      const quantization = stackType === STACK_EROSION ? 1.0 : 0.0; // Default to max quality
      progressModal.classList.add('active');
      try {
        await workerSend('loadSource', {
          stackType,
          fileName: file.name,
          fileData: arrayBuffer,
          quantization
        });
        // Auto-config messages arrive before sourceLoaded resolves,
        // so the stack is already populated when we get here.
        scheduleProcessStack(stackType);
      } catch (err) {
        showToast('Failed to load source: ' + err.message);
      } finally {
        progressModal.classList.remove('active');
      }
    };
    reader.readAsArrayBuffer(file);
  }

  // Add or update the visual source effect at the top of a stack
  function addSourceEffectVisual(stackEl, file, dataUrl) {
    // Check if source effect already exists
    let item = stackEl.querySelector('.effect-item[data-source="true"]');
    const safeName = escapeHtml(file.name);
    const isErosion = stackEl === erosionStack;

    if (!item) {
      item = document.createElement('div');
      item.className = 'effect-item';
      item.setAttribute('data-expanded', 'true');
      item.setAttribute('data-source', 'true');

      // Build HTML - add quantization slider for erosion stack only
      let bodyHtml =
        '<div class="source-thumb"><img alt="source"></div>' +
        '<div class="source-filename">' + safeName + '</div>';

      if (isErosion) {
        bodyHtml +=
          '<div class="param">' +
            '<label>Quantization</label>' +
            '<input type="range" min="0" max="1" step="0.01" value="1" data-source-param="quantization">' +
            '<span class="param-value">1.00</span>' +
          '</div>';
      }

      item.innerHTML =
        '<div class="effect-header">' +
          '<label class="toggle"><input type="checkbox" checked><span class="toggle-track"></span></label>' +
          '<span class="effect-name">Source</span>' +
          '<span class="effect-chevron"><svg viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><path d="M3 4.5l3 3 3-3"/></svg></span>' +
        '</div>' +
        '<div class="effect-body">' + bodyHtml + '</div>';

      // Insert as first child
      stackEl.insertBefore(item, stackEl.firstElementChild);
      wireEffectItem(item);
      wireSourceParams(item);
    } else {
      item.querySelector('.source-filename').innerHTML = safeName;
    }

    // Load thumbnail and upload to WebGL
    const img = item.querySelector('.source-thumb img');
    const glUnit = stackEl === erosionStack ? 0 : 1;
    img.onload = () => uploadImageToGL(img, glUnit);

    if (dataUrl) {
      const blob = new Blob([file], { type: file.type });
      img.src = URL.createObjectURL(blob);
    } else {
      const reader = new FileReader();
      reader.onload = (ev) => { img.src = ev.target.result; };
      reader.readAsDataURL(file);
    }
  }

  // =====================================================================
  //  Render timeline visuals
  // =====================================================================
  function renderTimeline() {
    const inP = fadeInPct * 100;
    const outP = fadeOutFlagPct() * 100;

    flagIn.style.left = inP + '%';
    flagOut.style.left = outP + '%';

    flagInTip.textContent = fadeInSeconds().toFixed(2) + 's';
    flagOutTip.textContent = fadeOutSeconds().toFixed(2) + 's';

    zoneIn.style.left = '0';
    zoneIn.style.width = inP + '%';

    zoneOut.style.right = '0';
    zoneOut.style.width = (100 - outP) + '%';

    if (outP > inP) {
      zoneActive.style.left = inP + '%';
      zoneActive.style.width = (outP - inP) + '%';
      zoneActive.style.display = '';
    } else {
      zoneActive.style.display = 'none';
    }

    playhead.style.left = (timePct() * 100) + '%';

    timeCurrent.textContent = time.toFixed(2);
    durationText.textContent = duration.toFixed(2) + 's';

    glRequestRender();
  }

  // =====================================================================
  //  Flag dragging
  // =====================================================================
  let dragging = null;

  function getTrackPct(clientX) {
    const rect = timelineTrack.getBoundingClientRect();
    return Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
  }

  flagIn.addEventListener('mousedown', (e) => {
    e.preventDefault();
    dragging = 'flagIn';
    flagIn.classList.add('dragging');
  });

  flagOut.addEventListener('mousedown', (e) => {
    e.preventDefault();
    dragging = 'flagOut';
    flagOut.classList.add('dragging');
  });

  playhead.addEventListener('mousedown', (e) => {
    e.preventDefault();
    e.stopPropagation();
    dragging = 'playhead';
    playhead.classList.add('dragging');
  });

  timelineTrack.addEventListener('mousedown', (e) => {
    if (e.target.closest('.timeline-flag') || e.target.closest('.track-playhead')) return;
    const pct = getTrackPct(e.clientX);
    time = pct * duration;
    dragging = 'playhead';
    playhead.classList.add('dragging');
    renderTimeline();
  });

  document.addEventListener('mousemove', (e) => {
    if (!dragging) return;
    const pct = getTrackPct(e.clientX);

    if (dragging === 'flagIn') {
      fadeInPct = pct;
    } else if (dragging === 'flagOut') {
      fadeOutPct = 1 - pct;
    } else if (dragging === 'playhead') {
      time = pct * duration;
    }

    renderTimeline();
  });

  document.addEventListener('mouseup', () => {
    if (dragging) {
      flagIn.classList.remove('dragging');
      flagOut.classList.remove('dragging');
      playhead.classList.remove('dragging');
      dragging = null;
    }
  });

  // =====================================================================
  //  Play / Pause
  // =====================================================================
  playPause.addEventListener('click', () => {
    isPlaying = !isPlaying;
    playIcon.style.display = isPlaying ? 'none' : '';
    pauseIcon.style.display = isPlaying ? '' : 'none';
    playPause.title = isPlaying ? 'Pause' : 'Play';
    if (isPlaying) {
      lastTimestamp = null;
      animFrameId = requestAnimationFrame(tick);
    } else if (animFrameId) {
      cancelAnimationFrame(animFrameId);
      animFrameId = null;
    }
  });

  function tick(ts) {
    if (!lastTimestamp) lastTimestamp = ts;
    const dt = (ts - lastTimestamp) / 1000;
    lastTimestamp = ts;
    time += dt;
    if (duration > 0) time = time % duration;
    renderTimeline();
    if (isPlaying) animFrameId = requestAnimationFrame(tick);
  }

  // =====================================================================
  //  Duration popover
  // =====================================================================
  durationBtn.addEventListener('click', (e) => {
    e.stopPropagation();
    const isOpen = durationPopover.classList.contains('open');
    durationPopover.classList.toggle('open');
    durationBtn.classList.toggle('active');
    if (!isOpen) durationSlider.focus();
  });

  document.addEventListener('click', (e) => {
    if (!durationPopover.contains(e.target) && e.target !== durationBtn) {
      durationPopover.classList.remove('open');
      durationBtn.classList.remove('active');
    }
  });
  durationPopover.addEventListener('click', (e) => e.stopPropagation());

  durationSlider.addEventListener('input', () => {
    duration = parseFloat(durationSlider.value);
    durationVal.textContent = duration.toFixed(2);
    if (time > duration) time = 0;
    renderTimeline();
  });

  // =====================================================================
  //  Effect collapse/expand (initial static items)
  // =====================================================================
  document.querySelectorAll('.effect-header').forEach(header => {
    header.addEventListener('click', (e) => {
      if (e.target.closest('.toggle') || e.target.closest('.effect-remove')) return;
      const item = header.closest('.effect-item');
      const expanded = item.getAttribute('data-expanded') === 'true';
      item.setAttribute('data-expanded', !expanded);
    });
  });

  // =====================================================================
  //  Remove effect (initial static items)
  // =====================================================================
  document.querySelectorAll('.effect-remove').forEach(btn => {
    btn.addEventListener('click', () => {
      const item = btn.closest('.effect-item');
      const stackEl = item.closest('.effect-stack');
      item.remove();
      if (stackEl) scheduleProcessStack(stackTypeFor(stackEl));
    });
  });

  // =====================================================================
  //  Add effect menus (hardcoded fallback + catalog-driven)
  // =====================================================================
  document.querySelectorAll('.add-effect-btn').forEach(btn => {
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      const menuId = btn.getAttribute('data-menu');
      const menu = document.getElementById(menuId);
      const wasOpen = menu.classList.contains('open');
      // Close all menus and remove menu-open from stacks
      document.querySelectorAll('.add-effect-menu').forEach(m => m.classList.remove('open'));
      document.querySelectorAll('.effect-stack').forEach(s => s.classList.remove('menu-open'));
      if (!wasOpen) {
        menu.classList.add('open');
        // Add menu-open to parent stack to disable overflow clipping
        const stack = btn.closest('.effect-stack');
        if (stack) stack.classList.add('menu-open');
      }
    });
  });

  document.addEventListener('click', () => {
    document.querySelectorAll('.add-effect-menu').forEach(m => m.classList.remove('open'));
    document.querySelectorAll('.effect-stack').forEach(s => s.classList.remove('menu-open'));
  });

  // Hardcoded menu buttons (fallback when WASM not available)
  document.querySelectorAll('.add-effect-menu button').forEach(btn => {
    if (btn.hasAttribute('data-group') || btn.hasAttribute('data-effect-id')) return; // catalog-driven
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      const effectName = btn.textContent.trim();
      const menu = btn.closest('.add-effect-menu');
      const stack = menu.closest('.effect-stack');
      const wrap = menu.closest('.add-effect-wrap');
      menu.classList.remove('open');

      const item = document.createElement('div');
      item.className = 'effect-item';
      item.setAttribute('data-expanded', 'true');
      item.innerHTML =
        '<div class="effect-header">' +
          '<label class="toggle"><input type="checkbox" checked><span class="toggle-track"></span></label>' +
          '<span class="effect-name">' + escapeHtml(effectName) + '</span>' +
          '<span class="effect-chevron"><svg viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><path d="M3 4.5l3 3 3-3"/></svg></span>' +
          '<button class="btn-icon effect-remove" title="Remove"><svg viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><path d="M3 3l6 6M9 3l-6 6"/></svg></button>' +
        '</div>' +
        '<div class="effect-body">' +
          '<div class="param"><div class="param-label"><label>Amount</label><span class="param-value">0.50</span></div><input type="range" min="0" max="1" step="0.01" value="0.5"></div>' +
        '</div>';
      stack.insertBefore(item, wrap);
      wireEffectItem(item);
    });
  });

  // =====================================================================
  //  Wire up effect item events (used for both static and dynamic items)
  // =====================================================================
  function wireEffectItem(item) {
    const hdr = item.querySelector('.effect-header');
    hdr.addEventListener('click', (ev) => {
      if (ev.target.closest('.toggle') || ev.target.closest('.effect-remove')) return;
      item.setAttribute('data-expanded', item.getAttribute('data-expanded') !== 'true');
    });

    const removeBtn = item.querySelector('.effect-remove');
    if (removeBtn) {
      removeBtn.addEventListener('click', () => {
        const stackEl = item.closest('.effect-stack');
        item.remove();
        if (stackEl) scheduleProcessStack(stackTypeFor(stackEl));
      });
    }

    // Param sliders — update display and trigger reprocess
    // Skip source params (handled separately by wireSourceParams)
    item.querySelectorAll('.param').forEach(param => {
      const sl = param.querySelector('input[type="range"]');
      const vl = param.querySelector('.param-value');
      if (sl && !sl.hasAttribute('data-source-param')) {
        sl.addEventListener('input', () => {
          if (vl) vl.textContent = formatSliderDisplay(sl);
          const stackEl = item.closest('.effect-stack');
          if (stackEl) scheduleProcessStack(stackTypeFor(stackEl));
        });
      }

      const sel = param.querySelector('select');
      if (sel) {
        sel.addEventListener('change', () => {
          const stackEl = item.closest('.effect-stack');
          if (stackEl) scheduleProcessStack(stackTypeFor(stackEl));
        });
      }
    });

    // Toggle checkbox — trigger reprocess
    const toggle = item.querySelector('.effect-header input[type="checkbox"]');
    if (toggle) {
      toggle.addEventListener('change', () => {
        const stackEl = item.closest('.effect-stack');
        if (stackEl) scheduleProcessStack(stackTypeFor(stackEl));
      });
    }

    // Initialize any color ramp widgets (grapick)
    initColorRamps(item);
  }

  // Debounce timers for source param updates (separate from processStack debounce)
  let sourceParamDebounce = { 0: null, 1: null };

  // Wire source-specific parameter sliders (e.g., quantization)
  function wireSourceParams(item) {
    const slider = item.querySelector('input[data-source-param="quantization"]');
    if (!slider) return;

    const display = slider.parentElement.querySelector('.param-value');

    // Helper to send the update
    function sendQuantizationUpdate() {
      const stackEl = item.closest('.effect-stack');
      if (!stackEl || !worker) return;

      const stackType = stackTypeFor(stackEl);
      const quantization = parseFloat(slider.value);

      workerFire('updateSourceParams', { stackType, quantization });
      scheduleProcessStack(stackType);
    }

    // Live update on input with debouncing
    slider.addEventListener('input', () => {
      if (display) display.textContent = formatParamValue(parseFloat(slider.value));

      const stackEl = item.closest('.effect-stack');
      if (!stackEl || !worker) return;

      const stackType = stackTypeFor(stackEl);

      // Debounce the source param update (30ms for responsive feel)
      clearTimeout(sourceParamDebounce[stackType]);
      sourceParamDebounce[stackType] = setTimeout(sendQuantizationUpdate, 30);
    });

    // On release, cancel debounce and send immediately
    slider.addEventListener('change', () => {
      const stackEl = item.closest('.effect-stack');
      if (!stackEl || !worker) return;

      const stackType = stackTypeFor(stackEl);
      clearTimeout(sourceParamDebounce[stackType]);
      sendQuantizationUpdate();
    });
  }

  // Get source parameters for a stack (quantization, enabled state)
  function getSourceParams(stackEl) {
    const sourceItem = stackEl.querySelector('.effect-item[data-source="true"]');
    if (!sourceItem) return { enabled: false, quantization: 1.0 };

    const toggle = sourceItem.querySelector('.effect-header input[type="checkbox"]');
    const quantSlider = sourceItem.querySelector('input[data-source-param="quantization"]');

    return {
      enabled: toggle ? toggle.checked : true,
      quantization: quantSlider ? parseFloat(quantSlider.value) : 1.0
    };
  }

  // =====================================================================
  //  Param slider displays (initial static items)
  // =====================================================================
  document.querySelectorAll('.effect-body .param').forEach(param => {
    const slider = param.querySelector('input[type="range"]');
    const display = param.querySelector('.param-value');
    if (slider && display) {
      slider.addEventListener('input', () => {
        display.textContent = formatSliderDisplay(slider);
      });
    }
  });

  // =====================================================================
  //  Copy Shader
  // =====================================================================
  copyShader.addEventListener('click', () => {
    fetch('shader_to_be_copied.glsl')
      .then(r => {
        if (!r.ok) throw new Error(r.statusText);
        return r.text();
      })
      .then(code => navigator.clipboard.writeText(code))
      .then(() => showToast('Shader code copied'))
      .catch(() => showToast('Failed to copy shader'));
  });

  // =====================================================================
  //  Toast
  // =====================================================================
  function showToast(msg) {
    toast.textContent = msg;
    toast.classList.add('show');
    setTimeout(() => toast.classList.remove('show'), 2000);
  }

  // =====================================================================
  //  Effect stack drag-and-drop (source files)
  // =====================================================================
  function setupStackDrop(stackEl, stackType) {
    stackEl.addEventListener('dragover', (e) => {
      if (!hasImageFile(e)) return;
      e.preventDefault();
      stackEl.classList.add('drag-over');
    });

    stackEl.addEventListener('dragleave', (e) => {
      if (e.relatedTarget && stackEl.contains(e.relatedTarget)) return;
      stackEl.classList.remove('drag-over');
    });

    stackEl.addEventListener('drop', (e) => {
      e.preventDefault();
      stackEl.classList.remove('drag-over');
      const file = getImageFile(e);
      if (file) loadSourceFile(stackType, file);
    });
  }

  function hasImageFile(e) {
    return e.dataTransfer.types.includes('Files');
  }

  function getImageFile(e) {
    for (const file of e.dataTransfer.files) {
      if (file.type.startsWith('image/')) return file;
    }
    return null;
  }

  setupStackDrop(gradientStack, STACK_GRADIENT);
  setupStackDrop(erosionStack, STACK_EROSION);

  // =====================================================================
  //  Import buttons (open file dialog)
  // =====================================================================
  document.querySelectorAll('[data-import]').forEach(btn => {
    btn.addEventListener('click', () => {
      const type = btn.getAttribute('data-import');
      const stackType = type === 'gradient' ? STACK_GRADIENT : STACK_EROSION;
      const input = document.createElement('input');
      input.type = 'file';
      input.accept = 'image/*';
      input.onchange = (ev) => {
        if (ev.target.files[0]) loadSourceFile(stackType, ev.target.files[0]);
      };
      input.click();
    });
  });

  // =====================================================================
  //  Download texture buttons
  // =====================================================================
  function downloadTexture(previewEl, filename) {
    console.log('downloadTexture called', previewEl, filename);
    const canvas = previewEl.querySelector('canvas');
    console.log('canvas found:', canvas);
    if (!canvas) {
      console.warn('No texture to download - no canvas element');
      return;
    }
    const link = document.createElement('a');
    link.download = filename;
    link.href = canvas.toDataURL('image/png');
    console.log('triggering download, data URL length:', link.href.length);
    link.click();
  }

  // Attach download handlers to preview action buttons
  console.log('gradientPreview:', gradientPreview);
  console.log('erosionPreview:', erosionPreview);

  const gradientDownloadBtn = gradientPreview.querySelector('.preview-actions .btn-icon');
  const erosionDownloadBtn = erosionPreview.querySelector('.preview-actions .btn-icon');

  console.log('gradientDownloadBtn:', gradientDownloadBtn);
  console.log('erosionDownloadBtn:', erosionDownloadBtn);

  if (gradientDownloadBtn) {
    gradientDownloadBtn.addEventListener('click', (e) => {
      console.log('gradient download button clicked');
      e.stopPropagation();
      downloadTexture(gradientPreview, 'gradient_texture.png');
    });
  } else {
    console.warn('Could not find gradient download button');
  }

  if (erosionDownloadBtn) {
    erosionDownloadBtn.addEventListener('click', (e) => {
      console.log('erosion download button clicked');
      e.stopPropagation();
      downloadTexture(erosionPreview, 'erosion_texture.png');
    });
  } else {
    console.warn('Could not find erosion download button');
  }

  // =====================================================================
  //  Utility
  // =====================================================================
  function escapeHtml(str) {
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function formatParamValue(v) {
    const abs = Math.abs(v);
    if (abs < 0.1) return v.toFixed(3);
    if (abs < 10) return v.toFixed(2);
    return v.toFixed(0);
  }

  // Parse CSS color string to {r,g,b,a} in 0-1 range
  function parseColor(colorStr) {
    // Handle rgba(r,g,b,a) format
    const rgbaMatch = colorStr.match(/rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*([\d.]+))?\)/);
    if (rgbaMatch) {
      return {
        r: parseInt(rgbaMatch[1]) / 255,
        g: parseInt(rgbaMatch[2]) / 255,
        b: parseInt(rgbaMatch[3]) / 255,
        a: rgbaMatch[4] !== undefined ? parseFloat(rgbaMatch[4]) : 1
      };
    }
    // Handle hex format
    const hexMatch = colorStr.match(/^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})?$/i);
    if (hexMatch) {
      return {
        r: parseInt(hexMatch[1], 16) / 255,
        g: parseInt(hexMatch[2], 16) / 255,
        b: parseInt(hexMatch[3], 16) / 255,
        a: hexMatch[4] !== undefined ? parseInt(hexMatch[4], 16) / 255 : 1
      };
    }
    // Fallback: black
    return { r: 0, g: 0, b: 0, a: 1 };
  }

  // Shared Coloris input and active handler tracking
  let sharedColorisInput = null;
  let activeGrapickHandler = null;

  // Initialize Coloris with dark theme and alpha support
  function initColoris() {
    if (sharedColorisInput) return;

    if (typeof Coloris === 'undefined') {
      console.error('Coloris is not loaded!');
      return;
    }

    // Create a shared hidden input for Coloris
    sharedColorisInput = document.createElement('input');
    sharedColorisInput.type = 'text';
    sharedColorisInput.setAttribute('data-coloris', '');
    sharedColorisInput.style.cssText = `
      position: fixed;
      opacity: 0;
      pointer-events: none;
      width: 1px;
      height: 1px;
    `;
    document.body.appendChild(sharedColorisInput);

    Coloris({
      el: '[data-coloris]',
      wrap: false,
      themeMode: 'dark',
      alpha: true,
      format: 'rgb',
      formatToggle: false,
      swatches: [
        'rgba(0, 0, 0, 1)',
        'rgba(255, 255, 255, 1)',
        'rgba(255, 0, 0, 1)',
        'rgba(0, 255, 0, 1)',
        'rgba(0, 0, 255, 1)',
        'rgba(255, 255, 0, 1)',
        'rgba(255, 0, 255, 1)',
        'rgba(0, 255, 255, 1)'
      ]
    });

    // When color changes, update the active grapick handler
    sharedColorisInput.addEventListener('input', () => {
      if (activeGrapickHandler) {
        activeGrapickHandler.setColor(sharedColorisInput.value);
        // Update the swatch visual
        const swatch = activeGrapickHandler.getEl().querySelector('.grapick-color-swatch');
        if (swatch) swatch.style.backgroundColor = sharedColorisInput.value;
      }
    });

    sharedColorisInput.addEventListener('change', () => {
      if (activeGrapickHandler) {
        activeGrapickHandler.setColor(sharedColorisInput.value);
        const swatch = activeGrapickHandler.getEl().querySelector('.grapick-color-swatch');
        if (swatch) swatch.style.backgroundColor = sharedColorisInput.value;
      }
    });
  }

  // Open Coloris for a specific grapick handler
  function openColorisForHandler(handler, anchorEl) {
    if (!sharedColorisInput) return;

    activeGrapickHandler = handler;

    // Set current color
    sharedColorisInput.value = handler.getColor() || 'rgba(0,0,0,1)';

    // Position near the anchor element
    const rect = anchorEl.getBoundingClientRect();
    sharedColorisInput.style.left = rect.left + 'px';
    sharedColorisInput.style.top = rect.bottom + 'px';

    // Trigger Coloris by clicking the input
    sharedColorisInput.click();
  }

  // Initialize grapick color ramp widgets on an effect item
  function initColorRamps(item) {
    initColoris();

    const containers = item.querySelectorAll('.grapick-container');
    for (const container of containers) {
      if (grapickInstances[container.id]) continue; // Already initialized

      const gp = new Grapick({
        el: container,
        // Use a simple div as color swatch instead of input
        colorEl: '<div class="grapick-color-swatch"></div>'
      });

      // Set up color picker integration for each handler
      gp.setColorPicker(handler => {
        const el = handler.getEl();
        const swatch = el.querySelector('.grapick-color-swatch');

        if (swatch) {
          // Set initial color
          const currentColor = handler.getColor() || 'rgba(0,0,0,1)';
          swatch.style.cssText = `
            background-color: ${currentColor};
            width: 14px;
            height: 14px;
            border: 2px solid #ffffff;
            border-radius: 50%;
            cursor: pointer;
            margin-left: -7px;
            box-shadow: 0 2px 6px rgba(0,0,0,0.4);
          `;

          // Open Coloris when swatch is clicked
          swatch.addEventListener('click', (e) => {
            e.stopPropagation();
            openColorisForHandler(handler, swatch);
          });
        }

        // Return cleanup function
        return () => {
          if (activeGrapickHandler === handler) {
            activeGrapickHandler = null;
          }
        };
      });

      // Default: black to white gradient (fully opaque)
      gp.addHandler(0, 'rgba(0,0,0,1)');
      gp.addHandler(100, 'rgba(255,255,255,1)');

      // Store instance
      grapickInstances[container.id] = gp;

      // Trigger reprocess on change
      gp.on('change', () => {
        const stackEl = item.closest('.effect-stack');
        if (stackEl) scheduleProcessStack(stackTypeFor(stackEl));
      });
    }
  }

  // Helper: convert RGB (0-1) to hex
  function rgbToHex(r, g, b) {
    const toHex = (v) => {
      const h = Math.round(v * 255).toString(16);
      return h.length === 1 ? '0' + h : h;
    };
    return '#' + toHex(r) + toHex(g) + toHex(b);
  }

  // =====================================================================
  //  WebGL Viewport
  // =====================================================================
  let gl = null;
  let glProgram = null;
  let glTextures = {};
  let glUniforms = {};
  let glReady = false;

  async function initGL() {
    const canvas = document.getElementById('glCanvas');
    gl = canvas.getContext('webgl2', { alpha: false });
    if (!gl) {
      console.warn('WebGL2 not available');
      return;
    }

    // Load shaders from the project's shader directory
    const [vertSrc, fragSrc] = await Promise.all([
      fetch('shaders/erosion.vert').then(r => r.text()),
      fetch('shaders/erosion.frag').then(r => r.text()),
    ]);

    // Compile shaders
    const vs = glCompileShader(gl.VERTEX_SHADER, vertSrc);
    const fs = glCompileShader(gl.FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) return;

    // Link program
    glProgram = gl.createProgram();
    gl.attachShader(glProgram, vs);
    gl.attachShader(glProgram, fs);
    gl.linkProgram(glProgram);
    if (!gl.getProgramParameter(glProgram, gl.LINK_STATUS)) {
      console.error('Shader link error:', gl.getProgramInfoLog(glProgram));
      return;
    }
    gl.useProgram(glProgram);

    // Attribute locations
    const aPos = gl.getAttribLocation(glProgram, 'a_position');
    const aTex = gl.getAttribLocation(glProgram, 'a_texCoord');

    // Uniform locations
    glUniforms = {
      viewportSize:      gl.getUniformLocation(glProgram, 'u_viewportSize'),
      erosionTexture:    gl.getUniformLocation(glProgram, 'u_erosionTexture'),
      gradient:          gl.getUniformLocation(glProgram, 'u_gradient'),
      fadeInDuration:    gl.getUniformLocation(glProgram, 'u_fadeInDuration'),
      fadeOutDuration:   gl.getUniformLocation(glProgram, 'u_fadeOutDuration'),
      animationDuration: gl.getUniformLocation(glProgram, 'u_animationDuration'),
      time:              gl.getUniformLocation(glProgram, 'u_time'),
    };

    // Fullscreen quad vertex buffer (position XY + texcoord UV, triangle strip)
    const verts = new Float32Array([
      -1, -1, 0, 1,
       1, -1, 1, 1,
      -1,  1, 0, 0,
       1,  1, 1, 0,
    ]);
    const vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

    const stride = 4 * 4;
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, stride, 0);
    gl.enableVertexAttribArray(aTex);
    gl.vertexAttribPointer(aTex, 2, gl.FLOAT, false, stride, 8);

    // Placeholder textures (1x1 black)
    glTextures.erosion  = glCreatePlaceholder();
    glTextures.gradient = glCreatePlaceholder();
    gl.uniform1i(glUniforms.erosionTexture, 0);
    gl.uniform1i(glUniforms.gradient, 1);

    // Track canvas pixel dimensions via ResizeObserver
    const frame = canvas.closest('.gl-canvas-frame');
    if (frame) {
      const ro = new ResizeObserver(() => {
        const rect = frame.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        canvas.width  = Math.round(rect.width * dpr);
        canvas.height = Math.round(rect.height * dpr);
        glRequestRender();
      });
      ro.observe(frame);
    }

    glReady = true;
    requestAnimationFrame(glLoop);
  }

  function glCompileShader(type, src) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
      console.error('Shader compile error:', gl.getShaderInfoLog(s));
      gl.deleteShader(s);
      return null;
    }
    return s;
  }

  function glCreatePlaceholder() {
    const t = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
      new Uint8Array([0, 0, 0, 255]));
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    return t;
  }

  function uploadImageToGL(image, textureUnit) {
    if (!gl || !glReady) return;
    const tex = textureUnit === 0 ? glTextures.erosion : glTextures.gradient;
    gl.activeTexture(gl.TEXTURE0 + textureUnit);
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
    gl.generateMipmap(gl.TEXTURE_2D);
    glRequestRender();
  }

  function uploadImageDataToGL(stackType, data, width, height) {
    if (!gl || !glReady) return;
    const unit = stackType === STACK_EROSION ? 0 : 1;
    const tex = unit === 0 ? glTextures.erosion : glTextures.gradient;
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0,
      gl.RGBA, gl.UNSIGNED_BYTE, data);
    gl.generateMipmap(gl.TEXTURE_2D);
    glRequestRender();
  }

  function glLoop() {
    if (!glReady) return;
    // Only render when playing or when something changed
    if (isPlaying || glNeedsRender) {
      glRender();
      glNeedsRender = false;
    }
    requestAnimationFrame(glLoop);
  }

  function glRequestRender() {
    glNeedsRender = true;
  }

  function glRender() {
    const c = gl.canvas;
    gl.viewport(0, 0, c.width, c.height);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(glProgram);

    gl.uniform2f(glUniforms.viewportSize, c.width, c.height);
    gl.uniform1f(glUniforms.fadeInDuration, fadeInPct * duration);
    gl.uniform1f(glUniforms.fadeOutDuration, fadeOutPct * duration);
    gl.uniform1f(glUniforms.animationDuration, duration);
    gl.uniform1f(glUniforms.time, time);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, glTextures.erosion);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, glTextures.gradient);

    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
  }

  // =====================================================================
  //  URL State Persistence
  // =====================================================================

  const URL_DEFAULTS = {
    duration: 2.0,
    fadeIn: 0.25,
    fadeOut: 0.25
  };

  let demoManifest = null;  // Loaded on demand

  // Parse URL query params into state object
  function parseUrlState() {
    const params = new URLSearchParams(window.location.search);
    const state = {};

    // Decode stacks (supports both old JSON and new byte format)
    const gParam = params.get('g');
    const eParam = params.get('e');

    if (gParam) {
      try {
        const decoded = atob(gParam);
        // Check if it looks like JSON (starts with '[')
        if (decoded.startsWith('[')) {
          // Old JSON format - for backwards compatibility
          state.gradient = JSON.parse(decoded);
        } else {
          // New byte format
          state.gradientBytes = base64ToBytes(gParam);
        }
      } catch (err) {
        console.warn('Failed to parse gradient stack from URL:', err);
      }
    }

    if (eParam) {
      try {
        const decoded = atob(eParam);
        if (decoded.startsWith('[')) {
          state.erosion = JSON.parse(decoded);
        } else {
          state.erosionBytes = base64ToBytes(eParam);
        }
      } catch (err) {
        console.warn('Failed to parse erosion stack from URL:', err);
      }
    }

    // Timeline params
    const d = params.get('d');
    const fi = params.get('fi');
    const fo = params.get('fo');

    if (d !== null) state.duration = parseFloat(d);
    if (fi !== null) state.fadeIn = parseFloat(fi);
    if (fo !== null) state.fadeOut = parseFloat(fo);

    // Demo image
    const demo = params.get('demo');
    if (demo !== null) {
      state.demo = demo.trim();  // Empty/whitespace = picker mode
    }

    return state;
  }

  // Encode stack to compact bytes: [count, effectId, paramCount, ...params, ...]
  function encodeStackToBytes(effects) {
    const bytes = [effects.length];
    for (const eff of effects) {
      // Effect ID as 1 byte (IDs are 0x10-0x31)
      bytes.push(eff.effectId);
      // Param count
      bytes.push(eff.params.length);
      // Params (already uint8)
      for (const p of eff.params) {
        bytes.push(p);
      }
    }
    return new Uint8Array(bytes);
  }

  // Decode bytes back to effect stack
  function decodeStackFromBytes(bytes, stackCatalog) {
    const effects = [];
    let i = 0;
    const count = bytes[i++];

    for (let e = 0; e < count && i < bytes.length; e++) {
      const effectId = bytes[i++];
      const paramCount = bytes[i++];
      const params = bytes.slice(i, i + paramCount);
      i += paramCount;

      const effectDef = findEffectDef(stackCatalog, effectId);
      if (effectDef) {
        effects.push({ effectId, params: new Uint8Array(params), def: effectDef });
      } else {
        console.warn(`Unknown effect ID ${effectId} in URL, skipping`);
      }
    }

    return effects;
  }

  function bytesToBase64(bytes) {
    return btoa(String.fromCharCode.apply(null, bytes));
  }

  function base64ToBytes(str) {
    return new Uint8Array(atob(str).split('').map(c => c.charCodeAt(0)));
  }

  // Convert URL format back to effect stack (for old JSON format backwards compatibility)
  function urlFormatToStack(arr, stackCatalog) {
    const effects = [];

    for (const item of arr) {
      if (!Array.isArray(item) || item.length < 1) {
        console.warn('Invalid effect entry in URL, skipping:', item);
        continue;
      }

      const [effectId, ...params] = item;
      const effectDef = findEffectDef(stackCatalog, effectId);

      if (!effectDef) {
        console.warn(`Unknown effect ID ${effectId}, skipping`);
        continue;
      }

      // Count expected params
      let expectedCount = 0;
      for (const p of effectDef.params) {
        if (p.type === 'float' || p.type === 'enum') {
          expectedCount++;
        } else if (p.type === 'color_ramp') {
          // Color ramp: 1 (stop count) + stopCount * 5 (pos, r, g, b, a)
          const stopCount = params[expectedCount] || 0;
          expectedCount += 1 + stopCount * 5;
        }
      }

      if (params.length !== expectedCount) {
        console.warn(`Effect ${effectId} expected ${expectedCount} params, got ${params.length}, skipping`);
        continue;
      }

      // Convert to uint8 for consistency with new format
      effects.push({ effectId, params: new Uint8Array(params), def: effectDef });
    }

    return effects;
  }

  // Serialize current state to URL string
  function serializeUrlState() {
    const params = new URLSearchParams();

    // Serialize stacks as compact bytes
    const gradientEffects = serializeStack(gradientStack);
    const erosionEffects = serializeStack(erosionStack);

    if (gradientEffects.length > 0) {
      const bytes = encodeStackToBytes(gradientEffects);
      params.set('g', bytesToBase64(bytes));
    }

    if (erosionEffects.length > 0) {
      const bytes = encodeStackToBytes(erosionEffects);
      params.set('e', bytesToBase64(bytes));
    }

    // Timeline (only if non-default)
    if (Math.abs(duration - URL_DEFAULTS.duration) > 0.001) {
      params.set('d', duration.toFixed(2));
    }
    if (Math.abs(fadeInPct - URL_DEFAULTS.fadeIn) > 0.001) {
      params.set('fi', fadeInPct.toFixed(3));
    }
    if (Math.abs(fadeOutPct - URL_DEFAULTS.fadeOut) > 0.001) {
      params.set('fo', fadeOutPct.toFixed(3));
    }

    // Demo image (if loaded from demo)
    const currentDemo = getCurrentDemoName();
    if (currentDemo) {
      params.set('demo', currentDemo);
    }

    const queryString = params.toString();
    return queryString ? '?' + queryString : '';
  }

  // Get current demo name if one is loaded
  let loadedDemoName = null;

  function getCurrentDemoName() {
    return loadedDemoName;
  }

  // Apply parsed URL state to the UI
  async function applyUrlState(state) {
    // Apply timeline settings first
    if (state.duration !== undefined && !isNaN(state.duration)) {
      duration = state.duration;
      durationSlider.value = duration;
      durationVal.textContent = duration.toFixed(2);
    }
    if (state.fadeIn !== undefined && !isNaN(state.fadeIn)) {
      fadeInPct = Math.max(0, Math.min(1, state.fadeIn));
    }
    if (state.fadeOut !== undefined && !isNaN(state.fadeOut)) {
      fadeOutPct = Math.max(0, Math.min(1, state.fadeOut));
    }
    renderTimeline();

    // Apply gradient stack (new byte format or old JSON format)
    let hasGradient = false;
    if (state.gradientBytes && catalog) {
      const effects = decodeStackFromBytes(state.gradientBytes, catalog.gradient);
      for (const eff of effects) {
        addEffectFromUrlState(gradientStack, eff);
      }
      hasGradient = effects.length > 0;
    } else if (state.gradient && catalog) {
      // Old JSON format for backwards compatibility
      const effects = urlFormatToStack(state.gradient, catalog.gradient);
      for (const eff of effects) {
        addEffectFromUrlState(gradientStack, eff);
      }
      hasGradient = effects.length > 0;
    }

    // Apply erosion stack (new byte format or old JSON format)
    let hasErosion = false;
    if (state.erosionBytes && catalog) {
      const effects = decodeStackFromBytes(state.erosionBytes, catalog.erosion);
      for (const eff of effects) {
        addEffectFromUrlState(erosionStack, eff);
      }
      hasErosion = effects.length > 0;
    } else if (state.erosion && catalog) {
      // Old JSON format for backwards compatibility
      const effects = urlFormatToStack(state.erosion, catalog.erosion);
      for (const eff of effects) {
        addEffectFromUrlState(erosionStack, eff);
      }
      hasErosion = effects.length > 0;
    }

    // Handle demo image
    if (state.demo !== undefined) {
      await handleDemoParam(state.demo);
    }

    // Trigger processing
    if (hasGradient) scheduleProcessStack(STACK_GRADIENT);
    if (hasErosion) scheduleProcessStack(STACK_EROSION);
  }

  // Add an effect from URL state (similar to addStandaloneEffect but with params)
  function addEffectFromUrlState(stackEl, eff) {
    const { effectId, params, def } = eff;

    // Check if this is part of a group
    const stackCatalog = stackEl === gradientStack ? catalog.gradient : catalog.erosion;
    let isGroupEffect = false;
    let groupDef = null;

    if (stackCatalog.groups) {
      for (const g of stackCatalog.groups) {
        for (const e of g.effects) {
          if (e.id === effectId) {
            isGroupEffect = true;
            groupDef = g;
            break;
          }
        }
        if (isGroupEffect) break;
      }
    }

    if (isGroupEffect && groupDef) {
      // For groups, we need to handle specially - for now, add as standalone
      // (groups would need all their sub-effects in sequence)
      const item = buildEffectItemFromDef(def);
      const wrap = stackEl.querySelector('.add-effect-wrap');
      stackEl.insertBefore(item, wrap);
      applyParamValuesFromArray(item, def.params, Array.from(params));
      wireEffectItem(item);
    } else {
      // Standalone effect
      const item = buildEffectItemFromDef(def);
      const wrap = stackEl.querySelector('.add-effect-wrap');
      stackEl.insertBefore(item, wrap);
      applyParamValuesFromArray(item, def.params, Array.from(params));
      wireEffectItem(item);
    }
  }

  // Apply param values from a flat array (like URL format)
  // Apply param values from a flat uint8 array (from URL)
  function applyParamValuesFromArray(item, paramDefs, values) {
    const sliders = item.querySelectorAll('input[type="range"]');
    const selects = item.querySelectorAll('select');
    let vi = 0;
    let si = 0;
    let di = 0;

    for (const p of paramDefs) {
      if (p.type === 'float' && vi < values.length && si < sliders.length) {
        // Values are now uint8 (0-255), set slider directly
        sliders[si].value = values[vi];
        const display = sliders[si].closest('.param').querySelector('.param-value');
        if (display) display.textContent = formatSliderDisplay(sliders[si]);
        si++;
        vi++;
      } else if (p.type === 'enum' && vi < values.length && di < selects.length) {
        selects[di].value = values[vi];
        di++;
        vi++;
      } else if (p.type === 'color_ramp') {
        // Apply color ramp from uint8 values
        const container = item.querySelector('.grapick-container');
        const gp = container ? grapickInstances[container.id] : null;
        if (gp && vi < values.length) {
          const stopCount = values[vi];
          vi++;

          // Clear existing handlers
          const handlers = gp.getHandlers();
          while (handlers.length > 0) {
            gp.removeHandler(handlers[0]);
          }

          // Add stops from URL (values are uint8: pos, r, g, b, a all 0-255)
          for (let i = 0; i < stopCount && vi + 4 < values.length; i++) {
            const pos = (values[vi] / 255) * 100;  // uint8 -> grapick 0-100
            const r = values[vi + 1];
            const g = values[vi + 2];
            const b = values[vi + 3];
            const a = values[vi + 4] / 255;  // alpha as 0-1 for CSS
            gp.addHandler(pos, `rgba(${r},${g},${b},${a})`);
            vi += 5;
          }
        } else if (vi < values.length) {
          // Skip color ramp values if no grapick instance
          const stopCount = values[vi];
          vi += 1 + stopCount * 5;
        }
      }
    }
  }

  // Load demo manifest
  async function loadDemoManifest() {
    if (demoManifest) return demoManifest;

    try {
      const response = await fetch('demo-images/manifest.json');
      if (!response.ok) throw new Error('Failed to load manifest');
      demoManifest = await response.json();
      return demoManifest;
    } catch (err) {
      console.warn('Could not load demo manifest:', err);
      return null;
    }
  }

  // Handle demo param from URL
  async function handleDemoParam(demoName) {
    const manifest = await loadDemoManifest();
    if (!manifest) return;

    // Empty/whitespace = picker mode - open both dropdowns
    if (!demoName) {
      showDemoDropdown(STACK_EROSION);
      showDemoDropdown(STACK_GRADIENT);
      return;
    }

    // Find and load specific demo
    const demo = manifest.demos.find(d => d.name === demoName);
    if (!demo) {
      console.warn(`Demo image "${demoName}" not found`);
      return;
    }

    await loadDemoImage(demo);
  }

  // Load a demo image by manifest entry
  async function loadDemoImage(demo) {
    try {
      const response = await fetch('demo-images/' + demo.file);
      if (!response.ok) throw new Error('Failed to fetch demo image');

      const blob = await response.blob();
      const file = new File([blob], demo.file, { type: blob.type });

      // Determine which stack based on demo.stack field
      const stackType = demo.stack === 'gradient' ? STACK_GRADIENT : STACK_EROSION;

      loadedDemoName = demo.name;
      loadSourceFile(stackType, file);

      // Update URL without reload
      updateUrlFromState();
    } catch (err) {
      console.warn('Failed to load demo image:', err);
    }
  }

  // Show demo dropdown in a stack's import button area
  function showDemoDropdown(stackType) {
    buildDemoDropdown(stackType);
  }

  // Build demo dropdown menu
  async function buildDemoDropdown(stackType) {
    const manifest = await loadDemoManifest();
    if (!manifest || !manifest.demos || manifest.demos.length === 0) return;

    // Filter demos for this stack type
    const stackName = stackType === STACK_GRADIENT ? 'gradient' : 'erosion';
    const stackDemos = manifest.demos.filter(d => d.stack === stackName);
    if (stackDemos.length === 0) return;  // No demos for this stack

    const panel = stackType === STACK_GRADIENT
      ? document.querySelector('.panel-left')
      : document.querySelector('.panel-right');

    const headerActions = panel.querySelector('.panel-header-actions');
    const importBtn = headerActions.querySelector('[data-import]');

    // Check if dropdown already exists
    let dropdown = headerActions.querySelector('.demo-dropdown');
    if (dropdown) {
      dropdown.classList.toggle('open');
      return;
    }

    // Create dropdown structure
    const wrap = document.createElement('div');
    wrap.className = 'demo-dropdown-wrap';
    wrap.innerHTML = `
      <button class="btn-icon btn-dropdown" title="Import or select demo">
        <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round">
          <path d="M8 10V2M8 2l3 3M8 2L5 5"/>
          <path d="M2 10v3a1 1 0 001 1h10a1 1 0 001-1v-3"/>
        </svg>
        <svg class="dropdown-caret" viewBox="0 0 8 8" fill="currentColor">
          <path d="M0 2l4 4 4-4z"/>
        </svg>
      </button>
      <div class="demo-dropdown">
        <button class="demo-dropdown-item" data-action="import">Import Image...</button>
        <hr class="demo-dropdown-divider">
      </div>
    `;

    // Add demo items (filtered by stack)
    const dropdownMenu = wrap.querySelector('.demo-dropdown');
    for (const demo of stackDemos) {
      const btn = document.createElement('button');
      btn.className = 'demo-dropdown-item';
      btn.setAttribute('data-demo', demo.name);
      btn.textContent = demo.label;
      if (demo.name === loadedDemoName) {
        btn.classList.add('active');
      }
      dropdownMenu.appendChild(btn);
    }

    // Replace import button
    if (importBtn) importBtn.remove();
    headerActions.appendChild(wrap);

    // Wire events
    const toggleBtn = wrap.querySelector('.btn-dropdown');
    toggleBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      dropdownMenu.classList.toggle('open');
    });

    dropdownMenu.addEventListener('click', async (e) => {
      const item = e.target.closest('.demo-dropdown-item');
      if (!item) return;

      e.stopPropagation();
      dropdownMenu.classList.remove('open');

      if (item.getAttribute('data-action') === 'import') {
        // Trigger file picker
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = 'image/*';
        input.onchange = (ev) => {
          if (ev.target.files[0]) {
            loadedDemoName = null;  // Clear demo name for user uploads
            loadSourceFile(stackType, ev.target.files[0]);
            updateUrlFromState();
          }
        };
        input.click();
      } else {
        const demoName = item.getAttribute('data-demo');
        const demo = manifest.demos.find(d => d.name === demoName);
        if (demo) {
          // Update active state
          dropdownMenu.querySelectorAll('.demo-dropdown-item').forEach(i => i.classList.remove('active'));
          item.classList.add('active');
          await loadDemoImage(demo);
        }
      }
    });

    // Close on outside click
    document.addEventListener('click', () => {
      dropdownMenu.classList.remove('open');
    });

    // Auto-open if in picker mode
    const urlState = parseUrlState();
    if (urlState.demo === '') {
      dropdownMenu.classList.add('open');
    }
  }

  // Update URL without page reload
  function updateUrlFromState() {
    const url = window.location.pathname + serializeUrlState();
    history.replaceState(null, '', url);
  }

  // Copy shareable link to clipboard
  async function copyShareableLink() {
    const url = window.location.origin + window.location.pathname + serializeUrlState();
    try {
      await navigator.clipboard.writeText(url);
      showToast('Link copied to clipboard');
    } catch (err) {
      console.error('Failed to copy link:', err);
      showToast('Failed to copy link');
    }
  }

  // Wire share button
  const shareBtn = document.getElementById('shareBtn');
  if (shareBtn) {
    shareBtn.addEventListener('click', copyShareableLink);
  }

  // =====================================================================
  //  Init
  // =====================================================================
  renderTimeline();

  // Initialize WebGL viewport
  initGL().catch(err => {
    console.warn('WebGL init failed:', err.message);
  });

  // Try to connect to WASM worker — non-blocking, UI works without it
  initWorker()
    .then(async () => {
      // Apply URL state after catalog is loaded
      const urlState = parseUrlState();
      if (Object.keys(urlState).length > 0) {
        await applyUrlState(urlState);
      }
      // Build demo dropdowns for both stacks if demos exist
      loadDemoManifest().then(manifest => {
        if (manifest && manifest.demos && manifest.demos.length > 0) {
          buildDemoDropdown(STACK_EROSION);
          buildDemoDropdown(STACK_GRADIENT);
        }
      });
    })
    .catch(err => {
      console.warn('WASM worker not available, running in UI-only mode:', err.message);
      // Still try to apply URL state for timeline settings
      const urlState = parseUrlState();
      if (urlState.duration || urlState.fadeIn || urlState.fadeOut) {
        if (urlState.duration !== undefined) {
          duration = urlState.duration;
          durationSlider.value = duration;
          durationVal.textContent = duration.toFixed(2);
        }
        if (urlState.fadeIn !== undefined) fadeInPct = urlState.fadeIn;
        if (urlState.fadeOut !== undefined) fadeOutPct = urlState.fadeOut;
        renderTimeline();
      }
    });

})();
