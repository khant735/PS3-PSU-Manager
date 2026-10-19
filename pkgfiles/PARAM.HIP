PS2 .PSU SAVE MANAGER v1.63

CONTROLS
FILE BROWSER
Up/Down  Select item
X  Open folder/PSU; resume .part
Circle  Back
Left/Right  Switch HDD / USB
L1/R1  Move 10 items
L2  Cancel staged action
Square  Copy selected file
Triangle  Rename file / folder
Select  Delete selected item
L3  Extract selected PSU / ZIP
R2  Create blank PSU
R3  Create PSU from folder
Start  Open Options

INSIDE PSU
Up/Down  Select entry
X  Show PSU details
Circle  Close PSU
Left/Right  Switch HDD / USB
L1/R1  Move 10 entries
L2  Cancel staged action
Square  Export selected entry
Triangle  Inject copied file
Select  Delete selected entry
R3  Rename selected entry
Start  Open Options

OPTIONS
Up/Down  Select option
Left/Right  Change selected setting
X  Toggle / open selected option
Circle / Start  Save and close
FPS and download connections are adjustable
Connections 0-1: single; 2-16: segmented
Direct URL opens the downloader keyboard
Direct URL accepts HTTP or HTTPS
Choose PS2Wiki or Homebrew Store with Left/Right
X  Open selected catalogue
X on Exit to XMB closes the app

URL KEYBOARD
D-pad  Move; X inserts selected character
Square  Backspace; Triangle inserts a space
L1/R1  Move caret; L2/R2  Start / End
Start  Download URL
Circle  Cancel

DOWNLOAD PROGRESS
Start  Pause / resume download
Circle  Cancel; keep partial for later resume
Partial downloads can resume from the browser
X  Close after download completes

PS2WIKI CATALOGUE
Up/Down  Select item
L1/R1  Change populated category
X  Check link or download PSU
Square  Refresh; Circle  Back to Options
Filen links are decrypted before download

HOMEBREW STORE
Up/Down  Select item or PSU / ZIP file
L1/R1  Change populated category
X  Check item or open its file list
X  Download selected PSU / ZIP file
Square  Refresh; Circle  Back to Options

FILE EXTRACTION
L3  Extract selected PSU or ZIP
PSU files go into a named subfolder
ZIP files unpack beside the archive
The ZIP stays after extraction
ZIP supports STORE and DEFLATE archives
X  Replace existing files; Circle  Cancel

UPDATE HISTORY

v1.63 XMB history wrapping and logo
Wrap long XMB Update History lines at word
boundaries so the release notes remain visible
within the display width.
Update the original XMB artwork with the gold
v1.63 label shifted right to clear the
memory-card graphic.

v1.62 Phase 6E Downloader Height Fit 30 FPS
Size downloader height from the diagnostic rows
actually drawn in the two-column C1-C16 layout,
removing unused space below the footer. Retain
the wrapped delete dialog, separated diagnostic
columns, and stable synchronized 30 FPS
presentation.

v1.61 Phase 6D Wrapped Dialog Fit
Wrap long file and folder names inside delete
confirmations and size the dialog to the wrapped
text and controls. Separate and clip the
downloader diagnostic columns.

v1.60 Phase 6C Downloader Panel Sizing
Keep the FPS meter inside the title bar, using a
compact readout when needed. Size downloader
panels to measured headings, statistics,
diagnostics, and wrapped footer controls.

v1.59 Phase 6B Catalogue Display Fixes
Show the selected Homebrew Store category. Fit
catalogue panels to their longest visible text
within screen margins, and center headings,
wrapped lines, rows, status, and controller
legends.

v1.58 Phase 6A ZIP Extraction
Extract supported ZIP32 STORE and DEFLATE
archives beside the ZIP while keeping the
original archive. Preserve folders, verify
CRC32, reject unsafe or unsupported entries, and
ask before replacing files.

v1.57 Phase 5Z Stability and CA Path Recovery
Restore the stable 64 KiB Tiny3D command FIFO
and synchronized presentation path. Find and
validate the installed CA bundle before secure
Filen and Homebrew Store requests.

v1.56 Phase 5Y Stable 30 FPS
Consolidate catalogue browsing, Filen downloads,
compact panels and text wrapping, catalogue link
actions, and ICN texture fixes on the stable
synchronized 30 FPS path.

v1.55 Phase 5X ICN Texture RLE Compatibility
Handle FDxx/FExx literal-run texture packets so
textured 3D ICN previews render correctly,
including the supplied Pinball saves.

