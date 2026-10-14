# History recovery in 0.4.2

The recurring empty Desktop window was reproduced in the normal 0.4.1 app, launched by Explorer without test arguments. Its native service reported zero history. Read-only Windows handle inspection established that its open SQLite file and a fresh development-tool open had different physical file identities and sizes, despite the same displayed LocalAppData path. The development view retained six encrypted downloads; the Desktop view was empty. Neither handle was marked for deletion. The responsible filesystem redirection component has not been identified. Earlier tool-launched restart checks therefore did not exercise the failing storage view.

The local development installation now uses an explicit data-location binding beside each development executable. Both launch contexts resolve the project data directory to the same physical SQLite file, checked before opening or creating the queue. An unexpected identity produces a startup error instead of a replacement empty history. The six encrypted download records and preferences were copied using SQLite backup, not plaintext export. Both previous database branches were archived privately. Existing downloads and partials were retained. The Explorer-launched 0.4.2 app reports six downloads, one paused and zero active; the visible paused record retains 79% progress.

## Storage binding

Without a binding, the default remains Windows LocalAppData/Usha Labs/Downshift. An optional `downshift-storage.json` beside Downshift.exe has this format:

```json
{"version":1,"dataDirectory":"D:/DownshiftData","expectedIdentity":"32-character-lowercase-hex-fingerprint"}
```

The actual user directory is `dataDirectory/users/<Windows SID>`. Relative roots resolve against the executable directory. `expectedIdentity` is optional for a fresh installation; for a recovered installation it binds the existing queue.sqlite using a truncated SHA-256 of Windows volume/file identity. This detects different physical storage, not content corruption. SQLite checks and DPAPI authentication still verify content. Intentional replacement/restoration of the database changes its file identity and requires updating this local binding after verifying the restored file. Do not remove the identity check to bypass an unexplained empty store.

This user's ignored local root is Desktop/Projects/Downshift/.downshift-data. It is not shipped or committed. Portable packaging does not copy bindings or private databases. No automatic migration, cross-user DPAPI decryption, or unreviewed overwrite is performed.

## Empty history safeguards

Startup already rejects missing or zero-byte live databases when an owned recovery index exists. It now also rejects a structurally valid empty live database when authenticated owned backups contain downloads. This check precedes publishing queue state and rotating backups. Unreadable or invalid recovery data fails visibly.

Removing the final entry through Downshift atomically records an encrypted intentional-empty marker. An intentional clear is allowed to remain empty after restart, even while older recovery snapshots contain records. Adding an entry clears that marker. Removing a history entry preserves downloaded/partial files.

The new queue_meta table is additive at existing schema version 2; previous payloads and IDs are unchanged. Older builds ignore this table. Do not concurrently run old and new builds against the same queue.

## Verification scope

Native tests cover location parsing, per-user scope, matching/mismatched file identities, invalid configuration, missing/truncated/valid-empty live stores, intentional Remove-all/restart, marker clearing on Add, full-file hashes, pause/restart/resume and forced process death. Fixtures use private queues. Actual Desktop startup verification is separate from those tests. Retaining a displayed paused progress value is not a claim that an unverified partial has been resumed; resume still validates the saved journal and remote version.
