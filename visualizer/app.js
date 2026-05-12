const els = {
  traceFile: document.getElementById("traceFile"),
  loadBundledTrace: document.getElementById("loadBundledTrace"),
  traceStatus: document.getElementById("traceStatus"),
  parseMessage: document.getElementById("parseMessage"),
  policy: document.getElementById("policy"),
  blockSize: document.getElementById("blockSize"),
  maxAccesses: document.getElementById("maxAccesses"),
  speed: document.getElementById("speed"),
  speedLabel: document.getElementById("speedLabel"),
  l1SizeKb: document.getElementById("l1SizeKb"),
  l1Assoc: document.getElementById("l1Assoc"),
  l2SizeKb: document.getElementById("l2SizeKb"),
  l2Assoc: document.getElementById("l2Assoc"),
  prepare: document.getElementById("prepare"),
  playPause: document.getElementById("playPause"),
  step: document.getElementById("step"),
  runAll: document.getElementById("runAll"),
  reset: document.getElementById("reset"),
  compare: document.getElementById("compare"),
  comparisonBody: document.getElementById("comparisonBody"),
  metricAccesses: document.getElementById("metricAccesses"),
  metricHits: document.getElementById("metricHits"),
  metricMemory: document.getElementById("metricMemory"),
  metricHitRate: document.getElementById("metricHitRate"),
  metricActiveLevel: document.getElementById("metricActiveLevel"),
  progressText: document.getElementById("progressText"),
  configSummary: document.getElementById("configSummary"),
  progressFill: document.getElementById("progressFill"),
  resultBadge: document.getElementById("resultBadge"),
  currentAccess: document.getElementById("currentAccess"),
  hitRateChart: document.getElementById("hitRateChart"),
  hitRateStats: document.getElementById("hitRateStats"),
  generateElbows: document.getElementById("generateElbows"),
  elbowStatus: document.getElementById("elbowStatus"),
  capacityChart: document.getElementById("capacityChart"),
  associativityChart: document.getElementById("associativityChart"),
  capacityBody: document.getElementById("capacityBody"),
  associativityBody: document.getElementById("associativityBody"),
  hierarchyBody: document.getElementById("hierarchyBody"),
  hierarchyMeta: document.getElementById("hierarchyMeta"),
  activeSetTitle: document.getElementById("activeSetTitle"),
  activeSetMeta: document.getElementById("activeSetMeta"),
  levelTabs: document.getElementById("levelTabs"),
  wayGrid: document.getElementById("wayGrid"),
  heatTitle: document.getElementById("heatTitle"),
  heatMap: document.getElementById("heatMap"),
  heatMeta: document.getElementById("heatMeta"),
  eventLog: document.getElementById("eventLog"),
  nodeL1: document.getElementById("nodeL1"),
  nodeL2: document.getElementById("nodeL2"),
  nodeMemory: document.getElementById("nodeMemory"),
  nodeL1Text: document.getElementById("nodeL1Text"),
  nodeL2Text: document.getElementById("nodeL2Text"),
  nodeMemoryText: document.getElementById("nodeMemoryText"),
};

const POLICIES = ["LRU", "FIFO", "RANDOM", "BELADY"];
const LEVEL_NAMES = ["L1", "L2"];
const HISTORY_LIMIT = 80;
const HEAT_CELL_LIMIT = 640;

const state = {
  trace: [],
  traceName: "",
  config: null,
  levels: [],
  future: new Map(),
  timer: 0,
  pointer: 0,
  totalHits: 0,
  memoryAccesses: 0,
  loads: 0,
  stores: 0,
  activeLevelName: "L1",
  activeSetIndex: null,
  lastEvent: null,
  history: [],
  rateSeries: [],
  prepared: false,
  playing: false,
  runningFast: false,
  playTimer: null,
  rngSeed: 0x5eed1234,
};

function setMessage(message, isError = false) {
  els.parseMessage.textContent = message;
  els.parseMessage.classList.toggle("error", isError);
}

function formatNumber(value) {
  return Number(value || 0).toLocaleString("en-US");
}

function formatPercent(value) {
  return `${Number(value || 0).toFixed(2)}%`;
}

function formatHex(value) {
  if (value === null || value === undefined || value === "") return "-";
  if (typeof value === "bigint") return `0x${value.toString(16)}`;
  if (typeof value === "number") return `0x${value.toString(16)}`;
  if (/^0x/i.test(String(value))) return String(value).toLowerCase();
  return `0x${BigInt(value).toString(16)}`;
}

