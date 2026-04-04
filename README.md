This repo contains a collection of board games, written in C (C99 and built using GNU Make), for the purpose of exploring/experimenting with reinforcement learning agents.

Available Games
===============

| Game                              | Tic-Tac-Toe |
|-----------------------------------|:-----------:|
| Num Players                       |      2      |
| State Size                        |      3      |
| Max Actions                       |      9      |
| Observation: Number of Dimensions |      3      |
| Observation: Dimensions           |  [2, 3, 3]  |
| Observation: Size                 |      18     |
| Features: Number of Dimensions    |      3      |
| Features: Dimensions              |  [2, 3, 3]  |
| Features: Size                    |      18     |

**Note:** Each game also defines a `STRING_BUF_SIZE` macro.

This is not shown in the table above as it is a derived constant for internal formatting and is not relevant to agent integration.

Building
========

The main build you will want to do is to build the static library, which will be output to `bin/board_games_lib.a`:

```shell
make lib
```

From there, you would include the headers in `include/*` and `bin/board_games_lib.a` in your project.

I've also included the ability to compile a "demo" executable, this is compiled by the `demo` target:

```shell
make demo
```

This is intended as a way to demo or smoke-test the game engine you are implementing.

Engine Structure
================

Each game must completely fulfil the `Game` interface located at [include/board_game.h](include/board_game.h). This allows for the following generic game loop:

```mermaid
flowchart TD
    init["init()"]
    gcp["get_current_player()"]
    gva["get_valid_actions()"]
    obs["get_observation()<br>get_features()"]
    agent["Agent selects action"]
    aa["apply_action()"]
    terminal{"is_terminal?"}
    outcome["get_outcome()"]

    init --> gcp
    gcp --> gva
    gva --> obs
    obs -. "feeds agent's<br>decision" .-> agent
    gva --> agent
    agent --> aa
    aa --> terminal
    terminal -- "no" --> gcp
    terminal -- "yes" --> outcome

    style init fill:#f1efe8,stroke:#5f5e5a,color:#2c2c2a
    style gcp fill:#eeedfe,stroke:#534ab7,color:#26215c
    style gva fill:#eeedfe,stroke:#534ab7,color:#26215c
    style obs fill:#f1efe8,stroke:#5f5e5a,stroke-dasharray:5 5,color:#2c2c2a
    style agent fill:#f1efe8,stroke:#5f5e5a,stroke-dasharray:5 5,color:#2c2c2a
    style aa fill:#eeedfe,stroke:#534ab7,color:#26215c
    style terminal fill:#e1f5ee,stroke:#0f6e56,color:#04342c
    style outcome fill:#faece7,stroke:#993c1d,color:#4a1b0c
```

See also [src/main.c](src/main.c) for a working example.

The `Game` Interface
====================

The `Game` struct (defined in [include/board_game.h](include/board_game.h)) is a vtable of function pointers and metadata that every game implementation must provide. All state is stored externally in a caller-allocated `uint64_t` array - the `Game` itself holds no mutable state.

**Note** that state is intended as a semi-opaque container for the game state. Agents are expected to observe the state of the game via the `get_observation()` or `get_features()` functions - not by inspecting the state directly! This is because some board games such as Liar's Dice or Poker are incomplete/hidden information games, so their state arrays will contain the **entire** information of the game state, which is not available to each player. `get_observation()` and `get_features()` expose only the information that a specific player should see.

Required Macros
---------------

Every game must define the following compile-time macros, each prefixed with a namespace (e.g. `TTT_` for Tic-Tac-Toe). These are used by callers to allocate correctly-sized buffers before calling into any `Game` function.

