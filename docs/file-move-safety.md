# Cross-drive move safety

Trash, Put Away and explicit Move use a staged copy when the source and destination are on
different filesystems. This applies to individual files, directories and symlinks. Same-filesystem
moves use native rename; GIO's automatic copy/delete move fallback is disabled.

The copy records each source entry's device, inode, mode, size, link count, modification time and
change time. Before publishing the destination, every recorded version must still match. Cleanup
checks the inventory again using open directory handles; it does not recursively delete newly
arrived files. Symlinks are copied and removed as links. Hard-link cleanup accounts for the link
count and change-time updates caused by its own unlink operations.

If a source change is detected before publication, the temporary copy is discarded and the source
is left in place. If detected afterward, the published copy and changed source items remain;
the operation reports failure rather than claiming a completed move. Trash/Put Away retain the
recovery record when the copy was published. This may leave copies in both locations, requiring
the user to compare them before retrying.

This is conservative change detection, not an atomic snapshot or a guarantee against arbitrary
concurrent writers. There remains a last-check/unlink interval, and filesystem timestamp precision
varies. It does not provide transaction rollback or a faithful snapshot of actively changing files.

Regression tests use actual GIO operations on two writable filesystems, with test-only linker
wrappers to inject edits at exact copy and publication boundaries. They cover changed contents,
equal-size edits with restored mtime, added children, replaced source directories, individual files,
hard links, symbolic links and retained Trash metadata. No production mutation hooks are added.
