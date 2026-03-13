const state = {
  root: null,
  selectedNodeId: null,
  search: "",
  flatNodes: [],
};

const elements = {
  dropzone: document.getElementById("dropzone"),
  fileInput: document.getElementById("file-input"),
  loadSampleButton: document.getElementById("load-sample-button"),
  searchInput: document.getElementById("search-input"),
  treeRoot: document.getElementById("tree-root"),
  emptyState: document.getElementById("empty-state"),
  detailContent: document.getElementById("detail-content"),
  metricRoot: document.getElementById("metric-root"),
  metricCount: document.getElementById("metric-count"),
  metricDepth: document.getElementById("metric-depth"),
  metricTime: document.getElementById("metric-time"),
  detailName: document.getElementById("detail-name"),
  detailDepth: document.getElementById("detail-depth"),
  detailInclusiveTime: document.getElementById("detail-inclusive-time"),
  detailChildCount: document.getElementById("detail-child-count"),
  detailEntryLine: document.getElementById("detail-entry-line"),
  detailExitLine: document.getElementById("detail-exit-line"),
  detailEntryTime: document.getElementById("detail-entry-time"),
  detailEntryLine2: document.getElementById("detail-entry-line-2"),
  detailEntryPc: document.getElementById("detail-entry-pc"),
  detailExitTime: document.getElementById("detail-exit-time"),
  detailExitLine2: document.getElementById("detail-exit-line-2"),
  detailExitPc: document.getElementById("detail-exit-pc"),
  detailPath: document.getElementById("detail-path"),
};

function formatNumber(value) {
  return new Intl.NumberFormat("en-GB").format(value ?? 0);
}

function formatAddress(value) {
  if (value === null || value === undefined) {
    return "-";
  }

  return `0x${Number(value).toString(16)}`;
}

function displayPc(site) {
  return site.pc_label || formatAddress(site.pc);
}

function normaliseTree(root) {
  let autoId = 0;

  function visit(node, depth, path) {
    const id = `node-${autoId++}`;
    const children = (node.children || []).map((child) =>
      visit(child.callee, depth + 1, [...path, node.name]),
    );

    return {
      id,
      name: node.name || "unknown",
      depth,
      path: [...path, node.name || "unknown"],
      inclusiveTime: node.inclusive_time || 0,
      childCount: children.length,
      functionEntry: node.function_entry || {},
      functionExit: node.function_exit || {},
      children,
    };
  }

  return visit(root, 0, []);
}

function flattenTree(root) {
  const nodes = [];

  function visit(node) {
    nodes.push(node);
    node.children.forEach(visit);
  }

  visit(root);
  return nodes;
}

function getTreeMetrics(root) {
  let count = 0;
  let maxDepth = 0;

  function visit(node) {
    count += 1;
    maxDepth = Math.max(maxDepth, node.depth);
    node.children.forEach(visit);
  }

  visit(root);
  return { count, maxDepth };
}

function updateMetrics() {
  if (!state.root) {
    elements.metricRoot.textContent = "No trace loaded";
    elements.metricCount.textContent = "0";
    elements.metricDepth.textContent = "0";
    elements.metricTime.textContent = "0";
    return;
  }

  const { count, maxDepth } = getTreeMetrics(state.root);
  elements.metricRoot.textContent = state.root.name;
  elements.metricCount.textContent = formatNumber(count);
  elements.metricDepth.textContent = formatNumber(maxDepth);
  elements.metricTime.textContent = formatNumber(state.root.inclusiveTime);
}

function selectedNode() {
  return state.flatNodes.find((node) => node.id === state.selectedNodeId) || null;
}

function updateDetailPanel() {
  const node = selectedNode();
  if (!node) {
    elements.emptyState.classList.remove("hidden");
    elements.detailContent.classList.add("hidden");
    return;
  }

  elements.emptyState.classList.add("hidden");
  elements.detailContent.classList.remove("hidden");

  elements.detailName.textContent = node.name;
  elements.detailDepth.textContent = `Depth ${node.depth}`;
  elements.detailInclusiveTime.textContent = formatNumber(node.inclusiveTime);
  elements.detailChildCount.textContent = formatNumber(node.childCount);
  elements.detailEntryLine.textContent = formatNumber(node.functionEntry.line);
  elements.detailExitLine.textContent = formatNumber(node.functionExit.line);
  elements.detailEntryTime.textContent = formatNumber(node.functionEntry.time);
  elements.detailEntryLine2.textContent = formatNumber(node.functionEntry.line);
  elements.detailEntryPc.textContent = displayPc(node.functionEntry);
  elements.detailExitTime.textContent = formatNumber(node.functionExit.time);
  elements.detailExitLine2.textContent = formatNumber(node.functionExit.line);
  elements.detailExitPc.textContent = displayPc(node.functionExit);

  elements.detailPath.innerHTML = "";
  node.path.forEach((segment) => {
    const item = document.createElement("li");
    item.textContent = segment;
    elements.detailPath.appendChild(item);
  });
}