|             Macro             |                                                     Description                                                     |
|:-----------------------------:|:-------------------------------------------------------------------------------------------------------------------:|
| `<NAMESPACE>_NUM_PLAYERS`     | Number of players in the game.                                                                                      |
| `<NAMESPACE>_STATE_SIZE`      | Number of `uint64_t` elements required to represent the full game state.                                            |
| `<NAMESPACE>_MAX_NUM_ACTIONS` | Maximum number of actions that `get_valid_actions()` can return in any state.                                       |
| `<NAMESPACE>_STRING_BUF_SIZE` | Minimum `char` buffer size (including null terminator) that `to_string()` requires.                                 |
| `<NAMESPACE>_OBS_NDIMS`       | Number of dimensions in the observation tensor (e.g. `1` for a flat vector, `3` for channels × rows × cols).        |
| `<NAMESPACE>_OBS_SIZE`        | Total number of elements in the observation. Must equal the product of the first `OBS_NDIMS` entries in `obs_dims`. |
| `<NAMESPACE>_FEATURES_NDIMS`  | Number of dimensions in the features tensor.                                                                        |
| `<NAMESPACE>_FEATURES_SIZE`   | Total number of elements in the features tensor.                                                                    |

Function Pointers
-----------------

|       Function       |                               Signature                               |                                                      Description                                                      |
|:--------------------:|:---------------------------------------------------------------------:|:---------------------------------------------------------------------------------------------------------------------:|
| `init`               | `void (const void* config, uint64_t state[])`                         | Initialise `state` to the start of the game. `config` is game-specific (may be `NULL`).                               |
| `get_current_player` | `uint64_t (const uint64_t state[])`                                   | Return the zero-based index of the player whose turn it is.                                                           |
| `get_valid_actions`  | `uint64_t (const uint64_t state[], uint64_t actions_out[])`           | Write valid action IDs into `actions_out` and return the count.                                                       |
| `apply_action`       | `void (uint64_t state[], uint64_t action)`                            | Mutate `state` in-place by applying `action`.                                                                         |
| `is_terminal`        | `bool (const uint64_t state[])`                                       | Return `true` if the game is over (win, loss, or draw).                                                               |
| `get_outcome`        | `void (const uint64_t state[], int64_t scores_out[])`                 | Write per-player scores into `scores_out` (e.g. +1 win, -1 loss, 0 draw). Only meaningful when terminal.              |
| `get_observation`    | `void (const uint64_t state[], uint64_t player, uint8_t* obs_out)`    | Encode the `state` as a discrete observation tensor from player's perspective.                                        |
| `get_features`       | `void (const uint64_t state[], uint64_t player, float* features_out)` | Encode the `state` as a floating-point feature tensor from `player`'s perspective, suitable for neural network input. |
| `to_string`          | `uint64_t (const uint64_t state[], uint64_t buf_size, char* buf)`     | Write a human-readable representation of the state into `buf`. Returns the number of bytes written.                   |
| `help_prompt`        | `const char* (void)`                                                  | Return a static string describing the game's rules and move format.                                                   |

Static Fields
-------------

|        Field       |      Type     |                                                   Description                                                  |
|:------------------:|:-------------:|:--------------------------------------------------------------------------------------------------------------:|
| `obs_dims[4]`      | `uint64_t[4]` | Shape of the observation tensor. Only the first `OBS_NDIMS` entries are meaningful (e.g. `{2, 3, 3}` for TTT). |
| `features_dims[4]` | `uint64_t[4]` | Shape of the features tensor. Only the first `FEATURES_NDIMS` entries are meaningful.                          |

Observation vs Features
-----------------------

Both `get_observation()` and `get_features()` encode the game state from a given player's perspective, but they serve different purposes:

- **Observation** (`uint8_t*`) - A semantically faithful, discrete/integer encoding of the game state. This is the "ground truth" representation i.e. each element maps precisely to a meaningful game concept. Useful for rule-based agents, debugging, and inspection.

- **Features** (`float*`) - A float encoding optimised for neural network consumption. This may normalise values, broadcast scalars across spatial dimensions, or apply other transformations beyond a simple cast of the observation. Note: A simple game (like Tic-Tac-Toe) may produce identical representation, but more complex games will diverge.

Memory Conventions
------------------

All memory is **caller-allocated**. The `Game` functions never allocate or free memory themselves. Before calling any function, the caller must allocate buffers according to the macros listed above. For example:

```c
uint64_t state[STATE_SIZE];
uint64_t actions[MAX_NUM_ACTIONS];
int64_t  scores[NUM_PLAYERS];
uint8_t  observation[OBS_SIZE];
float    features[FEATURES_SIZE];
char     string_buffer[STRING_BUF_SIZE];
```

Players are zero-indexed throughout (player 0 moves first).
