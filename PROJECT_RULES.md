# BIT_2026_CS shared project rules

These rules are tool-independent and apply to every human or AI contributor, including Codex, Claude Code, IDE agents, and future automation.

## 1. Classify the request before acting

- Distinguish design/contract/scaffold work from authorized implementation.
- For a design-only request, limit writes to Markdown, contract JSON fixtures, editable design diagrams, and directory README files. Do not create `.cpp/.h/.ui/.sql`, build files, databases, generated sites, implementation scripts, or other implementation artifacts.
- Default to the current course Demo. Do not activate legacy complexity merely because a reference document describes it.
- Do not broaden a task across another contributor's module without a real shared-contract need.

## 2. Follow the authority order

1. `docs/requirements/project-spec.doc` defines course acceptance scope.
2. `contracts/overall-interface-v1.md` defines current messages, DTOs, states, errors, Mock boundaries, and Web snapshot semantics.
3. `docs/design/demo-database-design.md` defines current entities, fields, units, relations, and transaction boundaries.
4. Merged numbered migrations become the database structure source once implementation begins.
5. `docs/extension/` is an optional backlog, not a second implementation standard.

Resolve conflicts in this order. Record lasting new decisions under `docs/decisions/`.

## 3. Respect module ownership

- Keep Qt user-client work in `client/`; do not add SQL or server implementation headers there.
- Keep TCP transport, ApplicationService, Repository, adapters, and administrator UI in `server/`.
- Keep only shared DTO, error-code, JSON, and frame-codec implementation in `shared/protocol/`.
- Keep ECharts work in `web/`; consume fixtures or exported JSON, never SQLite directly.
- Keep migrations, seeds, and optional samples in `database/`.
- Put cross-module semantics and fixtures in `contracts/`.

Avoid unrelated edits in another developer's owned directory. Expose necessary shared changes through the contract, database, or shared protocol boundaries.

## 4. Preserve Demo invariants

- Route authoritative state changes through ApplicationService and parameterized Repository operations.
- Keep amounts as integer cents, energy as Wh, timestamps as UTC ISO 8601, and business-day reporting as Asia/Shanghai.
- Preserve the order price snapshot and documented order/pile states.
- Keep runtime serial by default. The diagrammed QThread is an optional course presentation boundary, not permission to add pools, queues, locks, or performance claims.
- Use Mock hardware and Mock prediction until real integration is explicitly requested.
- Keep user location request-scoped and avatars client-local in the current version.
- Do not let UI, Web, models, or device adapters write the database directly.

## 5. Change contracts deliberately

For every cross-module change:

1. Identify the consumer and affected owners.
2. Update the current contract and JSON fixtures together.
3. Prefer an additive V1 message or optional field.
4. Create V2 for renamed fields, changed types/units, or changed success/state semantics.
5. Add a numbered database migration only when implementation is authorized.
6. Never paste the legacy 24-table snapshot over the five-table Demo schema.
7. Cover the principal success path and a relevant failure path once tests exist.

Keep contracts about observable boundaries. Do not freeze class counts, file layouts below the owned module, SQL text, UI styling, or threading machinery without an interoperability need.

## 6. Promote legacy features incrementally

Require a consumer, an accepted decision, a compatibility plan, and a bounded migration before promoting a legacy feature. Promote one package at a time, such as wallet history, reservation policy, pricing, RBAC, operations, support, or persisted analytics.

Preserve existing integer IDs and historical order snapshots. Add public codes or detail tables instead of changing existing field types. Introduce real hardware, another database, HTTP/WebSocket, or a real model behind the existing application boundary.

Use this lifecycle so roadmap work cannot silently become a current requirement:

| Stage | Allowed change |
| --- | --- |
| `IDEA` | Write only under `docs/extension/` and update its index; do not touch the current contract, migrations, or code. |
| `ACCEPTED` | Record an ADR and update current contract/design plus fixtures; still do not add implementation unless the user also authorizes it. |
| `IMPLEMENTED` | Add the bounded migration/code/tests authorized for that accepted design. |

Moving between stages requires an explicit user or team decision; an AI agent must not infer promotion merely because an old document contains details.

## 7. Collaborate safely

- Preserve unrelated and uncommitted changes.
- Keep branches short-lived and use the branch prefixes `client/`, `server/`, `web/`, `db/`, or `contract/`; these are branch names, while the database directory remains `database/`.
- Update documentation in the same change as the behavior it governs.
- When the project skill workflow changes, keep `.agents/skills/bit-charging-dev/SKILL.md` and `.claude/skills/bit-charging-dev/SKILL.md` byte-identical. Personal installations are convenience copies, not repository authority.
- Never commit secrets, local IDE state, build output, runtime databases, or generated dashboard data.
- Report assumptions, checks performed, and checks that could not run.

## 8. Verify proportionally

- For scaffold or documentation work, run `git diff --check`, validate local Markdown links, parse JSON fixtures, and confirm no unintended implementation files were added.
- For database work, run migrations against a new temporary database and check integrity, foreign keys, and relevant constraints.
- For code work, configure/build the affected module first, run focused tests, then run cross-module contract checks.
