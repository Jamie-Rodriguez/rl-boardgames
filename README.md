This repo contains a collection of board games, written in C (C99 and built using GNU Make), for the purpose of exploring/experimenting with reinforcement learning agents.

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

Available Games
===============

| Game                              | Tic-Tac-Toe |
|-----------------------------------|:-----------:|
| Num Players                       |      2      |
| State Size                        |      10     |
| Max Actions                       |      9      |
| Observation: Number of Dimensions |      3      |
| Observation: Dimensions           |  [2, 3, 3]  |
| Observation: Size                 |      18     |
| Features: Number of Dimensions    |      3      |
| Features: Dimensions              |  [2, 3, 3]  |
| Features: Size                    |      18     |

