# WorldForge — Overkill Edition

A **modular, deterministic-seed fantasy colony simulation** combining the depth of **Dwarf Fortress**, the management gameplay of **RimWorld**, and a **smooth voxel world powered by Unreal Engine**.

WorldForge generates a living world where civilizations rise and fall, economies evolve, religions compete, and cosmic forces shape history long before the player arrives.

**Every playthrough is a new world.**

---

# Core Concept

WorldForge is built around a **deterministic world seed**.

The same seed will generate the same world across machines, ensuring:

- reproducibility
- deterministic simulation
- mod compatibility
- emergent storytelling

Civilizations, economies, and religions emerge organically from geography, history, and cultural pressures rather than scripted narratives.

---

# Game Pillars

### Deterministic Worlds
Every generative system draws from seeded RNG streams so a world can be reproduced exactly.

### Emergent Civilization
Cultures form through geography, resource pressure, historical events, and cosmic influence.

### Indirect Player Control
Players set policies, priorities, and infrastructure — colonists act based on needs, skills, personalities, and beliefs.

### Infinite Extensibility
New races, biomes, economic systems, and cosmic entities can be added through the mod system without requiring engine changes.

---

# Core Features

## Procedural World Generation

Worlds are generated through a layered pipeline including:

- terrain simulation
- tectonic plate generation
- rainfall and temperature modeling
- biome distribution
- resource placement
- civilization generation
- economic systems
- cosmic entities

Each world also runs **centuries of simulated prehistory** before the player arrives, producing believable political and economic conditions.

---

## Smooth Voxel Terrain

WorldForge uses **smooth SDF voxel terrain** instead of traditional block voxels.

Features include:

- smooth hills and slopes
- natural caves and arches
- persistent terrain modification
- voxel-based construction
- destructible environments

Terrain generation is powered by **procedural noise and voxel chunk systems**.

---

## Hybrid Visual Style

| Element | Implementation |
|-------|--------|
Terrain | Smooth SDF voxel terrain |
Buildings | Voxel block construction |
Characters | 2D billboard sprites |
Water | Waterline Pro |
Weather | SkyScape volumetric system |

This creates a world that feels **physically constructed** while keeping characters expressive and readable.

---

## 20+ Playable Races

The race system supports unlimited races through a modular registry.

Each race defines:

- cultural DNA
- biome preferences
- economic tendencies
- currency types
- religious alignment
- diplomatic behavior

New races can be added via mods without engine changes.

---

## Living Economy

WorldForge includes a fully simulated economy featuring:

- supply and demand pricing
- dynamic regional markets
- per-civilization currencies
- exchange rates between currencies
- trade routes and caravans
- economic collapse events
- black market trade

Currencies derive their value from a **universal commodity anchor based on food calories**, allowing exchange rates to update automatically across civilizations.

---

## Government Economic Systems

Civilizations operate under **12 different economic policy models**:

- Free Market Republic
- Merchant Oligarchy
- Feudal Kingdom
- Theocracy
- Military Dictatorship
- Command Economy
- Trade Confederation
- Corporate State
- Magocracy
- Imperial Bureaucracy
- Tribal Confederation
- Aquatic Council

Each government type produces different market behavior, price controls, trade policies, and currency stability.

---

## Emergent Black Markets

When governments impose price controls or embargoes, shortages and surpluses naturally create **black markets**.

These arise from economic forces rather than scripted events.

Black markets react dynamically to:

- price ceilings
- rationing systems
- embargoes
- taxation pressure
- cultural restrictions

---

## Overworld Strategy Layer

Players manage their civilization through a persistent world map.

Features include:

- caravans and trade routes
- diplomatic negotiations
- military campaigns
- settlement expansion
- outpost networks
- regional economic influence

---

## Cosmic Influence

Procedurally generated cosmic entities influence the world.

Possible domains include:

- commerce
- war
- harvest
- corruption
- dreams
- fate

Entities can bless trade routes, destabilize economies, or reshape civilizations through supernatural influence.

---

# Technical Architecture

WorldForge uses a **deterministic simulation architecture** built on fixed-step processing.

Major subsystems include:

- SimulationTimeManager
- ProcessorManager
- EconomySubsystem
- CivilizationSubsystem
- CosmicSubsystem
- EventBus

This architecture ensures:

- deterministic behavior
- stable simulation ticks
- scalable world systems
- reproducible worlds

---

# Modding Philosophy

WorldForge is **mod-first** by design.

Players can add:

- new races
- biomes
- items
- economic systems
- cosmic entities
- world generators
- government types

All mod content loads dynamically without modifying the core engine.

---

# Project Status

**Development Stage:** Pre-Production

Current focus areas:

- world generation pipeline
- economic simulation
- civilization systems
- deterministic simulation architecture
- race registry framework

---

# Inspirations

WorldForge draws inspiration from:

- Dwarf Fortress
- RimWorld
- Civilization
- Factorio
- Mount & Blade
- Total War

---

# Repository Structure

```
WorldForge/
├ Config/
├ Source/
├ Content/
├ Plugins/
├ WorldForge.uproject
├ .gitignore
└ .gitattributes
```

---

# Long-Term Vision

WorldForge aims to create a **living procedural world simulation** where:

- history emerges organically
- civilizations evolve independently
- economies create real incentives
- players shape the world indirectly

Each world becomes a unique historical narrative.

---

# License

MIT