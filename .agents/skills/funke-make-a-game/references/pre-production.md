# Pre-production: game brief, palette, design language

Every new game starts here, before the first scene, script or texture. The output is two short documents and a palette file in the project. Every later step reads them: the scene layout, the Data Asset tables, the art generators (`funke-game-art`) and the UI.

**Why this step exists.** Without an agreed look, agents produce a grab-bag: three pixel densities in one screen, the default font, colours picked per file, a menu that looks like a debug panel. The user then rejects whole asset families late. Mindcraft's art was regenerated three times for exactly that reason. A few questions up front cost minutes; a redo costs days.

## When

| Situation | Do |
|---|---|
| New game | Full flow below. |
| Existing game that has `Design/GameBrief.md` + `Design/DesignLanguage.md` (or its own equivalents: `DesignLanguage.md`, `Planning/Style-Guide.md`, `Planning/Design.md`, a README) | Read them before you start, and follow them. Don't ask again. |
| Existing game without design docs, and the task adds visible content (new screen, new enemy family, new level theme) | Offer to write the docs from what the project already shows. Then extend them for the new content. |
| A small change that adds no new visuals (a bug fix, a tweak, one more enemy of an existing kind) | Skip. |

## 1. Ask

Ask in the user's language. Use the question tool if you have one (`AskUserQuestion`: up to 4 questions per round, 2–4 options each, and "Other" is always available for free text). Otherwise ask a short numbered list in chat. Put your recommendation first in each question. Make every option concrete enough that the answer can go straight into the brief.

**Skip what the request already answers.** "Make a pixel-art roguelike for phones in portrait, dark fantasy" already settles platform, orientation, style and mood, so don't ask those again.

### Round 0: how much does the user want to decide?

Ask this first and on its own, because the answer decides whether there are more rounds:

> How should we set up the game?
> 1. **Define it together (recommended).** Two short rounds: the game, then the look.
> 2. **I give the idea, you do the look.** Only the game round; you design palette and design language.
> 3. **You decide everything (100 %).** No more questions; you fill the brief and design language yourself.

If the user already said something like "just do it", "entscheide selbst" or "mach einfach", that means option 3 and you don't ask.

### Round 1: the game

1. **Genre and core idea**: offer 3 concrete pitches derived from the request (one sentence each), plus "Other".
2. **Platform**: PC (mouse + keyboard) · phone portrait · phone landscape · PC + phone.
3. **Scope**: playable prototype of the core mechanic · one polished level/vertical slice · small full game (menu, 3–5 levels, progression) · bigger (tell me).
4. **Dimension and camera**: 2D side view · 2D top-down · 3D third person · 3D top-down/isometric. Leave this question out when the genre already implies it.

### Round 2: the look

1. **Art style**: pixel art (state the density, e.g. 16 px characters on 16 px tiles) · flat vector (clean shapes, soft gradients) · low-poly 3D (flat shaded, solid colours) · hand-painted/illustrated.
2. **Palette and mood**: offer 3 named palettes that fit the pitch, each with 5–6 hex values in the option text (e.g. "Warm dusk: #2B1B2F #6B2E4F #E26D5C #F5B971 #FFF3D6"), plus "I'll give hex codes".
3. **UI language**: rounded and soft (pills, thick outline, bottom lip) · sharp and minimal (thin lines, flat) · material frames that come from the game world (wood + stone, metal + rivets) · pixel frames (9-slice, 1 px outline). Name the font feeling with each option, e.g. "rounded sans", "pixel font for titles".
4. **Feel / juice level**: calm and clean · punchy (screen shake, hit-stop, particles, pop-in) · cosy (gentle easing, soft sounds).

Optional third round only when it matters for the game: audio mood, reference games ("like Celeste's dash, Hades' UI"), languages to ship, accessibility (colour-blind safe signals, big touch targets).

### Option 3: the agent decides (100 %)

Make every decision above yourself and be bold rather than generic. Pick a specific palette with a mood name, one art style and a specific UI language that suits the pitch. "Blue buttons on a grey panel" is not a design language. Write the documents exactly as below and show the summary and the style sample (step 3) in your next progress message. Then keep working, without waiting for approval. The user can still interrupt.

## 2. Write the documents

Create them in `<Project>/Design/`. That folder sits next to `Assets/`, so it is never shipped.

### `Design/GameBrief.md`

