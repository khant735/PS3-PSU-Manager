Stage66 experimental automatic GameDB updater (source-only)

At startup, launches a PS3 PPU background thread to refresh the PS2 title database if no successful update marker exists or the marker is at least seven days old. This avoids blocking the UI during initial download. The existing manual Options action remains available; it will refuse to run while the automatic refresh is active. A successful update writes PS2_GAME_TITLES.check; a failed download retains the old database and does not create a success marker.

IMPORTANT LIMITATIONS:
- Not compiled or tested on PS3/RPCS3.
- The background worker currently does not report download progress or expose a cancel control.
- Redump DAT format and GameDB upstream formats have not been validated against actual downloaded payloads.
- Existing updater is synchronous when invoked manually and uses libcurl directly.
- A failed update will retry at the next application launch.
- Save archives are not modified by this feature.
