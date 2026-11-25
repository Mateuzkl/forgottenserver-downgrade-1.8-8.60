# PR 343: implementation and verification status — 2026-10-28

The revision is implemented on `fix/market-inbox-persistence-save`. Native C++ validation is still pending. The C2248 reported by the user was corrected through `Player::refreshInventoryStats()`. No local compilation was initiated during this revision; the user is compiling the server separately. This is not a declaration that the change is ready to merge.

## Behavior

- Async player saves distinguish committed, confirmed rollback and unknown outcomes. An unknown COMMIT is never retried blindly. Failed sessions are quarantined immediately; packets and movement stop, and removal runs after the current handler releases its resources. Quarantined cleanup skips logout/de-equip scripts and fresh player snapshots.
- One active worker and one replaceable latest pending snapshot are kept per GUID. A generation fingerprint in SQL WAL prevents an older worker from clearing a newer generation. Snapshot SQL and its generation deletion commit together. Online-time replacement makes new snapshots replayable. Recovery of older WAL omits its ambiguous additive online-time increment and logs this limitation.
- If the latest SQL WAL cannot be recorded, a local emergency SQL snapshot is preserved under `data/save-quarantine/`. Startup blocks affected GUIDs instead of replaying an older SQL WAL over that file. An OS/process crash and a power failure have different durability guarantees: the emergency file uses flush/close, without a filesystem power-loss durability guarantee. A disk-write failure is logged as RAM-only and requires operator intervention before restart.
- Cancellation follows the owner's **current city**, per the user's explicit choice. Automatic expiry and receipts for a BUY owner use the creation depot online/offline. Offers without a recorded depot fall back to Inbox 0. Pending destinations do not change when the player moves. Notices remain English, use city names and `MESSAGE_EVENT_ADVANCE`.
- Periodic delivery processing scans at most 256 online candidates, processes at most 2 players, 8 rows and 128 actual item instances per invocation (64 per player). It rotates a GUID cursor. Invalid rows back off for 60 seconds. Partial progress and player items commit atomically. Legacy quantities 64000, 64001 and 65535 are validated without allocating the entire entitlement. Large nonstackable returns require many ticks.
- The 8 ms elapsed target is **soft**: SQL and a full player snapshot cannot be preempted. The count budgets do not establish a maximum tick latency. Logs expose delivery p50/p95/p99 and save snapshot/WAL/map/worker timings; no performance improvement is claimed without measurements.
- Payment reservations preserve original coins/parents and bank balance. Confirmed failure restores them; success and unknown outcomes release retained originals without compensating resources. Native scope cleanup and logout cleanup bound the reservation lifetime, including script failures before settlement. This addresses an observed retention path; it does not prove absence of all leaks.
- Acceptance rejects `created + duration <= now` using wide arithmetic. Serialized item tier remains authoritative for legacy payloads; a mismatched metadata/serialized tier regression was added without reintroducing a restrictive equality check.
- Scheduled CLOSE uses its existing C++ save/hook path once. Periodic OPEN calls `saveServer("periodic")`. Mandatory saves bypass periodic throttling and drain active worker futures before shutdown. An incomplete drain is explicitly logged and is not reported as successful persistence.

## Verification actually performed

| Check | Result | Scope |
| --- | --- | --- |
| `git diff --check` | PASS | Diff whitespace; no compiler validation |
| Lua 5.5 DLL: load six changed/relevant Lua files | PASS | Syntax, including migrations and test script |
| `.github/tests/market_save_regressions.lua` through Lua 5.5 DLL | PASS | Actual Lua schedule open/close/shutdown and payment helper delegation; C++ APIs mocked |
| `cmake -DPROJECT_SOURCE_DIR=<repo> -P tools/check-shared-get-escapes.cmake` | PASS | Repository check for known shared_ptr/raw-pointer async escape patterns |
| `cmake -DPROJECT_SOURCE_DIR=<repo> -P tools/check-source-list-parity.cmake` | PASS | CMake / Visual Studio source parity |
| `cmake -DPROJECT_SOURCE_DIR=<repo> -P tools/check-manual-delete.cmake` | PASS | No manual delete expressions detected |
| `python build-tests-market/review_checks.py` | PASS | Real MariaDB 11.4.5, isolated localhost:33079: execute Lua migrations 68/69, repeat 69, preserve old rights/binary attrs; fresh schema import; actual WAL conditional-delete SQL and progress CAS with commit/rollback |
| Updated C++ unit and DB executables | NOT RUN | Not rebuilt locally, respecting the user's request. Old executables were not counted as evidence for these changes |
| Linux/Clang CI with MySQL 8.4 | PENDING | Workflow provisions a real disposable DB; missing credentials produce CTest SKIP (77), never an apparent DB pass |
| ASan/LSan/Valgrind and game-server stress benchmark | NOT RUN | Cannot assert leak-free operation or measured tick improvements |
| Gameplay, logout/restart and 29/100+ player benchmark | NOT RUN | Requires the new compiled server and an isolated game environment |

