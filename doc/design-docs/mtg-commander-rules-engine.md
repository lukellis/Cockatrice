> Mirrored verbatim from
> [jeffyche/fun-stuff/design-docs/mtg-commander-rules-engine.md](https://github.com/jeffyche/fun-stuff/blob/master/design-docs/mtg-commander-rules-engine.md)
> (fetched 2026-07-15) as the original design inspiration for this fork. Kept
> in-repo so it survives if the source repo changes or disappears. This fork does
> **not** implement the full doc — see
> [`COMMANDER_IMPLEMENTATION_STATUS.md`](../../COMMANDER_IMPLEMENTATION_STATUS.md)
> for a phase-by-phase tracking of what's actually built versus this proposal.
> Notably: this fork stayed on Cockatrice's original protocol/branding rather than
> forking to an independent ecosystem as §0 here suggests.
>
> **Direction update (2026-07-16):** the fork is now adopting this doc's §3 Phase 1
> `libcockatrice_rules/` library structure after all. The earlier phases were built
> directly into Cockatrice's existing files (behind `isCommanderGame()` gates), but
> that logic is being migrated into a dedicated `RulesEngine` home so future work
> lands in one place instead of scattered hooks. The fork is also being made **wholly
> Commander-only** — no other game type or format — so those gates are being removed
> entirely (the engine is always active). This is a *foundation-first* adoption:
> real rule *enforcement* (stack resolution, mana payment, combat — Phases 5–8's hard
> parts) still grows incrementally inside that engine later, not all at once. See
> [`COMMANDER_IMPLEMENTATION_STATUS.md`](../../COMMANDER_IMPLEMENTATION_STATUS.md)'s
> "Design & Implementation Review" and the Phase 1 tracking row for status.

# MTG Commander Rules Engine - Design Document

## Project: Cockatrice Fork for Magic: The Gathering Commander

### Executive Summary

This document outlines the design for forking Cockatrice to create a Magic: The Gathering-specific application with automated rule enforcement, starting with the Commander (EDH) format. The goal is to transform Cockatrice from a general tabletop simulator into a rules-enforcing MTG client.

---

## 0. Licensing & Fork Strategy

### Decision: Fork Cockatrice

We will fork the [Cockatrice repository](https://github.com/Cockatrice/Cockatrice) rather than building a standalone application.

### License: GPLv2

Cockatrice is licensed under **GNU General Public License v2 (GPLv2)**. This means our fork must:

| Requirement | Implication |
|-------------|-------------|
| **Copyleft** | Our derivative work must also be GPLv2 |
| **Source disclosure** | Must publish all source code |
| **Attribution** | Preserve original copyright notices |
| **Modification notices** | Document changes from upstream |

### What We Get from Forking

- ~130k lines of working Qt desktop client
- Servatrice game server with MySQL backend
- Oracle card database importer (MTGJSON integration)
- Protobuf-based client/server protocol (159 message types)
- Deck serialization and import/export
- Existing zone rendering and card display

### Fork Maintenance Strategy

1. **Initial fork**: Full clone of upstream `master`
2. **Rename**: Rebrand to distinguish from Cockatrice (e.g., "CommanderForge")
3. **Divergence**: Independent protocol version - no backward compatibility with Cockatrice servers
4. **Upstream sync**: Periodic review of upstream bug fixes (cherry-pick as needed)

---

## 1. Current Architecture Overview

### 1.1 Components

| Component | Location | Purpose |
|-----------|----------|---------|
| **Cockatrice** | `cockatrice/` | Qt-based desktop client (~130k lines) |
| **Servatrice** | `servatrice/` | Game server with MySQL backend |
| **Oracle** | `oracle/` | Card database import tool (MTGJSON) |
| **libcockatrice_card** | `libcockatrice_card/` | Card database and metadata |
| **libcockatrice_network** | `libcockatrice_network/` | Client/server networking |
| **libcockatrice_protocol** | `libcockatrice_protocol/` | 159 protobuf message definitions |
| **libcockatrice_deck_list** | `libcockatrice_deck_list/` | Deck serialization |

### 1.2 Current Game Logic (NO Rule Enforcement)

- **Phases**: 11 hardcoded phases (Untap → End/Cleanup)
- **Zones**: Hand, Library, Battlefield, Stack, Graveyard, Exile, Sideboard
- **Stack**: Visual zone only - no resolution mechanics
- **Priority**: Not implemented
- **Mana**: Not tracked (just visual counters)
- **Combat**: No attacker/blocker validation
- **Abilities**: Not parsed or executed

### 1.3 Key Extension Points

```
Server_Game (server_game.h)
├── activePlayer, activePhase tracking
├── gameTypes list for format detection
├── startingLifeTotal configuration
└── Virtual methods for extension

Server_Player (server_player.h)
├── setupZones() - zone initialization hook
├── clearZones() - cleanup hook
├── onCardBeingMoved() - card movement hook
└── All cmd*() methods are virtual

CardInfo (card_info.h)
├── QVariantHash properties (type, cmc, colors, xt)
├── Format legality via getLegalityProp()
└── Related cards for tokens/transforms
```

---

## 2. Commander Format Requirements

### 2.1 Deck Construction Rules
- 100 cards exactly (including commander)
- Singleton format (one copy per card, except basic lands)
- Commander must be a legendary creature (or designated card)
- All cards must match commander's color identity

### 2.2 Gameplay Rules
- 40 starting life (vs. 20 in standard formats)
- Command zone for commander
- Commander tax: +{2} for each previous cast from command zone
- 21 commander damage from a single commander = loss
- Multiplayer (typically 4 players)
- Free mulligan, then standard London mulligan

### 2.3 Core MTG Rules Needed
- Turn structure enforcement
- Priority system for casting/responding
- The Stack with LIFO resolution
- Mana pool and cost payment
- Combat phases (declare attackers/blockers, damage)
- Timing restrictions (sorcery speed vs. instant speed)
- State-based actions (0 life = loss, 0 toughness = death, eered abilities

---

## 3. Implementation Phases

### Phase 1: Foundation & Build Setup (2-3 weeks)

**Goal**: Get development environment running and create project structure.

**Tasks**:
1. Fork repository and set up development branch
2. Build locally on macOS (see Section 5)
3. Create new library: `libcockatrice_rules/`
4. Add Commander as a game type option
5. Set up testing infrastructure for rules engine

**New Directory Structure**:
```
libcockatrice_rules/
  libcockatrice/rules/
    core/
      rules_engine.h/.cpp
      game_state.h/.cpp
      action_validator.h/.cpp
    formats/
      format_interface.h
      commander_format.h/.cpp
    state/
      mana_pool.h/.cpp
      phase_manager.h/.cpp
```

---

### Phase 2: Commander Deck Validation (2-3 weeks)

**Goal**: Validate decks before game starts.

**Implementation**:
```cpp
class CommanderDeckValidator {
    ValidationResult validate(const DeckList* deck, CardDatabase* db) {
        // Check 100 cards exactly
        // Check singleton (except basic lands)
        // Check color identity matches commander
        // Check commander is legal (legendary creature or designated)
    }
};
```

**Files to Modify**:
- `libcockatrice_deck_list/deck_list.h` - Add validation hook
- `servatrice/src/` - Add deck validation before game start
- `cockatrice/src/interface/` - Show validation errors in UI

---

### Phase 3: Command Zone & Commander Tracking (3-4 weeks)

**Goal**: Add command zone and commander-specific mechanics.

**New Zone**: Add "command" zone type
```cpp
// In Server_Player::setupZones()
addZone(new Server_CardZone(this, "command", false, ServerInfo_Zone::PublicZone));
```

**Commander State Tracking**:
```cpp
struct CommanderState {
    int playerId;
    QString commanderName;
    int castCount;           // For commander tax
    bool inCommandZone;
    QMap<int, int> damageDealtTo;  // opponent -> damage
};
```

**Commander Damage UI**: Add per-opponent commander damage counters.

---

### Phase 4: Basic Turn Structure Enforcement (4-6 weeks)

**Goal**: Enforce phase progression and mandatory actions.

**Implementation**:
- Automatic untap at untap step
- Automatic draw at draw step (skip on turn 1 in multiplayer)
- Phase advancement requires explicit action or timer
- Discard to hand size at end step

**Files to Modify**:
- `libcockatrice_network/.../server_game.cpp` - Add phase enforcement
- `cockatrice/src/game/phase.cpp` - Add phase callbacks

---

### Phase 5: Priority & Stack System (6-8 weeks)

**Goal**: Implement priority passing and stack resolution.

**New Classes**:
```cpp
class Server_Stack {
    QList<StackObject*> stackObjects;  // LIFO
    void push(StackObject* obj);
    StackObject* pop();
    void resolveTop();
};

class Server_PriorityManager {
    int playerWithPriority;
    QSet<int> playersPassedSinceLastAction;
    void passPriority(int playerId);
    void checkAllPassed();  // Resolve or advance phase
};
```

**New Protocol Messages**:
- `Command_PassPriority`
- `Command_CastSpell`
- `Event_PriorityChanged`
- `Event_StackObjectAdded`
- `Event_StackObjectResolved`

---

### Phase 6: Mana System (4-5 weeks)

**Goal**: Track mana pools and validate cost payment.

**Implementation**:
```cpp
class ManaPool {
    QMap<ManaColor, int> pool;  // W, U, B, R, G, C
    void add(ManaColor color, int amount);
    bool canPay(const ManaCost& cost) const;
    void pay(const ManaCost& cost);
    void empty();  // At phase end
};

class ManaCost {
    static ManaCost parse(const QString& manaCostString);  // "{2}{U}{U}" -> cost
};
```

---

### Phase 7: Card Ability System (8-12 weeks)

**Goal**: Parse and execute card abilities.

**Approach**: Hybrid pattern parsing + community-curated data

**Increment 1: Keywords** (2-3 weeks)
- Parse evergreen keywords (Flying, Trample, Lifelink, etc.)
- Display parsed keywords in UI

**Increment 2: Mana Abilities** (2-3 weeks)
- Parse "{T}: Add {X}" patterns
- Auto-activate from context menu

**Increment 3: Triggered Abilities** (3-4 weeks)
- Parse ETB triggers ("When ~ enters the battlefield")
- Parse death triggers ("When ~ dies")
- Create trigger queue with APNAP ordering

**Increment 4: Community Data** (ongoing)
- Create `abilities.xml` for complex cards
- Community contribution workflow

---

### Phase 8: Combat System (4-6 weeks)

**Goal**: Enforce combat rules.

**Implementation**:
```cpp
class CombatManager {
    QList<AttackerDeclaration> attackers;
    QList<BlockerDeclaration> blockers;

    Response::ResponseCode declareAttackers(...);
    Response::ResponseCode declareBlockers(...);
    void assignCombatDamage();
    void dealCombatDamage();
};
```

**Validations**:
- Creature can attack (not summoning sick, not tapped)
- Legal blocking assignments
- Damage assignment order for multiple blockers
- First strike, double strike, trample handling

---

### Phase 9: State-Based Actions (3-4 weeks)

**Goal**: Automatically check and apply state-based actions.

**SBAs to Implement**:
- Player at 0 or less life loses
- Creature with 0 or less toughness dies
- Creature with lethal damage marked dies
- Planeswalker with 0 loyalty is sacrificed
- Player with 10+ poison counters loses
- Player who took 21+ commander damage from one commander loses
- Legend rule (keep one, sacrifice others)
- Player who drew from empty library loses

---

## 4. Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     COCKATRICE CLIENT                        │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │  Game UI    │  │ Stack View  │  │ Priority Indicator  │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└───────────────────────────────────────────────────────────────┘
                              │
                              │ Protobuf Messages
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      SERVATRICE SERVER                       │
│  ┌─────────────────────────────────────────────────────┐    │
│  │                    Server_Game                        │    │
│  │  ┌──────────────┐  ┌───────────────────────────┐    │    │
│  │  │ Server_Stack │  │ Server_PriorityManager    │    │    │
│  │  └──────────────┘  └───────────────────────────┘    │    │
│  └─────────────────────────────────────────────────────┘    │
│                           │                                   │
│  ┌─────────────────────────────────────────────────────┐    │
│  │                   RulesEngine                         │    │
│  │  ┌────────────┐ ┌────────────┐ ┌─────────────────┐  │    │
│  │  │ Validator  │ │ ManaSystem │ │ CombatManager   │  │    │
│  │  └────────────┘ └────────────┘ └─────────────────┘  │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

---

## 5. Local Development Setup (macOS)

### 5.1 Prerequisites

```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install Homebrew dependencies
brew install cmake qt@6 protobuf ninja

# Optional: ccache for faster rebuilds
brew install ccache
```

### 5.2 Build Commands

```bash
# Clone with submodules
git clone --recursive https://github.com/Cockatrice/Cockatrice.git
cd Cockatrice

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake .. -G Ninja \
    -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6) \
    -DWITH_SERVER=1

# Build
ninja

# Run Cockatrice
open release/cockatrice/Cockatrice.app
```

### 5.3 Running Components

```bash
# Start local server
./release/servatrice/servatrice --config servatrice.ini

# Run client
open release/cockatrice/Cockatrice.app

# Run Oracle (card database import)
open release/oracle/Oracle.app/Contents/MacOS/oracle
```

### 5.4 First-Time Setup

1. **Card Database**: Run Oracle and import from MTGJSON
2. **Card Images**: Configure in Cockatrice preferences (Scryfall is common)
3. **Local Server**: Connect to `localhost:4747` with any username

### 5.5 CMake Options

| Flag | Purpose |
|------|---------|
| `-DWITH_SERVER=1` | Build Servatrice server |
| `-DWITH_CLIENT=0` | Skip Cockatrice client |
| `-DWITH_ORACLE=0` | Skip Oracle tool |
| `-DTEST=1` | Enable regression tests |
| `-DCMAKE_BUILD_TYPE=Debug` | Debug build |

### 5.6 Running Tests

```bash
cd build
cmake .. -DTEST=1
ninja
ctest --output-on-failure
```

---

## 6. Critical Files Reference

### Server-Side Game Logic
- [server_game.h](../libcockatrice_network/libcockatrice/network/server/remote/game/server_game.h) - Core game class
- [server_player.h](../libcockatrice_network/libcockatrice/network/server/remote/game/server_player.h) - Player management
- [server_abstract_player.h](../libcockatrice_network/libcockatrice/network/server/remote/game/server_abstract_player.h) - Virtual command handlers
- [server_cardzone.h](../libcockatrice_network/libcockatrice/network/server/remote/game/server_cardzone.h) - Zone management

### Card Database
- [card_info.h](../libcockatrice_card/libcockatrice/card/card_info.h) - Card properties and metadata
- [card_database.h](../libcockatrice_card/libcockatrice/card/database/card_database.h) - Card lookup
- [game_specific_terms.h](../libcockatrice_card/libcockatrice/card/game_specific_terms.h) - Property name constants

### Protocol Definitions
- [libcockatrice_protocol/pb/](../libcockatrice_protocol/libcockatrice/protocol/pb/) - All protobuf messages

### Client Game UI
- [cockatrice/src/game/](../cockatrice/src/game/) - Game rendering and interaction
- [cockatrice/src/game/zones/](../cockatrice/src/game/zones/) - Zone implementations
- [cockatrice/src/game/player/](../cockatrice/src/game/player/) - Player actions

---

## 7. Timeline Estimate

| Phase | Description | Duration |
|-------|-------------|----------|
| 1 | Foundation & Build Setup | 2-3 weeks |
| 2 | Commander Deck Validation | 2-3 weeks |
| 3 | Command Zone & Commander Tracking | 3-4 weeks |
| 4 | Turn Structure Enforcement | 4-6 weeks |
| 5 | Priority & Stack System | 6-8 weeks |
| 6 | Mana System | 4-5 weeks |
| 7 | Card Ability System | 8-12 weeks |
| 8 | Combat System | 4-6 weeks |
| 9 | State-Based Actions | 3-4 weeks |

**Total Estimated Duration**: 36-51 weeks (9-13 months)

---

## 8. Recommended Starting Point

Begin with these low-risk, high-value features:

1. **Deck Validation** (Phase 2) - No gameplay changes, immediate value
2. **Commander Damage Tracking** (Phase 3) - Uses existing counter system
3. **40 Life Starting Total** - Already configurable via `startingLifeTotal`

This approach validates the architecture before deeper integration.

---

## 9. Key Design Decisions

1. **Assisted Mode**: Rules engine validates and suggests legal actions, but players can override for casual/educational play. Illegal actions show warnings but aren't blocked.
2. **Separate Ecosystem**: New fork with independent protocol - no backward compatibility with Cockatrice servers. This allows faster evolution without constraints.
3. **Server-Side Validation**: All rules checked on server for consistency, but overrides are allowed with confirmation.
4. **Format Abstraction**: Commander implements a `FormatInterface` for future formats (Modern, Standard, etc.)
5. **Hybrid Ability Parsing**: Pattern matching + community-curated data for complex cards
6. **Graceful Degradation**: Unsupported cards fall back to manual play with oracle text display

---

## 10. Assisted Mode Implementation

Since we've chosen **Assisted Mode**, here's how validation will work:

### Action Flow
```
Player Action → Validator Check → Result
                     │
         ┌──────────┴──────────┐
         ▼                      ▼
    Action Legal           Action Illegal
         │                      │
         ▼                      ▼
    Execute                Show Warning Dialog
    Normally            "This appears illegal: [reason]"
                               │
                    ┌──────────┴──────────┐
                    ▼                      ▼
              [Allow Anyway]         [Cancel]
                    │
                    ▼
              Execute with
              "Override" flag
              (logged in replay)
```

### UI Indicators
- **Green highlight**: Legal targets/actions
- **Yellow highlight**: Questionable but allowed (e.g., uncertain timing)
- **Red highlight + tooltip**: Illegal action with explanation
- **Override button**: "Allow anyway (casual mode)"

### Benefits of Assisted Mode
1. **Learning tool**: New players see why actions are illegal
2. **Casual play**: Friends can house-rule as needed
3. **Graceful degradation**: When ability parsing fails, manual play still works
4. **Testing**: Easier to test incomplete rules implementations

---

## 11. Verification Plan

### Initial Build Verification
1. Build all components successfully
2. Create a Commander game with 2+ players
3. Validate deck import and card database
4. Test basic game flow (draw, play, attack)
5. Verify network synchronization between clients

### Testing Rules Engine (as implemented)
1. Unit tests for each component (validator, mana, combat)
2. Integration tests with mocked game state
3. Regression tests for top 1000 Commander cards
4. Multiplayer stress testing (4 players, complex board states)

---

## 12. UI Design & Enhancements

### 12.1 Design Philosophy

Transform Cockatrice from a generic tabletop simulator into an MTG-native experience:

| Principle | Description |
|-----------|-------------|
| **MTG-First** | UI built around MTG concepts, not generic card games |
| **Information Density** | Show game state at a glance without clutter |
| **Reduce Clicks** | Common actions should be 1-2 clicks max |
| **Visual Feedback** | Every game action has clear visual confirmation |
| **Multiplayer Native** | Designed for 4 players, scales down to 2 |

---

### 12.2 Multiplayer Layout (4-Player Commander)

#### Screen Layout
```
┌─────────────────────────────────────────────────────────────────┐
│  ┌─────────────────────┐         ┌─────────────────────┐        │
│  │   OPPONENT LEFT     │         │   OPPONENT RIGHT    │        │
│  │  ┌───┐ Life: 38     │         │  Life: 40  ┌───┐    │        │
│  │  │Cmd│ CmdDmg: 6,0,0│         │  0,0,0    │Cmd│    │        │
│  │  └───┘              │         │            └───┘    │        │
│  │  [Battlefield]      │         │    [Battlefield]    │        │
│  └─────────────────────┘         └─────────────────────┘        │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                    OPPONENT ACROSS                         │  │
│  │         ┌───┐  Life: 35   CmdDmg: 0, 3, 0                 │  │
│  │         │Cmd│  [── Battlefield ──]                        │  │
│  │         └───┘                                              │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                  │
│              ┌─────────────────────────────┐                    │
│              │          STACK              │                    │
│              │  ┌─────┐ ┌─────┐ ┌─────┐   │                    │
│              │  │ Sp3 │→│ Sp2 │→│ Sp1 │   │                    │
│              │  └─────┘ └─────┘ └─────┘   │                    │
│              │     [Resolve] [Respond]    │                    │
│              └─────────────────────────────┘                    │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                      YOUR BATTLEFIELD                      │  │
│  │  ┌─────────────────────────────────────────────────────┐  │  │
│  │  │ Lands:    [Plains][Island][Sol Ring][Cmd Tower]     │  │  │
│  │  ├─────────────────────────────────────────────────────┤  │  │
│  │  │ Creatures:[Thalia][Sun Titan][Swords Equipped]      │  │  │
│  │  ├─────────────────────────────────────────────────────┤  │  │
│  │  │ Other:    [Rhystic Study][Smothering Tithe]         │  │  │
│  │  └─────────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌──────┐ ┌─────────────────────────────────┐ ┌──────────────┐  │
│  │ Cmd  │ │           YOUR HAND             │ │  Mana Pool   │  │
│  │ Zone │ │ [Card][Card][Card][Card][Card]  │ │ ⚪⚪🔵🔵⬡⬡ │  │
│  │ ┌──┐ │ │          7 cards                │ │   4 floating │  │
│  │ │🂡 │ │ └─────────────────────────────────┘ └──────────────┘  │
│  │ └──┘ │                                                        │
│  │ Tax:4│  Life: 40  │ Phase: Main 1 │ Priority: YOU            │
│  └──────┘                                                        │
└─────────────────────────────────────────────────────────────────┘
```

#### Opponent Panel Components
Each opponent panel shows:
- **Commander thumbnail** (click to enlarge)
- **Life total** (large, prominent)
- **Commander damage received** (from each opponent's commander)
- **Condensed battlefield** (expandable on hover/click)
- **Graveyard/Exile count badges**

#### Battlefield Organization
Automatic card organization by permanent type:
```cpp
enum class BattlefieldRow {
    Lands,          // Bottom row - tap for mana
    Creatures,      // Middle row - combat participants
    Artifacts,      // Grouped with enchantments
    Enchantments,   // Non-aura enchantments
    Planeswalkers,  // Special frame with loyalty
    Other           // Anything else
};
```

---

### 12.3 Stack & Priority System UI

#### Visual Stack Display
```
┌─────────────────────────────────────────┐
│  THE STACK (resolves top to bottom)     │
│  ┌─────────────────────────────────┐    │
│  │ 3. Counterspell       [BLUE]    │ ←── Top (resolves first)
│  │    Target: Lightning Bolt       │    │
│  │    Controller: Opponent A       │    │
│  └─────────────────────────────────┘    │
│              ↓                          │
│  ┌─────────────────────────────────┐    │
│  │ 2. Lightning Bolt     [RED]     │    │
│  │    Target: Your Sun Titan       │    │
│  │    Controller: Opponent B       │    │
│  └─────────────────────────────────┘    │
│              ↓                          │
│  ┌─────────────────────────────────┐    │
│  │ 1. Sun Titan ETB Trigger        │ ←── Bottom (resolves last)
│  │    Target: Swords to Plowshares │    │
│  │    Controller: You              │    │
│  └─────────────────────────────────┘    │
│                                         │
│  [Pass Priority]  [Hold Priority]       │
└─────────────────────────────────────────┘
```

#### Priority Indicator States
```
┌────────────────────────────────────────┐
│ Priority: YOUR TURN                    │  ← Green, pulsing border
│ [Pass] [Cast/Activate] [Full Control]  │
└────────────────────────────────────────┘

┌────────────────────────────────────────┐
│ Priority: Waiting for Opponent A...    │  ← Gray, subtle animation
│ [Request Full Control]                 │
└────────────────────────────────────────┘

┌────────────────────────────────────────┐
│ ⚠️ RESPOND? Stack has 2 items         │  ← Yellow, attention-grabbing
│ [Pass] [Respond with...]   Auto: 10s   │
└────────────────────────────────────────┘
```

#### Priority Pass Modes
| Mode | Behavior |
|------|----------|
| **Auto-Pass** | Pass priority when no legal responses (default) |
| **Confirm Responses** | Prompt only when responses available |
| **Full Control** | Always prompt at every priority window |
| **Bluff Mode** | Add random delays to hide lack of responses |

---

### 12.4 Combat UI

#### Declare Attackers Phase
```
┌─────────────────────────────────────────────────────────────┐
│  DECLARE ATTACKERS - Click creatures to attack             │
│                                                             │
│  Your Creatures:                                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ Thalia   │  │Sun Titan │  │ Recruiter│  │ Mother   │   │
│  │  2/1     │  │  6/6     │  │   1/1    │  │   1/1    │   │
│  │ [Attack] │  │ [Attack] │  │[Summoning│  │  [Tap    │   │
│  │ ☐Opp A   │  │ ☐Opp A   │  │  Sick]   │  │Ability]  │   │
│  │ ☐Opp B   │  │ ☐Opp B   │  └──────────┘  └──────────┘   │
│  │ ☐Opp C   │  │ ☐Opp C   │                               │
│  └──────────┘  └──────────┘                               │
│                                                             │
│        [Confirm Attackers]    [Cancel]                     │
└─────────────────────────────────────────────────────────────┘
```

#### Declare Blockers Phase (Defending Player View)
```
┌─────────────────────────────────────────────────────────────┐
│  DECLARE BLOCKERS - Drag your creatures to block           │
│                                                             │
│  Attacking:              Your Blockers:                     │
│  ┌──────────┐            ┌──────────┐  ┌──────────┐        │
│  │ Sun Titan│───────────→│ Wall of  │  │ Soldier │        │
│  │   6/6    │            │  Omens   │  │  Token  │        │
│  │ Trample  │            │   0/4    │  │   1/1   │        │
│  └──────────┘            └──────────┘  └──────────┘        │
│                               │                             │
│  ┌──────────┐                 │                             │
│  │  Thalia  │←────────────────┘  (double block)            │
│  │   2/1    │                                               │
│  │First Strk│                                               │
│  └──────────┘                                               │
│                                                             │
│        [Confirm Blocks]    [No Blocks]                     │
└─────────────────────────────────────────────────────────────┘
```

#### Damage Assignment (Trample/Multiple Blockers)
```
┌─────────────────────────────────────────────────────────────┐
│  ASSIGN COMBAT DAMAGE - Sun Titan (6 damage to assign)     │
│                                                             │
│  Blocked by:                                                │
│  ┌──────────────────────────────────────┐                  │
│  │  Wall of Omens (0/4)    [4 damage ▼] │ ← Slider/input   │
│  │  Soldier Token (1/1)    [1 damage ▼] │                  │
│  ├──────────────────────────────────────┤                  │
│  │  Trample to Player      [1 damage  ] │ ← Remaining      │
│  └──────────────────────────────────────┘                  │
│                                                             │
│  ⚠️ Must assign lethal before moving to next blocker       │
│                                                             │
│        [Confirm Damage]    [Auto-Assign]                   │
└─────────────────────────────────────────────────────────────┘
```

#### Combat Visualization
- **Tapped attackers** rotate 90°
- **Attack arrows** from creatures to defending player/planeswalker
- **Block arrows** from blockers to attackers (different color)
- **Damage numbers** float above creatures during damage step
- **Death X** overlay on creatures that will die

---

### 12.5 Mana System UI

#### Mana Pool Display
```
┌─────────────────────────────────────┐
│         MANA POOL                   │
│  ┌───┬───┬───┬───┬───┬───┐        │
│  │ W │ U │ B │ R │ G │ C │        │
│  │ 2 │ 3 │ 0 │ 0 │ 1 │ 2 │        │
│  │⚪⚪│🔵🔵│   │   │🟢 │◇◇ │        │
│  │   │🔵 │   │   │   │   │        │
│  └───┴───┴───┴───┴───┴───┘        │
│  Total: 8 mana floating            │
│                                     │
│  [Empty Pool]  Phase: Main 1       │
│  ⚠️ Pool empties at phase end      │
└─────────────────────────────────────┘
```

#### Land Tapping Interface
```
When casting a spell:

┌─────────────────────────────────────────────────────────────┐
│  Cast: Wrath of God  (Cost: 2WW)                           │
│                                                             │
│  Suggested Payment:        Your Lands:                      │
│  ┌─────────────────┐      ┌────────┐ ┌────────┐            │
│  │ Auto-tap:       │      │ Plains │ │ Plains │            │
│  │ Plains x2       │      │  [W]   │ │  [W]   │            │
│  │ Command Tower   │      │   ✓    │ │   ✓    │            │
│  │ Sol Ring        │      └────────┘ └────────┘            │
│  └─────────────────┘      ┌────────┐ ┌────────┐            │
│                           │CmdTower│ │Sol Ring│            │
│  [Accept] [Manual Tap]    │ [Any]  │ │  [2]   │            │
│                           │   ✓    │ │   ✓    │            │
│                           └────────┘ └────────┘            │
└─────────────────────────────────────────────────────────────┘
```

#### Auto-Tap Algorithm Priority
1. Use floating mana first
2. Prefer lands that produce only the needed colors
3. Tap dual lands last (preserve flexibility)
4. Consider untap effects (skip creatures with tap abilities)
5. User can always override with manual selection

#### Mana Ability Activation
Right-click on mana source shows:
```
┌─────────────────────┐
│ Command Tower       │
│ ─────────────────── │
│ [T]: Add W          │
│ [T]: Add U          │
│ [T]: Add B          │
│ [T]: Add R          │
│ [T]: Add G          │
└─────────────────────┘
```

---

### 12.6 Commander-Specific UI Elements

#### Command Zone Panel
```
┌────────────────────────────────────┐
│  COMMAND ZONE                      │
│  ┌────────────────────────────┐   │
│  │                            │   │
│  │     [Commander Image]      │   │
│  │      Atraxa, Praetors'     │   │
│  │          Voice             │   │
│  │         {G}{W}{U}{B}       │   │
│  │                            │   │
│  │  Cast Cost: 4GWUB + (4)    │   │ ← Shows commander tax
│  │  Times Cast: 2             │   │
│  │                            │   │
│  └────────────────────────────┘   │
│                                    │
│  [Cast Commander]  [View Card]    │
└────────────────────────────────────┘
```

#### Commander Damage Tracker
```
┌──────────────────────────────────────────────────┐
│  COMMANDER DAMAGE RECEIVED                       │
│  ┌────────────┬────────────┬────────────┐       │
│  │ Opponent A │ Opponent B │ Opponent C │       │
│  │  Kenrith   │  Korvold   │   Urza     │       │
│  │            │            │            │       │
│  │    12/21   │    3/21    │    0/21    │       │
│  │ ████████░░ │ ██░░░░░░░░ │ ░░░░░░░░░░ │       │
│  │  ⚠️ 9 more │            │            │       │
│  └────────────┴────────────┴────────────┘       │
│                                                  │
│  💀 = 21 damage from one commander = YOU LOSE   │
└──────────────────────────────────────────────────┘
```

#### Partner Commanders Layout
```
┌─────────────────────────────────────┐
│  COMMAND ZONE (Partners)           │
│  ┌───────────────┬───────────────┐ │
│  │   Thrasios    │    Tymna      │ │
│  │   {G}{U}      │    {W}{B}     │ │
│  │   Cost: 2GU   │   Cost: 1WB   │ │
│  │   Tax: +2     │   Tax: +0     │ │
│  │   [Cast]      │   [Cast]      │ │
│  └───────────────┴───────────────┘ │
│  Color Identity: WUBG              │
└─────────────────────────────────────┘
```

---

### 12.7 Card Interaction & Information

#### Hover Preview
Large card image appears on hover (configurable position):
```
┌─────────────────────────────────────┐
│                                     │
│     ┌─────────────────────────┐    │
│     │                         │    │
│     │    [Full Card Image]    │    │
│     │      300x420 pixels     │    │
│     │                         │    │
│     │                         │    │
│     └─────────────────────────┘    │
│                                     │
│     Oracle Text:                   │
│     Flying, vigilance, deathtouch, │
│     lifelink                       │
│     At the beginning of your end   │
│     step, proliferate.             │
│                                     │
│     Rulings: [Show 3 rulings]      │
└─────────────────────────────────────┘
```

#### Targeting Interface
```
When casting targeted spell:

┌─────────────────────────────────────────────────────────────┐
│  Cast: Swords to Plowshares                                │
│  Target: Select one creature                                │
│                                                             │
│  Valid Targets (highlighted green):                        │
│  • Your creatures: Sun Titan, Thalia                       │
│  • Opponent A: Korvold, Dockside Extortionist             │
│  • Opponent B: Thassa (NOT VALID - indestructible)        │
│  • Opponent C: [No creatures]                              │
│                                                             │
│  Click a valid target or [Cancel]                          │
└─────────────────────────────────────────────────────────────┘
```

#### Zone Browsers
Modal dialogs for hidden zone inspection:
```
┌─────────────────────────────────────────────────────────────┐
│  GRAVEYARD - Opponent A (14 cards)          [✕]            │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ Search: [________________]  Filter: [All Types ▼]   │   │
│  ├─────────────────────────────────────────────────────┤   │
│  │ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐   │   │
│  │ │Card1│ │Card2│ │Card3│ │Card4│ │Card5│ │Card6│   │   │
│  │ └─────┘ └─────┘ └─────┘ └─────┘ └─────┘ └─────┘   │   │
│  │ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐   │   │
│  │ │Card7│ │Card8│ │Card9│ │Cd10 │ │Cd11 │ │Cd12 │   │   │
│  │ └─────┘ └─────┘ └─────┘ └─────┘ └─────┘ └─────┘   │   │
│  └─────────────────────────────────────────────────────┘   │
│  Creatures: 6 | Instants: 4 | Sorceries: 2 | Other: 2     │
└─────────────────────────────────────────────────────────────┘
```

---

### 12.8 Triggered Abilities & Reminders

#### Trigger Queue Display
```
┌─────────────────────────────────────────────────────────────┐
│  TRIGGERS WAITING TO BE PUT ON STACK                       │
│  (You control these - choose order)                        │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ 1. [Rhystic Study] - Opponent cast a spell          │   │
│  │    "You may draw a card unless they pay {1}"        │   │
│  │    [Put on Stack]  [Order: 1st ▼]                   │   │
│  ├─────────────────────────────────────────────────────┤   │
│  │ 2. [Smothering Tithe] - Opponent drew a card        │   │
│  │    "Create a Treasure unless they pay {2}"          │   │
│  │    [Put on Stack]  [Order: 2nd ▼]                   │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  [Add All to Stack (selected order)]  [Auto-order: APNAP] │
└─────────────────────────────────────────────────────────────┘
```

#### May Ability Prompts
```
┌─────────────────────────────────────┐
│  Rhystic Study Trigger             │
│  ─────────────────────────────────  │
│  Opponent A cast Sol Ring          │
│  They chose not to pay {1}         │
│                                     │
│  Draw a card?                      │
│                                     │
│  [Yes, Draw]    [No, Decline]      │
│  ☐ Always draw (remember choice)   │
└─────────────────────────────────────┘
```

---

### 12.9 Phase Indicator & Turn Tracker

#### Phase Timeline
```
┌─────────────────────────────────────────────────────────────────────────┐
│ Turn 7 - Your Turn                                                      │
│ ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐   │
│ │Untap│Upkp │Draw │Main1│ BGN │ ATK │ BLK │ DMG │ END │Main2│ End │   │
│ │  ✓  │  ✓  │  ✓  │ ▶▶▶ │     │     │     │     │     │     │     │   │
│ └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘   │
│                        ▲                                                │
│                   CURRENT PHASE                                         │
│                                                                         │
│ [→ Next Phase]  [→ Go to Combat]  [→ End Turn]                        │
└─────────────────────────────────────────────────────────────────────────┘
```

#### Turn Order Display (Multiplayer)
```
┌────────────────────────────────────────┐
│  TURN ORDER                            │
│  ┌────┐  ┌────┐  ┌────┐  ┌────┐      │
│  │ ▶▶ │→ │    │→ │    │→ │    │→ ─┐ │
│  │You │  │Op A│  │Op B│  │Op C│   │ │
│  └────┘  └────┘  └────┘  └────┘   │ │
│    ▲                               │ │
│    └───────────────────────────────┘ │
│  Turn: 7  |  Active Player: You      │
└────────────────────────────────────────┘
```

---

### 12.10 Accessibility & Preferences

#### Color Blind Mode
| Standard | Deuteranopia | Protanopia |
|----------|--------------|------------|
| Green mana (🟢) | Blue outline + "G" | Blue outline + "G" |
| Red mana (🔴) | Orange + "R" | Yellow + "R" |
| Priority (green border) | Blue pulsing border | Blue pulsing border |

#### UI Scaling Options
- **Compact**: Smaller cards, more board visible
- **Standard**: Balanced view
- **Large**: Bigger cards, easier reading
- **Accessibility**: Maximum text size, high contrast

#### Sound Cues
| Event | Sound |
|-------|-------|
| Your priority | Soft chime |
| Spell targeting you | Alert tone |
| Your creature dies | Low thud |
| Game winning/losing | Distinct fanfare/defeat |
| Timer warning (10s) | Tick-tock |

---

### 12.11 Implementation Priority

| Priority | Component | Effort | Dependencies |
|----------|-----------|--------|--------------|
| **P0** | 4-player layout | Medium | None |
| **P0** | Phase indicator | Low | None |
| **P0** | Card hover preview | Low | None |
| **P1** | Stack visualization | Medium | Stack system (Phase 5) |
| **P1** | Priority indicator | Medium | Priority system (Phase 5) |
| **P1** | Commander damage tracker | Low | Commander tracking (Phase 3) |
| **P1** | Command zone panel | Low | Command zone (Phase 3) |
| **P2** | Mana pool display | Medium | Mana system (Phase 6) |
| **P2** | Auto-tap interface | High | Mana system (Phase 6) |
| **P2** | Combat UI (attackers/blockers) | High | Combat system (Phase 8) |
| **P3** | Trigger queue UI | Medium | Ability system (Phase 7) |
| **P3** | Zone browsers | Low | None |
| **P3** | Targeting arrows | Medium | Ability system (Phase 7) |

---

### 12.12 Cockatrice UI Files to Modify

```
cockatrice/src/
├── game/
│   ├── board/
│   │   ├── game_board.cpp          # Main board layout → 4-player grid
│   │   └── battlefield_zone.cpp    # Card organization by type
│   ├── zones/
│   │   ├── zone_view_zone.cpp      # Zone browser modals
│   │   └── stack_zone.cpp          # NEW: Visual stack
│   ├── player/
│   │   ├── player_area.cpp         # Opponent panels
│   │   └── player_info.cpp         # Life/commander damage display
│   ├── cards/
│   │   ├── card_item.cpp           # Hover preview trigger
│   │   └── card_info_widget.cpp    # Large card display
│   └── phases/
│       ├── phase_indicator.cpp     # NEW: Phase timeline
│       └── priority_display.cpp    # NEW: Priority UI
├── widgets/
│   ├── mana_pool_widget.cpp        # NEW: Mana pool display
│   ├── commander_panel.cpp         # NEW: Command zone UI
│   └── trigger_queue_widget.cpp    # NEW: Trigger ordering
└── dialogs/
    ├── combat_dialog.cpp           # NEW: Combat assignment
    └── targeting_dialog.cpp        # NEW: Target selection
```