function compactHex(value) {
  const text = formatHex(value);
  return text.length <= 16 ? text : `${text.slice(0, 8)}...${text.slice(-6)}`;
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function parseTraceLine(line, lineNumber) {
  const trimmed = line.trim();
  if (!trimmed || trimmed.startsWith("#")) return null;

  const parts = trimmed.split(/\s+/);
  const hexIndexes = [];
  let opIndex = -1;
  let size = 0;

  for (let i = 0; i < parts.length; i += 1) {
    if (/^0x[0-9a-f]+$/i.test(parts[i])) hexIndexes.push(i);
    if (/^(L|S|R|W|M|I)$/i.test(parts[i])) opIndex = i;
  }

  if (!hexIndexes.length) return null;

  const addressIndex = hexIndexes.length > 1 ? hexIndexes[1] : hexIndexes[0];
  const ipIndex = hexIndexes.length > 1 ? hexIndexes[0] : -1;
  const sizeToken = [...parts].reverse().find((part) => /^\d+$/.test(part));
  if (sizeToken) size = Number(sizeToken);

  try {
    return {
      lineNumber,
      ip: ipIndex >= 0 ? parts[ipIndex] : "-",
      op: opIndex >= 0 ? parts[opIndex].toUpperCase() : "-",
      address: BigInt(parts[addressIndex]),
      addressText: parts[addressIndex].toLowerCase(),
      size,
    };
  } catch {
    return null;
  }
}

function parseTraceText(text) {
  const lines = text.split(/\r?\n/);
  const trace = [];
  let rejected = 0;

  for (let i = 0; i < lines.length; i += 1) {
    const parsed = parseTraceLine(lines[i], i + 1);
    if (parsed) trace.push(parsed);
    else if (lines[i].trim()) rejected += 1;
  }

  return { trace, rejected };
}

async function loadTraceFromFile(file) {
  if (!file) return;
  stopPlayback();
  setMessage(`Reading ${file.name}...`);
  const text = await file.text();
  const { trace, rejected } = parseTraceText(text);
  acceptTrace(trace, file.name, rejected);
}

async function loadBundledTrace() {
  stopPlayback();
  setMessage("Reading bundled trace file...");

  try {
    const candidates = ["../trace.out", "../trace6.out", "../trace1.out"];
    let loaded = null;

    for (const url of candidates) {
      const response = await fetch(url, { cache: "no-store" });
      if (!response.ok) continue;
      const text = await response.text();
      loaded = { name: url.replace("../", ""), ...parseTraceText(text) };
      break;
    }

    if (!loaded) throw new Error("No bundled trace found");
    acceptTrace(loaded.trace, loaded.name, loaded.rejected);
  } catch {
    setMessage("Could not load a bundled trace. Start the local server or choose a trace file.", true);
  }
}

function acceptTrace(trace, name, rejected) {
  state.trace = trace;
  state.traceName = name;
  state.prepared = false;
  clearSimulationState();

  if (!trace.length) {
    els.traceStatus.textContent = "No valid memory accesses found";
    setMessage("Trace parsing found no address lines.", true);
  } else {
    els.traceStatus.textContent = `${name}: ${formatNumber(trace.length)} accesses loaded`;
    setMessage(`Loaded ${formatNumber(trace.length)} accesses.${rejected ? ` ${formatNumber(rejected)} lines skipped.` : ""}`);
  }

  updateButtons();
  renderAll();
}

function readPositiveInteger(element, label) {
  const value = Number(element.value);
  if (!Number.isInteger(value) || value <= 0) throw new Error(`${label} must be a positive integer.`);
  return value;
}

function readConfig() {
  const policy = els.policy.value;
  const blockSize = readPositiveInteger(els.blockSize, "Block size");
  const maxAccesses = readPositiveInteger(els.maxAccesses, "Max accesses");

  if (!POLICIES.includes(policy)) throw new Error("Choose a supported replacement policy.");

  const levelConfigs = [
    { name: "L1", sizeKb: readPositiveInteger(els.l1SizeKb, "L1 size"), associativity: readPositiveInteger(els.l1Assoc, "L1 associativity") },
    { name: "L2", sizeKb: readPositiveInteger(els.l2SizeKb, "L2 size"), associativity: readPositiveInteger(els.l2Assoc, "L2 associativity") },
  ].map((level) => {
    const sizeBytes = level.sizeKb * 1024;
    const numBlocks = sizeBytes / blockSize;
    if (!Number.isInteger(numBlocks)) throw new Error(`${level.name} size must divide evenly by block size.`);
    if (numBlocks < level.associativity) throw new Error(`${level.name} must contain at least one complete set.`);
    if (numBlocks % level.associativity !== 0) throw new Error(`${level.name} blocks must divide evenly by associativity.`);

    return {
      ...level,
      sizeBytes,
      blockSize,
      numBlocks,
      numSets: numBlocks / level.associativity,
    };
  });

  return {
    policy,
    blockSize,
    limit: Math.min(state.trace.length, maxAccesses),
    levels: levelConfigs,
  };
}

function emptyStats() {
  return { accesses: 0, hits: 0, misses: 0, evictions: 0, loads: 0, stores: 0 };
}

function createLevel(config) {
  return {
    ...config,
    cache: new Map(),
    setStats: new Map(),
    stats: emptyStats(),
  };
}

function clearSimulationState() {
  state.config = null;
  state.levels = [];
  state.future = new Map();
  state.timer = 0;
  state.pointer = 0;
  state.totalHits = 0;
  state.memoryAccesses = 0;
  state.loads = 0;
  state.stores = 0;
  state.activeLevelName = "L1";
  state.activeSetIndex = null;
  state.lastEvent = null;
  state.history = [];
  state.rateSeries = [];
  state.rngSeed = 0x5eed1234;
}

function buildFuture(trace, blockSize, limit) {
  const future = new Map();
  const blockSizeBig = BigInt(blockSize);

  for (let i = 0; i < limit; i += 1) {
    const blockKey = (trace[i].address / blockSizeBig).toString();
    if (!future.has(blockKey)) future.set(blockKey, []);
    future.get(blockKey).push(i);
  }

  return future;
}

function prepareSimulation() {
  stopPlayback();
  if (!state.trace.length) {
    setMessage("Load a trace file first.", true);
    return;
  }

  try {
    const config = readConfig();
    clearSimulationState();
    state.config = config;
    state.levels = config.levels.map(createLevel);
    state.future = buildFuture(state.trace, config.blockSize, config.limit);
    state.prepared = true;
    setMessage(`Prepared ${formatNumber(config.limit)} accesses through L1/L2/L3.`);
  } catch (error) {
    state.prepared = false;
    setMessage(error.message, true);
  }

  updateButtons();
  renderAll();
}

function resetSimulation() {
  if (!state.config) return;
  const previousConfig = JSON.parse(JSON.stringify(state.config));
  stopPlayback();
  clearSimulationState();
  state.config = previousConfig;
  state.levels = previousConfig.levels.map(createLevel);
  state.future = buildFuture(state.trace, previousConfig.blockSize, previousConfig.limit);
  state.prepared = true;
  setMessage("Hierarchy reset.");
  updateButtons();
  renderAll();
}

function markConfigDirty() {
  if (!state.prepared) return;
  stopPlayback();
  state.prepared = false;
  setMessage("Configuration changed. Prepare again to apply it.");
  updateButtons();
}

function decodeAddress(address, level) {
  const blockAddr = address / BigInt(level.blockSize);
  const setIndex = Number(blockAddr % BigInt(level.numSets));
  const tag = blockAddr / BigInt(level.numSets);
  return {
    blockAddr,
    blockKey: blockAddr.toString(),
    setIndex,
    tag,
    tagKey: tag.toString(),
  };
}

function makeEmptySet(ways) {
  return Array.from({ length: ways }, () => ({
    valid: false,
    tag: null,
    tagKey: "",
    blockAddr: null,
    blockKey: "",
    lastUsed: 0,
    insertedAt: 0,
    reads: 0,
    writes: 0,
  }));
}

function getSet(level, setIndex) {
  if (!level.cache.has(setIndex)) level.cache.set(setIndex, makeEmptySet(level.associativity));
  return level.cache.get(setIndex);
}

function getSetStats(level, setIndex) {
  if (!level.setStats.has(setIndex)) {
    level.setStats.set(setIndex, { accesses: 0, hits: 0, misses: 0, evictions: 0 });
  }
  return level.setStats.get(setIndex);
}

function nextUse(blockKey, currentIndex) {
  const positions = state.future.get(blockKey);
  if (!positions) return Infinity;

  let left = 0;
  let right = positions.length;
  while (left < right) {
    const mid = Math.floor((left + right) / 2);
    if (positions[mid] <= currentIndex) left = mid + 1;
    else right = mid;
  }

  return left >= positions.length ? Infinity : positions[left];
}

function randomIndex(max) {
  state.rngSeed = (Math.imul(state.rngSeed, 1664525) + 1013904223) >>> 0;
  return state.rngSeed % max;
}

function chooseVictim(set, policy, currentIndex) {
  if (policy === "LRU") {
    return set.reduce((victim, block, index) => block.lastUsed < set[victim].lastUsed ? index : victim, 0);
  }
  if (policy === "FIFO") {
    return set.reduce((victim, block, index) => block.insertedAt < set[victim].insertedAt ? index : victim, 0);
  }
  if (policy === "RANDOM") return randomIndex(set.length);

  let victim = 0;
  let farthest = -1;
  for (let i = 0; i < set.length; i += 1) {
    const use = nextUse(set[i].blockKey, currentIndex);
    if (use === Infinity) return i;
    if (use > farthest) {
      farthest = use;
      victim = i;
    }
  }
  return victim;
}

function classifyOperation(op) {
  if (op === "S" || op === "W") return "write";
  if (op === "M") return "modify";
  return "read";
}

function countOperation(target, op) {
  const kind = classifyOperation(op);
  if (kind === "read" || kind === "modify") target.loads += 1;
  if (kind === "write" || kind === "modify") target.stores += 1;
}

function touchBlock(block, op, timer, policy) {
  const kind = classifyOperation(op);
  if (kind === "read" || kind === "modify") block.reads += 1;
  if (kind === "write" || kind === "modify") block.writes += 1;
  if (policy === "LRU") block.lastUsed = timer;
}

function installBlock(level, event, timer, currentIndex) {
  const decoded = decodeAddress(event.address, level);
  const set = getSet(level, decoded.setIndex);

  const existing = set.find((block) => block.valid && block.tagKey === decoded.tagKey);
  if (existing) {
    touchBlock(existing, event.op, timer, state.config.policy);
    return { setIndex: decoded.setIndex, way: set.indexOf(existing), evicted: false };
  }

  const emptyWay = set.findIndex((block) => !block.valid);
  const way = emptyWay >= 0 ? emptyWay : chooseVictim(set, state.config.policy, currentIndex);

  if (emptyWay < 0) {
    level.stats.evictions += 1;
    getSetStats(level, decoded.setIndex).evictions += 1;
  }

  set[way] = {
    valid: true,
    tag: decoded.tag,
    tagKey: decoded.tagKey,
    blockAddr: decoded.blockAddr,
    blockKey: decoded.blockKey,
    lastUsed: timer,
    insertedAt: timer,
    reads: 0,
    writes: 0,
  };
  touchBlock(set[way], event.op, timer, state.config.policy);
  return { setIndex: decoded.setIndex, way, evicted: emptyWay < 0 };
}

function probeLevel(level, event, timer) {
  const decoded = decodeAddress(event.address, level);
  const set = getSet(level, decoded.setIndex);
  const setStats = getSetStats(level, decoded.setIndex);

  level.stats.accesses += 1;
  setStats.accesses += 1;
  countOperation(level.stats, event.op);

  const way = set.findIndex((block) => block.valid && block.tagKey === decoded.tagKey);
  if (way >= 0) {
    level.stats.hits += 1;
    setStats.hits += 1;
    touchBlock(set[way], event.op, timer, state.config.policy);
    return { hit: true, level, way, setIndex: decoded.setIndex, tag: decoded.tag, blockAddr: decoded.blockAddr };
  }

  level.stats.misses += 1;
  setStats.misses += 1;
  return { hit: false, level, way: -1, setIndex: decoded.setIndex, tag: decoded.tag, blockAddr: decoded.blockAddr };
}

function stepOnce() {
  if (!state.config || state.pointer >= state.config.limit) return false;

  const event = state.trace[state.pointer];
  const timer = state.timer + 1;
  const path = [];
  const probes = [];
  let result = "MEMORY";
  let resultLevel = "Memory";
  let activeProbe = null;
  let installInfo = null;

  state.timer = timer;
  countOperation(state, event.op);

  for (const level of state.levels) {
    const probe = probeLevel(level, event, timer);
    probes.push(probe);
    path.push(`${level.name}:${probe.hit ? "H" : "M"}`);
    activeProbe = probe;

    if (probe.hit) {
      result = "HIT";
      resultLevel = level.name;
      state.totalHits += 1;
      break;
    }
  }

  const hitIndex = probes.findIndex((probe) => probe.hit);
  if (hitIndex >= 0) {
    for (let i = hitIndex - 1; i >= 0; i -= 1) {
      installInfo = installBlock(state.levels[i], event, timer, state.pointer);
    }
  } else {
    state.memoryAccesses += 1;
    for (let i = state.levels.length - 1; i >= 0; i -= 1) {
      installInfo = installBlock(state.levels[i], event, timer, state.pointer);
    }
    activeProbe = probes[probes.length - 1];
    path.push("MEM");
  }

  const accessNumber = state.pointer + 1;
  const hitRate = accessNumber ? (state.totalHits / accessNumber) * 100 : 0;
  const activeLevel = result === "HIT" ? resultLevel : "L1";
  const activeLevelObj = state.levels.find((level) => level.name === activeLevel) || state.levels[0];
  const activeDecoded = decodeAddress(event.address, activeLevelObj);
  const activeWay = result === "HIT"
    ? activeProbe.way
    : (installInfo?.way ?? -1);

  const detail = {
    accessNumber,
    traceIndex: state.pointer,
    op: event.op,
    size: event.size,
    address: event.address,
    result,
    resultLevel,
    path,
    setIndex: result === "HIT" ? activeProbe.setIndex : activeDecoded.setIndex,
    tag: result === "HIT" ? activeProbe.tag : activeDecoded.tag,
    way: activeWay,
    hitRate,
  };

  state.pointer += 1;
  state.activeLevelName = activeLevel;
  state.activeSetIndex = detail.setIndex;
  state.lastEvent = detail;
  state.history.unshift(detail);
  if (state.history.length > HISTORY_LIMIT) state.history.pop();

  const sampleEvery = Math.max(1, Math.floor(state.config.limit / 180));
  if (state.pointer === 1 || state.pointer === state.config.limit || state.pointer % sampleEvery === 0) {
    state.rateSeries.push({ x: state.pointer, y: hitRate });
  }

  return true;
}

function stepMany(count) {
  let advanced = 0;
  for (let i = 0; i < count; i += 1) {
    if (!stepOnce()) break;
    advanced += 1;
  }
  if (advanced) renderAll();
  if (state.config && state.pointer >= state.config.limit) stopPlayback();
  updateButtons();
}

function togglePlayback() {
  if (state.playing) {
    stopPlayback();
    return;
  }
  if (!state.prepared) return;

  state.playing = true;
  els.playPause.textContent = "Pause";
  const fps = 20;
  state.playTimer = window.setInterval(() => {
    const speed = Math.max(1, Number(els.speed.value));
    stepMany(Math.max(1, Math.round(speed / fps)));
  }, 1000 / fps);
  updateButtons();
}

function stopPlayback() {
  if (state.playTimer) window.clearInterval(state.playTimer);
  state.playTimer = null;
  state.playing = false;
  els.playPause.textContent = "Play";
  updateButtons();
}

async function runToEnd() {
  if (!state.prepared || state.runningFast) return;
  stopPlayback();
  state.runningFast = true;
  setMessage("Running remaining accesses...");
  updateButtons();

  while (state.config && state.pointer < state.config.limit && state.runningFast) {
    const started = performance.now();
    let batch = 0;
    while (state.pointer < state.config.limit && batch < 6000 && performance.now() - started < 24) {
      stepOnce();
      batch += 1;
    }
    renderAll();
    await new Promise((resolve) => requestAnimationFrame(resolve));
  }

  state.runningFast = false;
  setMessage("Run complete.");
  renderAll();
  updateButtons();
}

function simulatePolicy(policy, config) {
  const saved = {
    config: state.config,
    levels: state.levels,
    future: state.future,
    timer: state.timer,
    pointer: state.pointer,
    totalHits: state.totalHits,
    memoryAccesses: state.memoryAccesses,
    loads: state.loads,
    stores: state.stores,
    activeLevelName: state.activeLevelName,
    activeSetIndex: state.activeSetIndex,
    lastEvent: state.lastEvent,
    history: state.history,
    rateSeries: state.rateSeries,
    rngSeed: state.rngSeed,
  };

  state.config = { ...config, policy };
  state.levels = config.levels.map(createLevel);
  state.future = buildFuture(state.trace, config.blockSize, config.limit);
  state.timer = 0;
  state.pointer = 0;
  state.totalHits = 0;
  state.memoryAccesses = 0;
  state.loads = 0;
  state.stores = 0;
  state.history = [];
  state.rateSeries = [];
  state.rngSeed = 0x5eed1234;

  while (state.pointer < state.config.limit) stepOnce();

  const result = {
    policy,
    totalHits: state.totalHits,
    memoryAccesses: state.memoryAccesses,
    overallHitRate: state.config.limit ? (state.totalHits / state.config.limit) * 100 : 0,
    levels: state.levels.map((level) => ({
      name: level.name,
      localHitRate: level.stats.accesses ? (level.stats.hits / level.stats.accesses) * 100 : 0,
    })),
  };

  Object.assign(state, saved);
  return result;
}

async function comparePolicies() {
  if (!state.trace.length) return;
  stopPlayback();

  let config;
  try {
    config = readConfig();
  } catch (error) {
    setMessage(error.message, true);
    return;
  }

  els.comparisonBody.innerHTML = `<tr><td colspan="6">Running policy comparison...</td></tr>`;
  await new Promise((resolve) => requestAnimationFrame(resolve));

  const results = POLICIES.map((policy) => simulatePolicy(policy, config))
    .sort((a, b) => b.overallHitRate - a.overallHitRate);

  els.comparisonBody.innerHTML = results.map((row) => {
    const l1 = row.levels.find((level) => level.name === "L1")?.localHitRate || 0;
    const l2 = row.levels.find((level) => level.name === "L2")?.localHitRate || 0;
    return `
      <tr>
        <td>${row.policy}</td>
        <td>${formatPercent(row.overallHitRate)}</td>
        <td>${formatPercent(l1)}</td>
        <td>${formatPercent(l2)}</td>
        <td>${formatNumber(row.memoryAccesses)}</td>
      </tr>
    `;
  }).join("");

  setMessage(`Compared ${POLICIES.length} policies over ${formatNumber(config.limit)} accesses.`);
}

function updateButtons() {
  const hasTrace = state.trace.length > 0;
  const canRun = state.prepared && !state.runningFast && (!state.config || state.pointer < state.config.limit);
  els.prepare.disabled = !hasTrace || state.runningFast;
  els.playPause.disabled = !canRun;
  els.step.disabled = !canRun || state.playing;
  els.runAll.disabled = !canRun || state.playing;
  els.reset.disabled = !state.prepared || state.runningFast;
  els.compare.disabled = !hasTrace || state.runningFast;
  els.generateElbows.disabled = !hasTrace || state.runningFast;
}

function cacheHits() {
  return state.totalHits;
}

function renderAll() {
  renderMetrics();
  renderPipeline();
  renderCurrentAccess();
  renderHierarchy();
  renderWays();
  renderHeatMap();
  renderEventLog();
  renderChart();
}

function renderMetrics() {
  const limit = state.config?.limit || 0;
  const hitRate = state.pointer ? (cacheHits() / state.pointer) * 100 : 0;
  const progress = limit ? (state.pointer / limit) * 100 : 0;

  els.metricAccesses.textContent = formatNumber(state.pointer);
  els.metricHits.textContent = formatNumber(cacheHits());
  els.metricMemory.textContent = formatNumber(state.memoryAccesses);
  els.metricHitRate.textContent = formatPercent(hitRate);
  els.metricActiveLevel.textContent = state.lastEvent?.resultLevel || "-";
  els.progressText.textContent = `${formatNumber(state.pointer)} / ${formatNumber(limit)}`;
  els.progressFill.style.inlineSize = `${Math.min(100, progress)}%`;

  if (state.config) {
    const levelText = state.config.levels
      .map((level) => `${level.name} ${level.sizeKb}KB ${level.associativity}-way`)
      .join(" | ");
    els.configSummary.textContent = `${state.config.policy}, ${state.config.blockSize}B blocks | ${levelText}`;
  } else {
    els.configSummary.textContent = "Cache hierarchy not prepared";
  }
}

function renderPipeline() {
  const nodes = [
    ["L1", els.nodeL1, els.nodeL1Text],
    ["L2", els.nodeL2, els.nodeL2Text],
    ["MEM", els.nodeMemory, els.nodeMemoryText],
  ];
  const path = state.lastEvent?.path || [];

  for (const [name, node, text] of nodes) {
    node.className = name === "MEM" ? "pipeline-node memory" : "pipeline-node";
    text.textContent = "Waiting";
  }

  if (!path.length) return;

  for (const item of path) {
    const [name, status] = item.split(":");
    const found = nodes.find(([nodeName]) => nodeName === name);
    if (!found) continue;
    const [, node, text] = found;
    node.classList.add(status === "H" ? "hit" : "miss");
    text.textContent = status === "H" ? "Hit" : "Miss";
  }

  if (path.includes("MEM")) {
    els.nodeMemory.classList.add("memory-read");
    els.nodeMemoryText.textContent = "Read";
  }
}

function renderCurrentAccess() {
  const event = state.lastEvent;
  if (!event) {
    els.resultBadge.textContent = state.prepared ? "Ready" : "Idle";
    els.resultBadge.className = "badge idle";
    els.currentAccess.innerHTML = `
      <div><dt>Index</dt><dd>-</dd></div>
      <div><dt>Operation</dt><dd>-</dd></div>
      <div><dt>Address</dt><dd>-</dd></div>
      <div><dt>Result</dt><dd>-</dd></div>
      <div><dt>Set</dt><dd>-</dd></div>
      <div><dt>Tag</dt><dd>-</dd></div>
    `;
    return;
  }

  const badgeClass = event.result === "HIT" ? "hit" : "miss";
  els.resultBadge.textContent = event.result === "HIT" ? `${event.resultLevel} HIT` : "MEMORY";
  els.resultBadge.className = `badge ${badgeClass}`;
  els.currentAccess.innerHTML = `
    <div><dt>Index</dt><dd>${formatNumber(event.accessNumber)}</dd></div>
    <div><dt>Operation</dt><dd>${escapeHtml(event.op)} / ${formatNumber(event.size)} B</dd></div>
    <div><dt>Address</dt><dd title="${formatHex(event.address)}">${compactHex(event.address)}</dd></div>
    <div><dt>Result</dt><dd>${event.resultLevel}</dd></div>
    <div><dt>Set</dt><dd>${formatNumber(event.setIndex)}</dd></div>
    <div><dt>Tag</dt><dd title="${formatHex(event.tag)}">${compactHex(event.tag)}</dd></div>
  `;
}

function renderHierarchy() {
  if (!state.levels.length) {
    els.hierarchyMeta.textContent = "No accesses yet";
    els.hierarchyBody.innerHTML = `<tr><td colspan="7">Prepare a trace to view hierarchy statistics</td></tr>`;
    return;
  }

  els.hierarchyMeta.textContent = `${formatNumber(state.memoryAccesses)} accesses reached memory`;
  els.hierarchyBody.innerHTML = state.levels.map((level) => {
    const local = level.stats.accesses ? (level.stats.hits / level.stats.accesses) * 100 : 0;
    const global = state.pointer ? (level.stats.hits / state.pointer) * 100 : 0;
    return `
      <tr>
        <td>${level.name}</td>
        <td>${formatNumber(level.stats.accesses)}</td>
        <td>${formatNumber(level.stats.hits)}</td>
        <td>${formatNumber(level.stats.misses)}</td>
        <td>${formatNumber(level.stats.evictions)}</td>
        <td>${formatPercent(local)}</td>
        <td>${formatPercent(global)}</td>
      </tr>
    `;
  }).join("");
}

function activeLevel() {
  return state.levels.find((level) => level.name === state.activeLevelName) || state.levels[0];
}

function renderWays() {
  const level = activeLevel();
  if (!level) {
    els.activeSetTitle.textContent = "Active Set";
    els.activeSetMeta.textContent = "No set selected";
    els.wayGrid.innerHTML = "";
    return;
  }

  for (const button of els.levelTabs.querySelectorAll("button")) {
    button.classList.toggle("active", button.dataset.level === level.name);
  }

  const setIndex = state.activeSetIndex ?? 0;
  const set = level.cache.get(setIndex) || makeEmptySet(level.associativity);
  const stats = level.setStats.get(setIndex) || { accesses: 0, hits: 0, misses: 0, evictions: 0 };
  els.activeSetTitle.textContent = `${level.name} Set ${formatNumber(setIndex)}`;
  els.activeSetMeta.textContent = `${formatNumber(stats.accesses)} accesses, ${formatNumber(stats.evictions)} evictions`;
  els.wayGrid.style.setProperty("--way-count", level.associativity);

  els.wayGrid.innerHTML = set.map((block, index) => {
    const isLast = state.lastEvent?.resultLevel === level.name && state.lastEvent?.way === index;
    const cls = ["way-card", block.valid ? "" : "empty", isLast ? "active" : ""].filter(Boolean).join(" ");
    return `
      <article class="${cls}">
        <div class="way-head">
          <h3>Way ${index}</h3>
          <span>${block.valid ? "Valid" : "Empty"}</span>
        </div>
        <dl>
          <dt>Tag</dt><dd title="${block.valid ? formatHex(block.tag) : "-"}">${block.valid ? compactHex(block.tag) : "-"}</dd>
          <dt>Block</dt><dd title="${block.valid ? formatHex(block.blockAddr) : "-"}">${block.valid ? compactHex(block.blockAddr) : "-"}</dd>
          <dt>Last</dt><dd>${block.valid ? formatNumber(block.lastUsed) : "-"}</dd>
          <dt>Inserted</dt><dd>${block.valid ? formatNumber(block.insertedAt) : "-"}</dd>
          <dt>Reads</dt><dd>${block.valid ? formatNumber(block.reads) : "-"}</dd>
          <dt>Writes</dt><dd>${block.valid ? formatNumber(block.writes) : "-"}</dd>
        </dl>
      </article>
    `;
  }).join("");
}

function renderHeatMap() {
  const level = activeLevel();
  if (!level) {
    els.heatMeta.textContent = "0 sets";
    els.heatMap.innerHTML = "";
    return;
  }

  const step = Math.max(1, Math.ceil(level.numSets / HEAT_CELL_LIMIT));
  const cells = [];
  let maxAccesses = 1;
  for (let start = 0; start < level.numSets; start += step) {
    let accesses = 0;
    const end = Math.min(level.numSets - 1, start + step - 1);
    for (let setIndex = start; setIndex <= end; setIndex += 1) {
      accesses += level.setStats.get(setIndex)?.accesses || 0;
    }
    maxAccesses = Math.max(maxAccesses, accesses);
    cells.push({ start, end, accesses });
  }

  els.heatTitle.textContent = `${level.name} Set Activity`;
  els.heatMeta.textContent = step === 1
    ? `${formatNumber(level.numSets)} sets`
    : `${formatNumber(level.numSets)} sets, grouped by ${formatNumber(step)}`;

  els.heatMap.innerHTML = cells.map((cell) => {
    const levelValue = cell.accesses / maxAccesses;
    const active = state.activeSetIndex !== null && state.activeSetIndex >= cell.start && state.activeSetIndex <= cell.end;
    const label = cell.start === cell.end
      ? `${level.name} set ${cell.start}: ${cell.accesses} accesses`
      : `${level.name} sets ${cell.start}-${cell.end}: ${cell.accesses} accesses`;
    return `<span class="heat-cell ${active ? "active" : ""}" title="${label}" style="--level:${levelValue.toFixed(3)}"></span>`;
  }).join("");
}

function renderEventLog() {
  if (!state.history.length) {
    els.eventLog.innerHTML = `<tr><td colspan="7">Waiting for the first access</td></tr>`;
    return;
  }

  els.eventLog.innerHTML = state.history.map((event) => `
    <tr>
      <td>${formatNumber(event.accessNumber)}</td>
      <td>${escapeHtml(event.op)}</td>
      <td class="address-cell" title="${formatHex(event.address)}">${compactHex(event.address)}</td>
      <td>${event.path.join(" > ")}</td>
      <td>${event.resultLevel}</td>
      <td>${formatNumber(event.setIndex)}</td>
      <td>${event.way >= 0 ? event.way : "-"}</td>
    </tr>
  `).join("");
}

function renderChart() {
  const canvas = els.hitRateChart;
  const parent = canvas.parentElement;
  const rect = parent.getBoundingClientRect();
  const width = Math.max(320, Math.floor(rect.width - 32));
  const height = 260;
  const ratio = window.devicePixelRatio || 1;

  if (canvas.width !== width * ratio || canvas.height !== height * ratio) {
    canvas.width = width * ratio;
    canvas.height = height * ratio;
    canvas.style.width = `${width}px`;
    canvas.style.height = `${height}px`;
  }

  const ctx = canvas.getContext("2d");
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  ctx.clearRect(0, 0, width, height);

  const padding = { top: 22, right: 28, bottom: 38, left: 52 };
  const plotWidth = width - padding.left - padding.right;
  const plotHeight = height - padding.top - padding.bottom;
  const series = state.rateSeries;
  const current = state.pointer ? (cacheHits() / state.pointer) * 100 : 0;
  const average = series.length ? series.reduce((sum, point) => sum + point.y, 0) / series.length : 0;
  const peak = series.length ? Math.max(...series.map((point) => point.y)) : 0;

  els.hitRateStats.innerHTML = `
    <span>Current <strong>${formatPercent(current)}</strong></span>
    <span>Average <strong>${formatPercent(average)}</strong></span>
    <span>Peak <strong>${formatPercent(peak)}</strong></span>
    <span>Samples <strong>${formatNumber(series.length)}</strong></span>
  `;

  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = "#d9e1e6";
  ctx.lineWidth = 1;
  ctx.beginPath();
  for (let i = 0; i <= 4; i += 1) {
    const y = padding.top + (plotHeight * i) / 4;
    ctx.moveTo(padding.left, y);
    ctx.lineTo(width - padding.right, y);
  }
  for (let i = 0; i <= 4; i += 1) {
    const x = padding.left + (plotWidth * i) / 4;
    ctx.moveTo(x, padding.top);
    ctx.lineTo(x, height - padding.bottom);
  }
  ctx.stroke();

  ctx.fillStyle = "#6c7a84";
  ctx.font = "12px system-ui, sans-serif";
  ctx.textAlign = "right";
  ctx.textBaseline = "middle";
  [0, 25, 50, 75, 100].forEach((value) => {
    const y = padding.top + plotHeight - (value / 100) * plotHeight;
    ctx.fillText(`${value}%`, padding.left - 8, y);
  });

  if (!series.length) {
    ctx.textAlign = "center";
    ctx.fillStyle = "#7c8a93";
    ctx.fillText("Run the trace to draw L1/L2/L3 cache performance", width / 2, height / 2);
    return;
  }

  const maxX = Math.max(state.config?.limit || 1, series.at(-1)?.x || 1);
  const points = series.map((point) => ({
    x: padding.left + (point.x / maxX) * plotWidth,
    y: padding.top + plotHeight - (point.y / 100) * plotHeight,
  }));

  const fill = ctx.createLinearGradient(0, padding.top, 0, height - padding.bottom);
  fill.addColorStop(0, "rgba(46, 112, 92, 0.18)");
  fill.addColorStop(1, "rgba(46, 112, 92, 0.02)");
  ctx.beginPath();
  points.forEach((point, index) => index ? ctx.lineTo(point.x, point.y) : ctx.moveTo(point.x, point.y));
  ctx.lineTo(points.at(-1).x, height - padding.bottom);
  ctx.lineTo(points[0].x, height - padding.bottom);
  ctx.closePath();
  ctx.fillStyle = fill;
  ctx.fill();

  ctx.strokeStyle = "#2e705c";
  ctx.lineWidth = 2.5;
  ctx.beginPath();
  points.forEach((point, index) => index ? ctx.lineTo(point.x, point.y) : ctx.moveTo(point.x, point.y));
  ctx.stroke();

  const movingAverage = series.map((point, index) => {
    const start = Math.max(0, index - 7);
    const slice = series.slice(start, index + 1);
    const value = slice.reduce((sum, item) => sum + item.y, 0) / slice.length;
    return {
      x: padding.left + (point.x / maxX) * plotWidth,
      y: padding.top + plotHeight - (value / 100) * plotHeight,
    };
  });
  ctx.strokeStyle = "#5f6972";
  ctx.lineWidth = 2;
  ctx.beginPath();
  movingAverage.forEach((point, index) => index ? ctx.lineTo(point.x, point.y) : ctx.moveTo(point.x, point.y));
  ctx.stroke();

  const last = points.at(-1);
  ctx.fillStyle = "#2e705c";
  ctx.beginPath();
  ctx.arc(last.x, last.y, 5, 0, Math.PI * 2);
  ctx.fill();

  ctx.fillStyle = "#1f2b33";
  ctx.font = "600 12px system-ui, sans-serif";
  ctx.textAlign = "left";
  ctx.fillText(`Current ${formatPercent(current)}`, Math.min(last.x + 8, width - 128), Math.max(padding.top + 14, last.y - 10));
}

function cloneConfigForExperiment(policy = "LRU") {
  const config = readConfig();
  return {
    ...config,
    policy,
    levels: config.levels.map((level) => ({ ...level })),
  };
}

function l1RatesFromResult(result) {
  const l1 = result.levels.find((level) => level.name === "L1");
  const l2 = result.levels.find((level) => level.name === "L2");
  return {
    l1HitRate: l1?.localHitRate || 0,
    l1MissRate: 100 - (l1?.localHitRate || 0),
    l2HitRate: l2?.localHitRate || 0,
    overallHitRate: result.overallHitRate,
    memoryAccesses: result.memoryAccesses,
  };
}

function drawElbowChart(canvas, points, options) {
  const rect = canvas.parentElement.getBoundingClientRect();
  const width = Math.max(320, Math.floor(rect.width - 24));
  const height = 260;
  const ratio = window.devicePixelRatio || 1;

  if (canvas.width !== width * ratio || canvas.height !== height * ratio) {
    canvas.width = width * ratio;
    canvas.height = height * ratio;
    canvas.style.width = `${width}px`;
    canvas.style.height = `${height}px`;
  }

  const ctx = canvas.getContext("2d");
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  ctx.clearRect(0, 0, width, height);

  const padding = { top: 22, right: 24, bottom: 42, left: 52 };
  const plotWidth = width - padding.left - padding.right;
  const plotHeight = height - padding.top - padding.bottom;

  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = "#d9e1e6";
  ctx.lineWidth = 1;
  ctx.beginPath();
  for (let i = 0; i <= 4; i += 1) {
    const y = padding.top + (plotHeight * i) / 4;
    ctx.moveTo(padding.left, y);
    ctx.lineTo(width - padding.right, y);
  }
  ctx.stroke();

  const maxX = Math.max(...points.map((point) => point.x));
  const minX = Math.min(...points.map((point) => point.x));
  const maxY = Math.max(100, Math.ceil(Math.max(...points.map((point) => point.y))));

  ctx.fillStyle = "#6c7a84";
  ctx.font = "12px system-ui, sans-serif";
  ctx.textAlign = "right";
  ctx.textBaseline = "middle";
  for (let i = 0; i <= 4; i += 1) {
    const value = (maxY * (4 - i)) / 4;
    const y = padding.top + (plotHeight * i) / 4;
    ctx.fillText(`${value.toFixed(0)}%`, padding.left - 8, y);
  }

  const plotPoints = points.map((point) => ({
    ...point,
    px: padding.left + ((point.x - minX) / Math.max(1, maxX - minX)) * plotWidth,
    py: padding.top + plotHeight - (point.y / maxY) * plotHeight,
  }));

  ctx.strokeStyle = "#2e705c";
  ctx.lineWidth = 2.5;
  ctx.beginPath();
  plotPoints.forEach((point, index) => {
    if (index === 0) ctx.moveTo(point.px, point.py);
    else ctx.lineTo(point.px, point.py);
  });
  ctx.stroke();

  ctx.fillStyle = "#ffffff";
  ctx.strokeStyle = "#2e705c";
  ctx.lineWidth = 2;
  plotPoints.forEach((point) => {
    ctx.beginPath();
    ctx.arc(point.px, point.py, 4, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
  });

  ctx.fillStyle = "#1f2a33";
  ctx.textAlign = "center";
  ctx.textBaseline = "top";
  plotPoints.forEach((point) => {
    ctx.fillText(point.label, point.px, height - padding.bottom + 12);
  });

  ctx.font = "600 12px system-ui, sans-serif";
  ctx.fillText(options.xLabel, padding.left + plotWidth / 2, height - 18);

  ctx.save();
  ctx.translate(15, padding.top + plotHeight / 2);
  ctx.rotate(-Math.PI / 2);
  ctx.fillText(options.yLabel, 0, 0);
  ctx.restore();
}

async function generateElbowGraphs() {
  if (!state.trace.length) return;
  stopPlayback();

  let baseConfig;
  try {
    baseConfig = cloneConfigForExperiment("LRU");
  } catch (error) {
    setMessage(error.message, true);
    return;
  }

  els.elbowStatus.textContent = "Generating elbow graph data with LRU...";
  await new Promise((resolve) => requestAnimationFrame(resolve));

  const capacitySizes = [2, 4, 8, 16, 32];
  const capacityRows = capacitySizes.map((sizeKb) => {
    const config = {
      ...baseConfig,
      levels: baseConfig.levels.map((level) => level.name === "L1" ? { ...level, sizeKb, sizeBytes: sizeKb * 1024, numBlocks: (sizeKb * 1024) / level.blockSize, numSets: ((sizeKb * 1024) / level.blockSize) / level.associativity } : { ...level }),
    };
    const rates = l1RatesFromResult(simulatePolicy("LRU", config));
    return { sizeKb, ...rates };
  });

  const associativities = [1, 2, 4, 8];
  const assocRows = associativities.map((associativity) => {
    const config = {
      ...baseConfig,
      levels: baseConfig.levels.map((level) => level.name === "L1" ? { ...level, associativity, numSets: level.numBlocks / associativity } : { ...level }),
    };
    const rates = l1RatesFromResult(simulatePolicy("LRU", config));
    return { associativity, ...rates };
  });

  els.capacityBody.innerHTML = capacityRows.map((row) => `
    <tr>
      <td>${row.sizeKb}</td>
      <td>${formatPercent(row.l1MissRate)}</td>
      <td>${formatPercent(row.l1HitRate)}</td>
      <td>${formatPercent(row.overallHitRate)}</td>
      <td>${formatNumber(row.memoryAccesses)}</td>
    </tr>
  `).join("");

  els.associativityBody.innerHTML = assocRows.map((row) => `
    <tr>
      <td>${row.associativity}</td>
      <td>${formatPercent(row.l1HitRate)}</td>
      <td>${formatPercent(row.l1MissRate)}</td>
      <td>${formatPercent(row.overallHitRate)}</td>
      <td>${formatNumber(row.memoryAccesses)}</td>
    </tr>
  `).join("");

  drawElbowChart(
    els.capacityChart,
    capacityRows.map((row) => ({ x: row.sizeKb, y: row.l1MissRate, label: `${row.sizeKb}K` })),
    { xLabel: "L1 Cache Size", yLabel: "L1 Miss Rate" },
  );
  drawElbowChart(
    els.associativityChart,
    assocRows.map((row) => ({ x: row.associativity, y: row.l1HitRate, label: `${row.associativity}w` })),
    { xLabel: "L1 Associativity", yLabel: "L1 Hit Rate" },
  );

  els.elbowStatus.textContent = "Generated capacity and associativity graph data using LRU.";
}

els.traceFile.addEventListener("change", (event) => {
  loadTraceFromFile(event.target.files[0]).catch((error) => setMessage(error.message, true));
});
els.loadBundledTrace.addEventListener("click", loadBundledTrace);
els.prepare.addEventListener("click", prepareSimulation);
els.reset.addEventListener("click", resetSimulation);
els.playPause.addEventListener("click", togglePlayback);
els.step.addEventListener("click", () => stepMany(1));
els.runAll.addEventListener("click", runToEnd);
els.compare.addEventListener("click", comparePolicies);
els.generateElbows.addEventListener("click", generateElbowGraphs);

[
  els.policy, els.blockSize, els.maxAccesses,
  els.l1SizeKb, els.l1Assoc, els.l2SizeKb, els.l2Assoc,
].forEach((input) => {
  input.addEventListener("change", markConfigDirty);
  input.addEventListener("input", () => {
    if (input !== els.policy) markConfigDirty();
  });
});

els.speed.addEventListener("input", () => {
  els.speedLabel.textContent = formatNumber(els.speed.value);
});

els.levelTabs.addEventListener("click", (event) => {
  const button = event.target.closest("button[data-level]");
  if (!button) return;
  state.activeLevelName = button.dataset.level;
  state.activeSetIndex = 0;
  renderWays();
  renderHeatMap();
});

window.addEventListener("resize", renderChart);

els.speedLabel.textContent = formatNumber(els.speed.value);
updateButtons();
renderAll();
