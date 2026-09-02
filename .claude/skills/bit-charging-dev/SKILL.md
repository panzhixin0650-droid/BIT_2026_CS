---
name: bit-charging-dev
description: Develop, review, plan, or extend the BIT_2026_CS electric-vehicle charging course project. Use for repository structure, Qt user-client or server/admin work, SQLite schema and migrations, TCP JSON contracts, Mock pile or prediction adapters, ECharts dashboard work, team collaboration rules, and selectively promoting features from the legacy expansion documents. Enforce the Demo-first baseline and distinguish design-only requests from authorized implementation.
---

# BIT Charging Development

## Establish scope first

1. Locate the repository root and read `PROJECT_RULES.md` plus `docs/INDEX.md`; also honor the tool-specific entry file (`AGENTS.md` or `CLAUDE.md`) when present.
2. Classify the request as one of:
   - design/contract/scaffold only;
   - current Demo implementation;
   - explicitly approved expansion.
3. For a design-only request, limit writes to Markdown, contract JSON fixtures, design diagrams, and directory README files. Do not create application code, SQL, databases, build targets, implementation scripts, or generated application/runtime assets.
4. Treat the current five-table Demo and Mock hardware path as the default unless the user explicitly activates an expansion.

## Follow the authority order

1. Use `docs/requirements/project-spec.doc` for course acceptance scope.
2. Use `contracts/overall-interface-v1.md` for current messages, DTOs, states, errors, Mock boundaries, and Web snapshot semantics.
3. Use `docs/design/demo-database-design.md` for current entities, fields, units, relations, and transaction boundaries.
4. Use merged numbered migrations as the database structure source once they exist.
5. Read `docs/extension/` only for an explicitly requested expansion or roadmap task. Never implement the whole legacy design by implication.

Resolve conflicts in that order. Treat `PROJECT_RULES.md` as the canonical cross-agent policy and record a lasting new decision under `docs/decisions/`.

## Respect module ownership

- Keep Qt user-client work in `client/`. Do not add SQL or server implementation headers there.
- Keep TCP transport, ApplicationService, Repository, adapters, and administrator UI in `server/`.
- Keep only shared DTO, error-code, JSON, and frame-codec implementation in `shared/protocol/`.
- Keep ECharts work in `web/`; consume fixtures or exported JSON, never SQLite directly.
- Keep migrations, seeds, and optional samples in `database/`.
- Put cross-module semantics and fixtures in `contracts/`.

Avoid unrelated edits in another developer's owned module. Surface genuine shared changes through the contract or database directories.

## Preserve Demo invariants

- Route authoritative state changes through ApplicationService and parameterized Repository operations.
- Keep amounts as integer cents, energy as Wh, times as UTC ISO 8601, and business-day reporting as Asia/Shanghai.
- Preserve the order price snapshot and documented order/pile state rules.
- Keep runtime serial by default. Treat the diagrammed QThread as optional presentation structure, not a reason to add pools, queues, locks, or load claims.
- Use `MockPile` and `MockPredictionProvider` until real integration is explicitly requested.
- Keep user location request-scoped and avatars client-local in the current version.

## Change contracts deliberately

For a cross-module change:

1. State the consumer and affected owners.
2. Update the current contract and JSON fixtures in the same change.
3. Prefer an additive V1 message or optional field.
4. Create V2 for renamed fields, changed types or units, or changed success/state semantics.
5. Add a numbered database migration only when implementation is authorized; never paste the legacy 24-table snapshot over the Demo schema.
6. Cover the principal success path and at least one relevant failure path once tests exist.

Keep documents focused on observable boundaries. Do not freeze filenames, class counts, SQL text, UI layout, threading details, or deployment machinery without a real interoperability need.

## Promote expansion features incrementally

Require each legacy feature to have a consumer, an accepted decision, a compatibility plan, and a bounded data migration. Promote one feature package at a time, such as wallet history, reservation policy, pricing, RBAC, operations, support, or persisted analytics.

Preserve existing integer IDs and historical order snapshots. Add public codes or detail tables instead of changing existing field types. Introduce `IPileGateway`, a new Repository backend, HTTP/WebSocket, or a real model provider behind the existing application boundary.

## Verify proportionally

- For scaffold or documentation work, run `git diff --check`, validate relative links, parse JSON fixtures, and confirm no unintended implementation files were added.
- For database work, run migrations against a new temporary database, check integrity and foreign keys, and test relevant constraints.
- For code work, configure/build only the affected module first, run its focused tests, then run cross-module contract checks.
- Report commands that could not run and leave unrelated work intact.
