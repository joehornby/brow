---
name: delete-comment
description: Audit every code comment in this repository, put each in a category, then delete it, file an issue, or move it into an ADR. Use when the user runs /delete-comment, asks for a comment audit, or when the comment lint fails.
---

# Delete comments

This repository does not permit code comments. `tests/no_comments.rs` enforces the rule with a ratchet. This skill removes the comments that remain and lowers the ratchet.

## 1. List the comments

```sh
cargo test --test no_comments -- --nocapture comments_do_not_exceed_the_baseline
```

The output has one `path:line` row for each comment line and a total. If the total is zero, stop and tell the user.

## 2. Put each comment in a category

Read each comment together with the code around it. Use one category for each comment.

| Category | Test | Action |
| --- | --- | --- |
| Tautology | The comment repeats what the code already shows. | Delete it. |
| Stale | The comment refers to code or behaviour that is gone. | Delete it. |
| Excuse | The comment gives a reason for a hack: a guess, a copied system value, a timing workaround, a skipped case. | Delete it and file an issue. |
| Decision Record | The comment records an important decision that the code cannot show. | Move it into an ADR, then delete it. |

When a comment is between two categories, ask this: "Is the code below correct as designed, or is it a workaround?" A workaround is an Excuse.

## 3. Do the action

- **Excuse:** file one issue for each hack on the project issue tracker. Give the problem, why it is a hack, a possible fix, and the function names. Quote the reason from the comment, because the comment will be gone. Do not repair the hack in the same change.
- **Decision Record:** add the decision to `docs/adr/`. Extend an ADR that already covers the subject. If there is none, create `docs/adr/NNNN-short-title.md` with the next number and these sections: Context, Decision, Consequences. Refer to functions by name, not by line number.
- Delete each comment line. Do not change the code.

## 4. Lower the ratchet

```sh
COMMENT_BASELINE=lower cargo test --test no_comments
cargo build
```

`tests/comment-baseline.txt` holds `<comment lines> <path>` for each file that is still permitted to have comments. The command only lowers the numbers. Never increase a number by hand.

## 5. Report

Give the user:

- a table with the count for each category
- the issues that were filed
- the ADRs that were added or changed
- the number of lines removed (`git diff --stat` on the source files) and the number that remain