function matchesSearch(node) {
  if (!state.search) {
    return true;
  }

  const haystack = [
    node.name,
    displayPc(node.functionEntry),
    displayPc(node.functionExit),
    node.path.join(" "),
  ]
    .join(" ")
    .toLowerCase();

  return haystack.includes(state.search);
}

function nodeOrDescendantMatches(node) {
  if (matchesSearch(node)) {
    return true;
  }

  return node.children.some(nodeOrDescendantMatches);
}

function renderTree() {
  elements.treeRoot.innerHTML = "";

  if (!state.root) {
    const empty = document.createElement("div");
    empty.className = "empty-state";
    empty.textContent = "Load a call tree export to start exploring.";
    elements.treeRoot.appendChild(empty);
    updateDetailPanel();
    return;
  }

  function createNode(node) {
    const wrapper = document.createElement("div");
    wrapper.className = "tree-node";
    if (!nodeOrDescendantMatches(node)) {
      wrapper.classList.add("hidden-match");
    }

    const button = document.createElement("button");
    button.className = "tree-card";
    if (node.id === state.selectedNodeId) {
      button.classList.add("selected");
    }

    button.addEventListener("click", () => {
      state.selectedNodeId = node.id;
      renderTree();
      updateDetailPanel();
    });

    const indent = document.createElement("span");
    indent.className = "tree-indent";

    const main = document.createElement("div");
    main.className = "tree-main";

    const nameRow = document.createElement("div");
    nameRow.className = "tree-name-row";

    const name = document.createElement("span");
    name.className = "tree-name";
    name.textContent = node.name;

    const path = document.createElement("span");
    path.className = "tree-path";
    path.textContent = node.path.join(" / ");

    nameRow.append(name, path);

    const meta = document.createElement("div");
    meta.className = "tree-meta";

    const time = document.createElement("span");
    time.className = "tree-time";
    time.textContent = `${formatNumber(node.inclusiveTime)} ticks`;

    const line = document.createElement("span");
    line.className = "tree-line";
    line.textContent = `L${formatNumber(node.functionEntry.line)} -> L${formatNumber(node.functionExit.line)}`;

    main.append(nameRow);
    meta.append(time, line);
    button.append(indent, main, meta);
    wrapper.appendChild(button);

    if (node.children.length) {
      const children = document.createElement("div");
      children.className = "tree-children";
      node.children.forEach((child) => children.appendChild(createNode(child)));
      wrapper.appendChild(children);
    }

    return wrapper;
  }

  elements.treeRoot.appendChild(createNode(state.root));
  updateDetailPanel();
}

function loadTreeData(rawData) {
  const normalisedRoot = normaliseTree(rawData);
  state.root = normalisedRoot;
  state.flatNodes = flattenTree(normalisedRoot);
  state.selectedNodeId = normalisedRoot.id;
  updateMetrics();
  renderTree();
}

async function loadBundledSample() {
  const response = await fetch("../tests/calltree-quicksort-symbols.json.ref");
  if (!response.ok) {
    throw new Error("Failed to load bundled sample");
  }

  const data = await response.json();
  loadTreeData(data);
}

function handleFile(file) {
  if (!file) {
    return;
  }

  const reader = new FileReader();
  reader.onload = () => {
    try {
      const data = JSON.parse(String(reader.result));
      loadTreeData(data);
    } catch (error) {
      window.alert(`Could not parse JSON: ${error.message}`);
    }
  };
  reader.readAsText(file);
}

elements.fileInput.addEventListener("change", (event) => {
  handleFile(event.target.files?.[0]);
});

elements.loadSampleButton.addEventListener("click", async () => {
  try {
    await loadBundledSample();
  } catch (error) {
    window.alert(
      "Could not load the bundled sample. Start a local web server from the repo root or drag the sample JSON file into the page.",
    );
  }
});

elements.searchInput.addEventListener("input", (event) => {
  state.search = event.target.value.trim().toLowerCase();
  renderTree();
});

["dragenter", "dragover"].forEach((eventName) => {
  elements.dropzone.addEventListener(eventName, (event) => {
    event.preventDefault();
    elements.dropzone.classList.add("drag-over");
  });
});

["dragleave", "drop"].forEach((eventName) => {
  elements.dropzone.addEventListener(eventName, (event) => {
    event.preventDefault();
    elements.dropzone.classList.remove("drag-over");
  });
});

elements.dropzone.addEventListener("drop", (event) => {
  handleFile(event.dataTransfer?.files?.[0]);
});

updateMetrics();
renderTree();