```markdown
# <Game> — Game Brief

**Pitch:** <one or two sentences: what the player does and why it is fun>
**Platform:** <PC / phone portrait / …> · **Dimension:** <2D side / top-down / 3D …> · **Scope:** <prototype / slice / full game: N levels>

## Core loop
1. <action> → 2. <challenge> → 3. <reward / progress> → back to 1

## Controls
| Action | PC | Touch |
|---|---|---|

## Win, lose, restart
- Win: … · Lose: … · Restart: … · Feedback: <score, lives, stars, HUD>

## Progression
<levels / waves / unlocks / economy; what the result screen and level cards show>

## Content (→ Data Assets)
| Kind | Entries | Data Asset type |
|---|---|---|
| Enemies | Slime, Bat, … | `EnemyData` |
| Levels | 1–5 | `LevelData` |

## Feel targets
<speed, weight, juice level, camera behaviour>

## Out of scope
<what we deliberately don't build now>

## Open questions
```

The **Content** table is where data-driven design starts: everything with more than one entry of the same kind (enemies, items, waves, levels, upgrades, dialogue) becomes a Data Asset type. See `data-driven.md`.

### `Design/DesignLanguage.md`

```markdown
# <Game> — Design Language

## Art direction
3–5 numbered principles, each one sentence plus the reason. Example: "One pixel density: 16 texels per tile,
characters 16×24, UI art pixel = 3 screen px at 1080p. Nothing finer or coarser."

## Palette
Tokens, not loose values. Source of truth: `Design/palette.json`.
| Token | Hex | Role |
|---|---|---|
| ink | #1B1A2E | outlines, text shadows |
| surface | … | panel body |
| primary | … | main action, the player's signature colour |
| positive / danger / gold | … | win, damage, rewards |
(+ world ramps: 4–5 steps dark → light per material, e.g. grass, stone, wood)
Signature colours: every mechanic the player must learn (hazard, pickup, checkpoint) gets one colour that it keeps
everywhere: model, particles, UI icon.

## Typography
| Role | Font | Sizes (UI reference units) |
|---|---|---|
| Titles | … | 64 / 40 |
| Body, buttons | … | 32 / 26 |

## Scale and pixel rules
Pixel density, art px → UI px, PPU, filter (Point for pixel art), camera size; minimum touch target.

## Shapes and UI
Corner radius or cut corners, outline width, bevel/lip, shadow, panel vs well vs button, 9-slice borders,
states (hover ×1.1, pressed ×0.8 + lip removed, disabled ×0.5).

## Light and shading
Light direction (e.g. top-left), highlight/shadow hue shift (warm highlights, cool shadows), outline colour rules.

## Motion and juice
Easing (ease-out for UI, overshoot for pop-in), durations (UI 0.15–0.25 s), screen shake, hit flash, particles.

## Do / Don't
Short list of the things that break the look.

## Generators
| Asset family | Generator | Output |
|---|---|---|
```

### `Design/palette.json`

The machine-readable copy of the palette. It is the one source that every art generator reads (`funke-game-art` → `scripts/pixelkit.py` loads it), so a colour changes in one place:

```json
{
  "name": "Warm dusk",
  "tokens": { "ink": "#2B1B2F", "surface": "#3D2A45", "primary": "#E26D5C", "positive": "#7BC47F",
              "danger": "#D9455F", "gold": "#F5B971", "text": "#FFF3D6", "textMuted": "#C9B8B0" },
  "ramps": { "grass": ["#1F3B2D", "#2F5D3A", "#4C8C4A", "#7DBB5C", "#B7E07A"],
             "stone": ["#2A2633", "#46404F", "#6A6273", "#958B9A", "#C9C0C8"] }
}
```

When runtime code needs the colours too (tinting UI states, damage flashes, team colours), a generator writes them into a Data Asset (`Assets/Data/Theme.dataasset` of a `Theme : DataAsset` class with `Color` fields), and scripts take it as a field. Don't hand-copy hex values into C#.

## 3. Show a style sample, then build

Before you produce whole asset families, render **one** sample image to `Design/style-sample.png`. It should hold the palette swatches with token names, one panel with a title and a button in its normal and pressed states, one character or tile at the real pixel density, and a line of body text in the chosen font. `funke-game-art` → `scripts/pixelkit.py sample` builds it from `palette.json`. Show it with the Read tool and your summary of both documents.

- **Modes 1 and 2:** ask "Does the look fit?" and wait. A change here costs one regenerate; after rollout it costs the whole asset family.
- **Mode 3:** show the sample and continue.

Then go back to `SKILL.md` → Workflow step 1 (plan) and keep both documents up to date as the game grows. If a decision changes, change the document first.
