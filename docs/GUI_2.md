# Downshift GUI 2.0 and history loading

The user directly requested persistence diagnosis, then explicitly referred to their GUI 2.0 design brief on 14 October 2026. The screenshot is a visual reference, not a source of real download counts or speeds. This is one approved phase; the native C++20/Qt Widgets stack and download engine remain.

## History investigation

The Desktop shortcut points to the normal Release EXE with no smoke/test arguments. The existing per-user SQLite database passes quick_check and the native record loader. It contains seven valid encrypted records: four completed, two failed and one paused. Three partial files and resume journals remain. A consistent encrypted SQLite backup was made before changes. A clone loaded all seven records in the actual redesigned Qt window, with original records/settings/receipts and partial/journal metadata unchanged.

A normal-app close/reopen from a different working directory retained all seven encrypted records/settings/receipts. During review the user deliberately removed one failed entry and confirmed doing so; the remaining six-record state, including the paused download, is the new preservation baseline.

The exact original empty-window event was not captured. Two concrete misleading behaviors were found: the old GUI displayed Ready when you are before its first asynchronous history result (and retained that empty surface after a loading exception), and some GUI/media validation windows looked like Downshift while using temporary databases. Those are corrected without claiming that the original database was deleted or inventing an observed trigger.

Normal storage now resolves Windows' Local AppData known folder and retains Usha Labs/Downshift/queue.sqlite, independently of the EXE folder or Qt application metadata. Startup validates all encrypted settings, receipts and records before publishing the queue or changing interrupted statuses. An unavailable database displays History unavailable, disables creation/saving, preserves existing files and offers Retry loading after temporary ownership trouble. A missing or zero-byte live file with existing backup ownership is refused before SQLite can create an empty replacement. Retry is serialized on the service thread and does not reset an already loaded queue. Settings diagnostics remain accessible without permitting default preferences to overwrite unavailable saved values.

The SQLite backup API creates consistent standalone encrypted snapshots after successful initialization, explicit Pause and clean shutdown. The ownership index rotates eight managed snapshots; unknown files are never selected for deletion. Backup failure reports a warning separately from saved history availability. Backups do not contain the downloaded files or replace the encrypted resume journals. There is no automatic overwrite/restore of a damaged live database.

All Qt validation and smoke windows remain physically hidden while rendering the actual widgets. Browser acceptance additionally uses a separate test Native Messaging host, per-user test pipe and private profiles. Normal Chrome registration and the user's database are not redirected to fixtures.

## Interface

- Neutral light surfaces and charcoal dark surfaces, with pink reserved for actions, selection, focus and progress.
- Consistent line icons, summary cards and an original rounded pink arrow/ring icon. Windows ICO resources and Qt's raw RGBA resource work without optional image plugins.
- A 1000×650 logical default, 800×550 minimum and compact sidebar below 950 logical pixels. Search retains priority; sorting uses Latest, Oldest and Size. Resume/Pause/Cancel move into More when narrow. Add download stays accessible.
- Lightweight painted filename/source, progress and status cells; no progress widget per row. Filenames elide and retain full tooltips. Existing ID-based sorting, filters and queue actions are retained.
- Details appear only for a selected record and can collapse. Long values wrap or remain available in tooltips/copy actions. The list can use the full window when no details are selected.
- Clear loading, genuine empty, no-match and history-failure states. Metrics remain unknown while history is unavailable rather than claiming zero records.
- General, Downloads, Appearance, Integration and Advanced Settings sections. Bounded dropdowns retain saved values outside presets. Arbitrary speed limits use a validated numeric field; unchanged sub-KB limits remain exact. Save/Cancel, theme preview and validation preserve preferences.
- Fusion list views and a frameless Qt popup container remove the legacy popup frame; all forms, menus, scrollbars and tooltips share the design system. Native Windows title controls remain, with supported neutral/dark caption hints.
- Keyboard shortcuts, named compact icon actions and keyboard sorting. File deletion, restarting and history removal require explicit user action/confirmation.

## Verification and scope

DEVELOPMENT_PROGRESS.md records actual final local/remote results, device-pixel ratios and inspected renders. Tests cover paused restart retention, forced process death, backups, temporary ownership failure/retry, settings equality, controls at 1000×650/900×600/800×550, maximization, popup frame/keyboard handling, dark themes and live progress. Synthetic screenshot rows are fixtures, not asserted Internet transfers. User data, signed links and original filenames are excluded from public evidence.

The actual download engine source is unchanged from 0.3.3. Its public-file/range/retry/crypto checks remain required. Current source refusal, authenticated/browser-only/DRM and signing/clean-VM limits remain as previously documented. System theme is not offered because the existing saved appearance setting supports Light/Dark. Native caption hints are best-effort on supported Windows versions. This phase stops after verified commit/push for user review; v0.4.0 is not a newly published preview.
