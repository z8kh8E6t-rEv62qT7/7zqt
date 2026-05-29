# 7zG Add GUI Test Memo

## Done
- Added smoke coverage for Add dialog format filtering, single-file archive naming, zip password/AES without `he`, disabled encryption controls, and update/path mode combo order/default/output.
- Added real `7zG` Task IPC coverage for zip/AES add followed by wrong-password and correct-password extract against the created archive.
- Added real `7zG` Task IPC coverage for 7z/AES encrypted headers: canceled open must prompt for password, provided password lists names, and password extract round-trips content.
- Added real `7zG` Task IPC coverage for Add update modes (`add`, `update`, `fresh`, `sync`) against original action-set semantics.
- Added real `7zG` Task IPC coverage for extract overwrite switches `-aoa`, `-aos`, `-aou`, and `-aot` against real archive/file conflicts.
- Added real `7zG` Task IPC coverage for extracting multiple real archives in one request.
- Added app_logic matrices for all Add dialog update modes (`add`, `update`, `fresh`, `sync`) and path modes (`relative`, `full`, `absolute`), including the full 4x3 update/path cross-product for backend action-set semantics.
- Completed GUI behavior matrix registration for existing filemanager behavior cases so the matrix gate passes again.
- Fixed direct extract opening of encrypted 7z headers so a supplied extract password is used before archive contents are listed.
- Fixed native update/open callback hook lifetime by owning hook copies instead of retaining references to short-lived open helpers.
- Added real `7zG` Task IPC coverage for archive-aware `Test` entry selection, including selected-entry success and missing-entry no-op success.
- Added real `7zG` Task IPC coverage for filesystem and archive-aware `Hash` with SHA256 digest assertions against real file contents.
- Fixed and covered real `7zG` Task IPC `Test` failure completion for missing archives under the `Z7_TESTING` result-dialog suppression path.
- Routed pure CompressDialog test object names behind `Z7_TESTING` so release builds do not carry those test hooks.
- Removed the CompressDialog production dependency on the `formatCombo` object name; format normalization now has an explicit helper.
- Added real `7zG` Task IPC format matrix coverage for valid original Add combinations: single-file `7z/bzip2/gzip/tar/wim/xz/zip`, directory `7z/tar/wim/zip`, 7z password/header modes, and zip ZipCrypto/AES modes.
- Added real `7zG` Task IPC archive export matrix coverage for nested `7z/tar/wim/zip` children across `full/relative/absolute` path modes and `-aoa/-aos/-aou/-aot` overwrite modes.
- Added real `7zG` Task IPC failure matrix coverage for missing inputs, corrupt archives, wrong passwords, invalid zip header encryption, and missing archive entries.
- Added real `7zG` Task IPC `kCli` payload coverage for add/update/rename/delete/test/extract against a real archive plus missing-archive failure completion.
- Fixed archive export overwrite switch parsing so `-aoa/-aos/-aou/-aot` are not normalized into Ask mode.
- Fixed archive-aware Test/Extract/Hash selection for pathless stream entries by using the same synthetic item name that extraction materializes.
- Completed full production `setObjectName` audit: pure test hooks are now guarded by `Z7_TESTING`; only Options dialog names used by production text-refresh `findChild` remain unconditional.
- Kept this memo at `docs/7zg-add-gui-test-memo.md`; no migration needed.
- External codec support is explicitly out of scope for the Add GUI and real-archive acceptance matrix; coverage stays on original built-in valid combinations.
- Added `Z7_TESTING` scripted interaction coverage for progress pause/resume, background toggles, cancel confirmation/close paths, disabled completion controls, and consecutive password/overwrite/choice/memory-limit prompts.
- Fixed GUI Add dialog 7z encrypted-file-name propagation after dialog close: accepted options now use format capability rather than child-widget visibility, and smoke coverage creates a real header-encrypted 7z from the dialog.