The local SQL check script and its logs are retained in ignored `build-tests-market/`; they test migration/SQL behavior, not the native Market engine. The helper MariaDB was shut down after the checks. No production database or live server was used.

## Migration and rollback

The previous local placeholder was copied, with matching SHA256, to `.git/local-backups/migration-69-placeholder.lua`. Migration 69 upgrades database version 69 to 70 through the normal migration runner. Its ALTERs are repeatable; old offers retain NULL destination and existing pending rights are preserved. Fresh schema definitions include the new columns. Standalone SQL repair does not advance db_version.

For rollout, first back up the DB and any emergency snapshots, stop the old server, deploy the matching native code and Lua scripts together, and let the normal runner apply migrations 68/69 as required. Verify pending counts and sample city destinations before opening player access. Do not run two server binaries against the same characters while upgrading.

Do not downgrade to a processor that ignores `delivered_amount`: partially delivered rights would be applied again. Keep the queue, WAL and new columns. Reconcile or drain partial receipts and pending WAL before a reviewed code downgrade; never delete these tables to enable it.

Emergency `.sql`/`.tmp` files require offline reconciliation. Check them against the matching SQL WAL and authoritative DB state; do not replay an older WAL first. Apply an approved latest replacement snapshot and retire its old WAL in one DB transaction, verify item/balance state, then archive the emergency file outside the active quarantine directory. Incomplete files and RAM-only failures require manual investigation. No automatic importer was added.

## Remaining acceptance gates

Native build and all updated tests must pass. ASan/LSan or equivalent must exercise repeated create/cancel/accept, script failure and logout, plus shutdown with outstanding workers. The injected unknown-worker-completion test is not a network fault test of a real lost COMMIT acknowledgement. Physical insertion failure, actual live-client quarantine, online/offline reload and performance under load still need execution. The hard delivery budgets limit allocation/work counts; full player serialization and synchronous SQL remain a latency risk. No review thread was marked resolved without runtime evidence, and no merge was performed.

## Review follow-up after 5723e303

Reviewed CodeRabbit, Greptile and Sourcery inline comments plus CodeRabbit's follow-up/nitpick sections. Confirmed Linux, ThreadSanitizer and Docker logs all stopped at the omitted `walClearQuery` initializer; it now has an explicit empty value.

| Finding | Disposition |
| --- | --- |
| Missing snapshot member initializer (CI / CodeRabbit) | Explicit final empty member; preserves `-Werror` rather than suppressing it |
| Reconnect lowers online time (Greptile 4403225270) | Separate session-start clock survives reconnect; repeatable elapsed-time regression added |
| WAL changes written item text (Greptile 4403225291) | Normalize only generated `UPDATE players SET` assignments outside quoted values; inserts/binary attributes remain byte-for-byte unchanged; quoted-text and DB replay regressions added |
| Coins need change but no bag (Greptile 4403225303 / CodeRabbit 4403310639) | Place prepared change in released coin slots or valid empty slots; bank-only fallback if no slot is available and the bank covers the entire payment; restore/success/fallback cases added |
| Emergency SQL privacy (CodeRabbit 4403310651) | On POSIX, directory permissions 0700 are set before file creation and temporary file permissions 0600 before writing/rename. Windows retains its existing NTFS ACL inheritance; no claim of POSIX permission enforcement on Windows |
| Credited balance exceeds Lua integer range (CodeRabbit follow-up) | Reject credits above INT64_MAX; retain item-only delivery even with a legacy unsigned balance; guard direct BUY-accept credit before removing items; boundary and DB regressions added |
| Legacy Inbox 0 has no world locker (CodeRabbit follow-up) | `!marketstatus legacy` opens the existing owned inbox at a nearby physical depot. Its locker is attached to that depot for normal item-distance/close checks. No automatic relocation or replacement deliveries |
| Event test checked syntax only (CodeRabbit) | Execute and assert actual C++ event registration; executable Lua test also verifies name, interval, registration and callback |
| Payment success / actual reload / deferred removal coverage (CodeRabbit nitpicks) | Added restore=false debit/weak-reference check, fresh DB-loaded Player assertions, and registered online-player deferred-removal integration scenario |
| Older refund, work-budget, expiry, tier and full-inbox comments | Current source already has exact money restoration, rotating count budgets, accept-expiry guard, serialized attributes and verified inbox ownership; retained those fixes |
| Older suggestion to simply remove runtime quarantine | Not adopted: unknown commits require quarantine; current code freezes/disconnects the player and has controlled recovery, addressing the underlying lost-progress report |

The 7-file Lua syntax check and expanded Lua regressions passed locally through the existing Lua 5.5 DLL. Source-list/ownership checks and `git diff --check` passed. Repeated isolated MariaDB migration/WAL/progress SQL checks passed after the interruption. Native regressions above were added but **not compiled or executed locally**. Build/TSan/ASan results are pending the pushed revision; no leak-free claim or merge approval is made.
