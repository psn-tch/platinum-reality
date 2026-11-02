# The Platinum Log In for SDDM

Computers installed from the Platinum Reality live disk log in through SDDM. The theme in
`packaging/sddm/platinum-reality` shows the same Mac OS 9.2.2-style Log In dialog as the tty1 Log In
(`src/lock/LockScreen.qml`), on a Mac OS 9 desktop color. It's an original adaptation; no Apple art.

- **One account:** "Welcome, <full name>" and the password field, which has the keyboard.
- **More than one account:** a User pop-up menu above the welcome line, as Mac OS 9's Multiple Users
  asked who is logging in. The last user is chosen at first.
- A Session pop-up when there is more than one session, plus Restart and Shut Down (each only when
  SDDM allows it).
- Return or Log In logs in. A wrong password says "Sorry, that password is incorrect. Please try
  again." and clears the field. A Caps Lock warning appears while Caps Lock is on.
- `theme.conf`: `backgroundColor` (default `#666699`) and an optional `background` image path.

It is installed into `/usr/share/sddm/themes/platinum-reality`. The installer selects it on the target
with an SDDM drop-in, `[Theme] Current=platinum-reality`. That's the install engine's part (Rocky).

## Tests

`tests/sddm_theme_tests.cpp` loads the real `Main.qml` against stand-ins for SDDM's `sddm`,
`userModel`, `sessionModel`, `config` and `keyboard`, using SDDM's role names. It covers:
- one account;
- several accounts;
- choosing a session;
- a failed then successful login;
- Restart;
- a hidden Shut Down;
- Caps Lock.

Screenshots: `build/artifacts/sddm-theme.png`, `build/artifacts/sddm-theme-users.png`. The real
greeter is checked in the VM install proof.
