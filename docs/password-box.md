# Password box

Lily Hewitt asked for this on 2026-11-14: "make the polkit more 9.2.2 style". When Software Update, the
App Store, Fingerprints or anything else uses pkexec, polkit asks the session's authentication agent for
an administrator's password. Since update 44 that agent was KDE's (`polkit-kde-authentication-agent-1`),
a Plasma-style window. Platinum Reality now has its own agent, `platinum-authentication-agent`, so the
box looks like Mac OS 9.2.2's password dialogs.

## What it does

- The desktop starts it once per login, before KDE's: `NativeManager::startAuthenticationAgent` runs the
  first installed agent in its list, and ours is first. KDE's agent stays a dependency and is used when
  ours is not installed.
- It registers with polkit for the login session it runs in (`UnixSessionSubject` of its own process), so
  it answers only for this session.
- Each request is one box: the message polkit gives, **Name** and **Password**, **Cancel** and **OK**.
  The name is the person logged in when polkit allows them (as 9.2.2 fills in the owner's name);
  otherwise the first administrator polkit names. With several, Name is a pop-up menu.
- The password goes straight to polkit's helper (`polkit-agent-helper-1`), which asks PAM. The agent
  never logs it, the field is emptied as soon as it is sent, and the controller holds it only until PAM's
  question arrives (a moment after OK).
- PAM is asked only when OK is pressed. A conversation that ends unanswered (Cancel, or polkit
  withdrawing the request because the program quit) counts as a failed password in pam_faillock, so
  KDE's agent, which starts PAM as the box opens, could lock an account with three canceled boxes.
  Found on 2026-11-14 while testing this agent: two withdrawn requests left two failure records.
  The exception: when polkit's PAM stack (`/etc/pam.d/polkit-1` and what it includes) uses
  `pam_fprintd`, PAM starts as the box opens so the fingerprint reader works without typing, and
  Cancel then counts as a failure as it always did.
- PAM's own words (for example a fingerprint prompt from `pam_fprintd`) appear under the fields.
- A wrong password asks again, at most three times in one box: pam_faillock locks the account after three
  failures by default. As in 9.2.2, a stop alert says *The password for "Name" failed authentication.
  Please try again.*, and after OK the box shows the old password selected, so typing replaces it. The
  field is really empty: it only draws that many selected bullets, and Return or OK on them sends nothing.
- The look follows 9.2.2's Unlock Keychain box (Keychain Access, captured 2026-11-14), with Create
  Keychain's bold line beside the icon for polkit's message: an etched line, then bold labels right-aligned
  to Name and Password, then Cancel and the default OK. The reference shots stay out of the repository
  (Apple's artwork).
- Its icon is the keychain from the person's own Keychain Access when the Asset Importer brought it in
  (`icons/keychain.png`, 'icl8'/'ICN#' 1031), else polkit's icon or the theme's password icon, else a
  padlock drawn by the agent: never an empty space. The stop alert, like every Platinum alert, uses the
  imported 9.2.2 stop, caution and note icons (`icons/alert-*.png`, System Resources -3822/-3821/-3820)
  when present, else PlatinumStyle's own drawings.
- One box at a time. A second request while a box is up is refused, as KDE's agent does.
- The desktop recognizes the box by its program (`authenticationAgentClient`), keeps it above other
  windows and keeps the window that asked just beneath it (update 45).

## Parts

- `src/authentication/authentication_controller.*`: the rules above, independent of polkit.
- `src/authentication/authentication_dialog.*`: the box itself. The controller uses only its interface
  (`ask`, `showMessage`, `rejectAnswer`, `setWaiting`; `answered`, `identityChosen`, `canceled`), so
  its look can follow the 9.2.2 references without touching the rules.
- `src/authentication/polkit_agent.*` and `main.cpp`: polkit-qt6's Listener and Session.
- Tests: `tests/authentication_tests.cpp` drives the controller and the box with a scripted
  conversation, without polkitd.
