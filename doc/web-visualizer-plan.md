# Tarmac Trace Utilities Web Visualizer Plan

## Goal

Deliver a browser-based analysis workbench on top of the existing `libtarmac`
engine so Arm teams can inspect traces faster, onboard engineers more easily,
and share a modern demo surface with internal and partner groups.

This repo already has the hard systems work:

- Tarmac parsing and indexing
- register and memory state reconstruction
- call tree generation
- flamegraph-compatible folded stacks
- trace navigation by line, time, and PC

The web project should preserve that investment and expose it through a thin
API and a polished frontend.

## Product Thesis

The most compelling first deliverable is not "a browser that copies the curses
UI". It is a linked analysis dashboard with four investigation surfaces:

1. Instruction timeline
2. Call analysis
3. Register and memory state inspector
4. Memory access density / heatmap

Each surface shares a single selected cursor in the trace so users can move
between "what executed", "who called it", and "what state changed".

## Recommended Architecture

### Phase 1: repo-native slice

Build the new web capability around exportable JSON from the existing C++
library. This repo now includes that first slice via `tarmac-calltree-json`.

That lets us validate:

- schema shape
- frontend interaction model
- maintainer appetite for web outputs
- demo narrative for Solfast Labs

### Phase 2: thin runtime API

Add a dedicated `tarmac-web-api` service in C++ that reuses `libtarmac` and
serves JSON endpoints directly.

Suggested endpoints:

- `POST /api/session/open-trace`
- `GET /api/session/:id/summary`
- `GET /api/session/:id/calltree`
- `GET /api/session/:id/flamegraph`
- `GET /api/session/:id/timeline?start=&end=&q=`
- `GET /api/session/:id/registers?line=`
- `GET /api/session/:id/memory?line=&addr=&size=`
- `GET /api/session/:id/search?q=`

The server should keep a small in-memory session map of opened trace/index/image
triples so the frontend can cheaply request linked views.

### Why not reimplement parsing in TypeScript or Python

- It duplicates Arm-maintained semantics.
- It increases merge risk.
- It makes large-trace performance worse.
- It weakens the pitch, because the strongest story is "we extended your core"
  rather than "we built a parallel toolchain".

## Proposed Folder Structure

### Near-term, inside this repo

```text
doc/
  web-visualizer-plan.md
tools/
  calltree-json.cpp
web/
  README.md
  calltree-viewer.html
  app.css
  app.js
```

### Full project layout once the thin API lands

```text
include/libtarmac/
lib/
tools/
  calltree-json.cpp
  web-api.cpp
web/
  package.json
  src/
    app/
    components/
    features/
      calltree/
      flamegraph/
      timeline/
      registers/
      memory/
    lib/
    styles/
  public/
tests/
  web-api/
```

## MVP Scope

### Must-have

1. Open a trace and image pair
2. Render interactive call tree / flamegraph
3. Select a call and inspect metadata
4. Search by function name or address
5. Keep the UI fast and polished on sample traces

### Next after MVP

1. Instruction timeline
2. Register inspector at selected line
3. Memory view with previous-write navigation
4. Memory access heatmap
5. Deep links and bookmarkable state

### Later paid expansion

1. Trace-to-trace diffing
2. Saved investigations
3. Exportable reports
4. Remote trace hosting
5. Team annotations

## Fixed-Price Pitch Framing For Solfast Labs

### Phase 1: discovery + demonstrator

- working web call tree viewer
- export schema owned inside TTU
- sample traces and screenshots
- architecture note for maintainers

### Phase 2: production MVP

- thin API service
- call tree + flamegraph + linked timeline
- register inspector
- large-trace performance hardening

### Phase 3: advanced debugging workflows

- memory analytics
- comparison mode
- session sharing and saved views

## Delivery Narrative

Pitch this as "web-native visibility for Arm trace analysis" rather than a UI
reskin. The core business value is faster model-debug loops and lower friction
for teams who do not want to live inside a terminal or desktop-native utility.