v1.54 Phase 5W UI Layout and Catalogue Actions
Wrap long text with clear line spacing, size
compact panels to their contents, and label
catalogue actions according to whether files are
resolved, multiple builds are available, or
links still need checking.

v1.53 Phase 5V PS2Wiki Archive Updates
Add RTEs, UMCS, and Other archive pages. Skip
categories without downloadable entries and
resolve current selector-based and escaped share
links, including uLaunchELF.

v1.52 Phase 5U Empty Exploits Category
Skip store categories that have no supported PSU
or ZIP links.

v1.51 Phase 5T Catalogue Selector
Combine PS2Wiki and PS2 Homebrew Store into one
PSU Catalogue option. Use Left/Right to choose a
catalogue and Cross to open it.

v1.50 Phase 5S Live Catalogues and Filen
Add live PS2Wiki and PS2 Homebrew Store
catalogues and native public Filen link
decryption and downloading.

v1.49
- Added persistent Options settings and restored
  EXIT TO XMB in the Options menu.
- Added the HTTP/HTTPS File Downloader with PS3
  network initialization, TLS 1.2, redirects,
  HTTP error handling and system proxy support.
- Added configurable simultaneous segmented
  downloads and raised the maximum from 8 to 16
  connections.
- Added live C1-C16 connection diagnostics,
  preserving the original C1-C8 text size and
  placing C9-C16 underneath; the diagnostics and
  downloader panels resize to their actual
  content.
- Added pause/resume and persistent partial-
  download support. Incomplete downloads retain
  their URL and segmented range state and can be
  resumed after leaving the downloader or
  restarting the application.
- Fixed segmented resume so .part, .seg0-.seg15
  and the segment manifest preserve all
  downloaded ranges without silently restarting
  or discarding non-contiguous data.
- Added backward recovery for partial downloads
  produced by the earlier segmented-resume
  implementation.
- Completed downloads and cancelled/resumed
  partials now refresh the file browser
  immediately, including file size and modified
  date/time.
- Added live directory-change detection so
  externally added/deleted files refresh the
  current file list and list-panel height.
- Empty folders no longer reserve an unused file
  row.
- Added automatic blank PSU naming: blank.psu,
  blank00.psu, blank01.psu and onward, with
  collision checks before the filename keyboard
  and again before creation.
- Added editable blank-template defaults and PS2
  save-folder presets EXAMPL-00000EFOLDER, SYS_
  and APP_.
- Expanded icon.sys metadata Details pages for
  title/icon references, background/transparency
  information and lighting vectors/colours.
- Added icon.sys lighting to the software ICN
  preview renderer using decoded vertex normals,
  ambient light and three directional lights,
  with fallback for malformed/missing metadata.
- Cached PSU/icon.sys Details data when the
  modal opens so page changes no longer
  reopen/reparse the PSU or relist the archive.
- Consolidated modal rendering and isolated
  Options, Details, Downloader, Delete and
  warning screens from browser/animated-ICN fast
  paths to reduce flicker, blinking and stale-
  frame artifacts.
- Delete confirmation and Options controls now
  use the shared graphical PS3 controller-button
  legends.
- Reworked Home/XMB lifecycle handling and clean
  redraw behavior on return from the system
  menu.
- Moved FPS and current HDD/USB/PSU status into
  the title bar and removed duplicate standalone
  status/FPS overlays.
- Recovered and consolidated the compact
  downloader/options-control UI, including a
  single downloader exit action, one completion
  percentage, compact completion footer and
  content-sized modal layout.

v1.48
- Standardized generated PS2 save templates
  around lowercase icon.sys naming and matching
  View, Copy and Delete icon references.
- Added lowercase filename glyph rendering
  throughout the file browser without changing
  stored filenames.
- Added uppercase/lowercase CASE switching to
  the virtual keyboard with a persistent high-
  contrast mode indicator.
- Added an insertion caret to the virtual
  keyboard; text can be inserted at the caret
  instead of only appended.
- Added L1/R1 character-by-character caret
  movement and L2/R2 jump-to-start/end controls.
- Added separate PSU filename, PS2 save-folder,
  Icon Title and Icon Text entry when creating
  blank PSU saves.
- Corrected generated icon.sys title encoding to
  Shift-JIS for PS2, Apollo Save Tool and PS3
  XMB compatibility.
