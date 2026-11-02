# Command-line copy into shell text fields

Source-only regression candidate; not compiled or run. No fix is claimed.

The new native case commandLineCopiesReachTheShellSearchField seeds an ordinary
application copy, completes a real ext-data-control tool copy, opens Applications
through Alt-D and pastes into its actual search field through Command-V. It must
show the newer tool copy rather than stale ordinary data. This covers a path
that the separate application-to-shell and command-line-to-client cases do not.

Source review suspects that the shared overriding guard avoids publishing tool
bytes to the process-local shell clipboard. That is a hypothesis until this
regression runs; do not modify or call it a proven runtime bug from source alone.
Run it on the clear-only and bounded candidates after the shared gate releases
the VM lease. If it fails, retain the red run, implement the smallest origin/echo
fix, and repeat normal plus sanitizer clipboard/owner-exit qualification.

These candidates are rebased on frozen update-25 source47f1280 for update26;
original d656b10 branches/pins remain available and unchanged.
