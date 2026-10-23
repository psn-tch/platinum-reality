# Platinum appearance with modern file interactions

The user's direction is to retain the Mac OS 9.2.2 visual identity and interaction
philosophy while keeping practical modern conveniences. These behaviors are
deliberate native adaptations (M), not authenticated historical claims.

## Desktop shortcuts

**Alt+D** opens Applications from the desktop or an active native application.
**Alt+Q** asks the active application's independent windows to close. Normal save
prompts and client cancellation remain authoritative; no process is killed.
Wayland has no generic application-Quit message, so applications may retain a
background process after their windows close. Requests apply to the active
application only. Finder's desktop remains running, and Alt+Q never logs out.

The shortcuts do not repeat while held. Alt+Q is blocked while a shell dialog,
menu or file drag is active, and while the application has a modal decision open.
Application windows receive individual normal close requests; cancellation is
per window, not an atomic application-wide transaction. Alt+D takes precedence
over applications' own Alt+D bindings, including Firefox's address shortcut;
**Ctrl+L** remains available in Firefox.

The [Desktop Picture chooser](desktop-picture.md) is another deliberate native
convenience: preview and save local pictures with fill, fit, center or tile
placement, without changing other appearance settings.

**Appearance → Options → Never turn the screen off automatically** is a saved
keep-awake preference. It takes effect on OK and composes with video/game
requests; Cancel preserves the previous choice. Manual locking and explicit
sleep remain available. This is a modern adaptation, not an authenticated
Mac OS 9 control. See [idle inhibition](idle-inhibit.md) for scope and checks.

## Current implementation

Finder's icon and list views export standard local file URLs and an explicit
owned URI-list payload. Qt 6.12 can redirect standard URLs through an advertised
file-transfer portal, which is absent on this project's private bus. Owned
destinations use `application/x-frankenstein-local-file-urls` when accompanied
by the Finder drag marker; both the native helper and Finder decode one bounded
snapshot, with no malformed-payload fallback. This marker is a format hint,
not authentication. The existing peer/workspace checks still apply. Other Finder
windows receive them through Qt's native drag events. The destination owns the
asynchronous file operation; a drag return value never causes Finder to delete
the originals separately. Default Finder transfers move; Option/Alt or Ctrl
requests a copy. Escape cancels. Multi-selection URLs are deduplicated.

The compositor routes offers using public QWaylandDrag APIs and actual surface
coordinate conversion. Pointer-button release is reconciled through public seat
state, with desktop release deferred until its receiver has decoded the URLs.
A five-second timeout cancels an uncompleted transport; rejected payloads create
no file job. It resolves background window content despite the
first-click activation shield, rejects window chrome/covered desktop targets and
explicitly clears a previous offer when leaving a target. A displayed drag icon
and copy badge follow the pointer. Source release ends the protocol grab.

Qt's public compositor API targets client surfaces. A dedicated owned executable,
`frankenstein-drop-receiver`, supplies a small private Wayland endpoint for the
shell desktop. Its surface is never laid out, given policy focus or listed as an
application. It receives normal Qt QWindow MIME events and forwards decoded URLs
to the desktop over the private bus. Only the exact launched helper PID and
executable may consume a single pending desktop release, within five seconds.
Final point and copy intent come from the compositor, not a sender-provided path
or a claimed app ID. Move-by-default on the desktop is restricted to the owned
Finder process; other origins default to copy and are not broadly qualified.
After decoding the URLs, the manager acknowledges an owned Finder source using
its private service, exact process ID and a per-drag random token. Finder verifies
the desktop bus owner/executable and cancels only a matching transport loop. This
idempotent completion covers older data-device protocols without treating a
transport return value as permission to delete files. Stale acknowledgements
cannot finish another drag. The native-test controller is allowed only in the
explicit FRANKENSTEIN_NATIVE_TEST venue; production expects the owned desktop.
The helper has no file-job or user-document ownership and is explicitly retired
with the manager. Its failure is reported rather than silently disabling drops.

URL validation rejects remote/mixed/relative payloads, NULs, more than 128 URLs
and decoded URI lists above 256 KiB. Qt owns the underlying MIME transport; this
decoded-size check is not a hard pre-read pipe limit. FileOperations repeats
workspace and filesystem checks. This remains an ordinary user process, not a
filesystem sandbox. A conflicting item is preserved at both ends; completed
earlier batch items remain completed and the failure reports their count.

Right-click menus use the same real actions as Finder's File/View menus and
desktop commands. They preserve an existing group when clicking a selected
member, select an unselected target and provide background-specific actions.
Single-item actions are disabled for groups; disk/Trash objects cannot be renamed
or trashed. Finder's Trash menu offers Put Away. The desktop menu dispatches from
the surviving root scope, after closing its popup. Native right-click on an
inactive window activates it and sends a single right-click to its client.

## Limits and qualification

Direct shell desktop drags support placement, desktop folder/Trash icons and
owned open Finder windows. The destination is the exact visible viewport or child
folder under the release point, including a background window; covered windows,
chrome and stale/disconnected peers are rejected. The Qt 6.12.0-only surface-ID
adapter fails closed on unsupported versions. Both conflicting files survive.

Hold left-click and drag from empty desktop space to draw a selection rectangle.
It works in either direction; Shift or Ctrl preserves the existing selection and
adds intersected entries. Escape restores the selection from before the gesture.
A plain empty-space click clears selection. Starting on a selected icon keeps its
group for dragging. Finder icon/list view selection uses Qt ExtendedSelection;
the icon view now explicitly shows its selection rectangle. These convenience
choices are not historical certification. Clipboard file Cut/Copy/Paste, generalized Undo, spring-loaded
folders, cross-host-desktop transfer, remote file import and universal Linux
application DnD are unfinished. Menus do not contain nonfunctional placeholders.

The local suites cover actual context menu clicks, selection preservation,
keyboard dismissal, icon/list drop destination jobs, decoded URL validation,
QWindow receiver events, desktop placement and conflict/copy/Trash behavior.
They do not establish native MIME transport or cross-process grabs. The seventh
native workflow passed that path: inactive-folder context menu, actual
Finder icon/list drags to desktop/another folder, Option-copy, Escape-cancel,
name-conflict preservation and a desktop context-menu Duplicate. Run the full gate using `scripts/play.sh`; it opens a hands-on session only
after the gate succeeds.

The [10:41 native run](evidence/20261018T164110-480975/native-tests.txt) passes all
seven workflows, including the complete new file/menu/close path. Applications
offering only portal-mediated file data still need portal integration; the owned
URL format does not provide it.

The [10:58 native run](evidence/20261018T165807-489894/native-tests.txt) passes
all eight workflows, including desktop-to-exact-folder-window move, child-folder
copy, cancel, chrome rejection and collision preservation. The extended eighth
workflow now adds an actual desktop marquee, Escape restoration and a two-file
group drop into an open folder; the [11:09 native run](evidence/20261018T170917-495823/native-tests.txt) passes that extension and all eight workflows. Local
checks use actual pointer drags in shell and Finder icon/list views, and verify
that selected groups transfer with their bytes intact.