- Generated blank saves use icon.sys plus
  normal, copy and delete ICN references, with
  del.icn supported for the delete icon.
- Removed the temporary PSU/icon.sys metadata
  DETAILS diagnostic viewer after completing the
  format audit.

v1.47
- Optimized ordinary folder navigation and PSU-
  internal file browsing for substantially
  higher and more stable framerates on real PS3
  hardware.
- Reworked file-list selection updates to redraw
  only affected rows and keep both framebuffers
  synchronized without expensive framebuffer-to-
  framebuffer copies.
- Replaced costly translucent selected-row
  blending with an opaque row renderer while
  preserving the established UI appearance.
- Added a strict fast path for navigation
  between non-.ICN entries inside an opened PSU;
  non-.ICN selections bypass ICN parsing,
  loading and preview work.
- Added ICON.SYS-driven discovery of the
  standard View, Copy and Delete ICN models and
  a cache for up to three unique decoded ICNs
  per opened PSU.
- Grouped .ICN entries at the end of the opened-
  PSU display list without changing the
  directory order stored in the PSU.
- Non-.ICN selections correctly return the ICN
  Preview to "Highlight a .ICN file" while
  retaining decoded ICNs in cache for quick
  reuse.
- Fixed virtual-keyboard selection trails and
  reduced rename-keyboard redraw work.
- Fixed rename-time animated .ICN preview
  composition so 3D rotation remains clean
  without duplicate or ghost geometry.
- Audited the .ICN parser against the PS2 icon
  format, corrected texture-type bitmask
  handling, limited shape count to 16 and
  tightened validation.
- Removed unsupported speculative alternate .ICN
  magic and animation parsing paths.

v1.45
- Improved UI/HUD render safety to prevent
  duplicate, overlapping and stale drawing.
- Dynamic text is clipped to its owning UI
  rectangles and layout uses a common 1280x720
  logical space.
- Added safety guards to the animated ICN fast-
  render path before model/texture access.
- Improved PS/Home XMB lifecycle handling:
  callbacks remain serviced while the system
  menu owns the display.
- Rendering/input remain suspended through
  DRAW_END; graphics resources remain allocated
  and return forces a clean redraw.
- Clears stale controller-edge state after
  returning from the system menu.
- Restored the correct v1.45 XMB artwork and
  package metadata.

v1.44
- Fixed duplicate/overlapping FPS badge
  rendering.
- FPS uses one fixed-size, fixed-position opaque
  HUD owner rectangle across output
  modes/framebuffers.
- Added PS/Home system-menu suspend/resume
  handling and clean framebuffer redraw on
  return.

v1.42
- Optimized the virtual rename keyboard for real
  PS3 hardware.
- Cursor movement redraws only the old/new key
  cells.
- Typing/backspace redraws only the filename
  field instead of the whole modal.

v1.41
- Added PS3 XMB Update History / control guide
  via PARAM.HIP.

v1.40
- START Exit is shown in all browser modes.

v1.39
- Renamed controls to Create PSU from folder and
  Extract PSU to folder.

v1.38
- Fixed current path text turning dark during
  fast folder redraws.

v1.37
- Removed unsafe cross-buffer writes during
  folder transitions.

v1.36
- Restored visible folder/file text under the
  translucent selection highlight.

v1.35
- Added the stronger navy/cyan UI redesign.

v1.33
- Empty Apollo SAVEDATA can return to /dev_hdd0.

v1.32
- Fixed list-panel cache allocation overflow
  causing HDD/USB switching crashes.

v1.27
- Stabilized the first few ICN preview frames
  before rotation begins.

v1.26
- Fixed PARAM.SFO packaging/version metadata.

v1.25
- Added Apollo fakeusb missing-folder warning
  and optional creation.

v1.24
- Prefer Apollo
  /dev_hdd0/tmp/fakeusb/PS2/SAVEDATA when
  available.
- Folder size field displays DIR.

v1.23
- Added rename support for files inside PSU
  archives.
- Renaming referenced ICNs also repairs ICON.SYS
  filename references.

v1.22
- Added delete confirmation.
- Added DD-MM-YYYY HH:MM timestamps.

v1.20
- Corrected generated blank-template ICN front-
  face orientation.

EARLIER RELEASES
- Pre-v1.20 development established the PSU
  browser/editor, HDD/USB browsing, PSU
  create/extract/inject/delete operations, ICN
  preview support, storage-device handling and
  core software-framebuffer UI.
