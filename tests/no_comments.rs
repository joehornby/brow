use std::{collections::BTreeMap, env, fs, path::Path, process::Command};

const BASELINE: &str = "tests/comment-baseline.txt";
const SLASH_STYLE: [&str; 8] = ["rs", "m", "mm", "h", "c", "cpp", "inc", "swift"];
const HASH_STYLE: [&str; 2] = ["toml", "sh"];

fn slash_comment_lines(source: &str, rust: bool) -> Vec<usize> {
    let text: Vec<char> = source.chars().collect();
    let at = |i: usize| text.get(i).copied().unwrap_or('\0');
    let mut lines = Vec::new();
    let (mut i, mut line) = (0, 1);
    while i < text.len() {
        match text[i] {
            '\n' => line += 1,
            '/' if at(i + 1) == '/' => {
                lines.push(line);
                while i + 1 < text.len() && text[i + 1] != '\n' {
                    i += 1;
                }
            }
            '/' if at(i + 1) == '*' => {
                lines.push(line);
                i += 2;
                while i < text.len() && !(text[i] == '*' && at(i + 1) == '/') {
                    if text[i] == '\n' {
                        line += 1;
                        lines.push(line);
                    }
                    i += 1;
                }
                i += 1;
            }
            '"' => {
                let mut hashes = 0;
                while hashes < i && text[i - 1 - hashes] == '#' {
                    hashes += 1;
                }
                let raw = rust && i > hashes && text[i - 1 - hashes] == 'r';
                i += 1;
                while i < text.len() {
                    if text[i] == '\n' {
                        line += 1;
                    } else if raw {
                        if text[i] == '"' && (1..=hashes).all(|n| at(i + n) == '#') {
                            break;
                        }
                    } else if text[i] == '\\' {
                        if at(i + 1) == '\n' {
                            line += 1;
                        }
                        i += 1;
                    } else if text[i] == '"' {
                        break;
                    }
                    i += 1;
                }
            }
            '\'' if at(i + 1) == '\\' => {
                i += 2;
                while i < text.len() && text[i] != '\'' && text[i] != '\n' {
                    i += 1;
                }
            }
            '\'' if at(i + 2) == '\'' => i += 2,
            _ => {}
        }
        i += 1;
    }
    lines
}

fn hash_comment_lines(source: &str) -> Vec<usize> {
    let mut lines = Vec::new();
    let mut quote = None;
    for (index, row) in source.lines().enumerate() {
        if index == 0 && row.starts_with("#!") {
            continue;
        }
        let mut previous = ' ';
        let mut escaped = false;
        for c in row.chars() {
            match quote {
                Some('"') if escaped => escaped = false,
                Some('"') if c == '\\' => escaped = true,
                Some(open) if c == open => quote = None,
                Some(_) => {}
                None if c == '"' || c == '\'' => quote = Some(c),
                None if c == '#' && previous.is_whitespace() => {
                    lines.push(index + 1);
                    break;
                }
                None => {}
            }
            previous = c;
        }
    }
    lines
}

fn comment_lines(path: &str) -> Option<Vec<usize>> {
    let source = fs::read_to_string(path).ok()?;
    let extension = Path::new(path).extension().and_then(|e| e.to_str());
    match extension {
        Some(e) if SLASH_STYLE.contains(&e) => Some(slash_comment_lines(&source, e == "rs")),
        Some(e) if HASH_STYLE.contains(&e) => Some(hash_comment_lines(&source)),
        None if source.starts_with("#!") => Some(hash_comment_lines(&source)),
        _ => None,
    }
}

fn baseline() -> BTreeMap<String, usize> {
    fs::read_to_string(BASELINE)
        .unwrap_or_default()
        .lines()
        .filter_map(|row| {
            let (count, path) = row.trim().split_once(' ')?;
            Some((path.to_owned(), count.parse().ok()?))
        })
        .collect()
}

#[test]
fn comments_do_not_exceed_the_baseline() {
    let listing = Command::new("git").args(["ls-files", "-z"]).output().expect("git must list the tracked files");
    assert!(listing.status.success(), "git ls-files failed");
    let allowed = baseline();
    let mut next = BTreeMap::new();
    let (mut added, mut removed, mut total) = (Vec::new(), false, 0);
    for path in String::from_utf8_lossy(&listing.stdout).split('\0') {
        let Some(found) = comment_lines(path) else { continue };
        let limit = allowed.get(path).copied().unwrap_or(0);
        total += found.len();
        for line in &found {
            println!("{path}:{line}");
        }
        if found.len() > limit {
            added.push(format!("{path}: {} comment lines, baseline permits {limit}", found.len()));
        }
        removed |= found.len() < limit;
        if !found.is_empty() {
            next.insert(path.to_owned(), found.len().min(limit));
        }
    }
    removed |= allowed.keys().any(|path| !next.contains_key(path));
    println!("{total} comment lines in total");
    if env::var("COMMENT_BASELINE").as_deref() == Ok("lower") {
        let rows: String = next.iter().filter(|(_, n)| **n > 0).map(|(path, n)| format!("{n} {path}\n")).collect();
        fs::write(BASELINE, rows).expect("the baseline must be writable");
        removed = false;
    }
    assert!(
        added.is_empty(),
        "Comments are not permitted. Put a decision in docs/adr and delete the comment.\n{}",
        added.join("\n")
    );
    assert!(
        !removed,
        "Comments were deleted. Lower the baseline: COMMENT_BASELINE=lower cargo test --test no_comments"
    );
}

#[test]
fn slash_scanner_ignores_literals() {
    let rust = "let a = \"http://x\"; // one\nlet b = r#\"// \"no\" \"#; let c = '/'; let d: &'static str = \"\\\"//\";\n/* two\nthree */ let e = '\\'';";
    assert_eq!(slash_comment_lines(rust, true), [1, 3, 4]);
    assert_eq!(slash_comment_lines("NSString *s = @\"a//b\"; char c = '\"'; // one", false), [1]);
}

#[test]
fn hash_scanner_ignores_literals() {
    let shell = "#!/bin/bash\necho \"$#\" '# no' \"a # no\" ${#name}\ncd x # one\n# two\n";
    assert_eq!(hash_comment_lines(shell), [3, 4]);
}
