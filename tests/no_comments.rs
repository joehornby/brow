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
                let mut depth = 1;
                i += 2;
                while i < text.len() {
                    if text[i] == '*' && at(i + 1) == '/' {
                        depth -= 1;
                        i += 1;
                        if depth == 0 {
                            break;
                        }
                    } else if rust && text[i] == '/' && at(i + 1) == '*' {
                        depth += 1;
                        i += 1;
                    } else if text[i] == '\n' {
                        line += 1;
                        lines.push(line);
                    }
                    i += 1;
                }
            }
            '"' => {
                let mut hashes = 0;
                while hashes < i && text[i - 1 - hashes] == '#' {
                    hashes += 1;
                }
                let raw = rust && i > hashes && text[i - 1 - hashes] == 'r';
                let delimiter = if rust { None } else { cpp_raw_delimiter(&text, i) };
                i += 1;
                if let Some(delimiter) = delimiter {
                    i += delimiter.len() + 1;
                    while i < text.len() {
                        let end = i + 1 + delimiter.len();
                        if text[i] == ')' && text[i + 1..].starts_with(&delimiter) && at(end) == '"' {
                            i = end;
                            break;
                        }
                        if text[i] == '\n' {
                            line += 1;
                        }
                        i += 1;
                    }
                    i += 1;
                    continue;
                }
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
    lines.dedup();
    lines
}

fn cpp_raw_delimiter(text: &[char], quote: usize) -> Option<Vec<char>> {
    if quote == 0 || text[quote - 1] != 'R' {
        return None;
    }
    if quote > 1 && (text[quote - 2].is_alphanumeric() || text[quote - 2] == '_') && !"uUL8".contains(text[quote - 2]) {
        return None;
    }
    let rest = &text[quote + 1..];
    let length = rest.iter().take(17).position(|c| *c == '(')?;
    let delimiter = &rest[..length];
    let valid = delimiter.iter().all(|c| !c.is_whitespace() && !"\\\")".contains(*c));
    valid.then(|| delimiter.to_vec())
}

fn heredoc_delimiter(row: &[char], start: usize) -> Option<(String, bool, usize)> {
    let mut i = start;
    let strip_tabs = row.get(i) == Some(&'-');
    if strip_tabs {
        i += 1;
    }
    while row.get(i).is_some_and(|c| *c == ' ' || *c == '\t') {
        i += 1;
    }
    let mut word = String::new();
    match row.get(i).copied() {
        Some(open) if open == '"' || open == '\'' => {
            i += 1;
            while row.get(i).is_some_and(|c| *c != open) {
                word.push(row[i]);
                i += 1;
            }
            i += 1;
        }
        Some(first) if first.is_alphabetic() || first == '_' || first == '\\' => {
            if first == '\\' {
                i += 1;
            }
            while row.get(i).is_some_and(|c| c.is_alphanumeric() || *c == '_') {
                word.push(row[i]);
                i += 1;
            }
        }
        _ => {}
    }
    (!word.is_empty()).then_some((word, strip_tabs, i))
}

fn hash_comment_lines(source: &str, shell: bool) -> Vec<usize> {
    let rows: Vec<&str> = source.lines().collect();
    let ends = |row: &str, word: &str, strip_tabs: bool| {
        (if strip_tabs { row.trim_start_matches('\t') } else { row }) == word
    };
    let mut lines = Vec::new();
    let mut quote: Option<(char, bool)> = None;
    let mut heredocs: Vec<(String, bool)> = Vec::new();
    for (index, raw) in rows.iter().enumerate() {
        if index == 0 && raw.starts_with("#!") {
            continue;
        }
        if let Some((word, strip_tabs)) = heredocs.first() {
            if ends(raw, word, *strip_tabs) {
                heredocs.remove(0);
            }
            continue;
        }
        let row: Vec<char> = raw.chars().collect();
        let at = |i: usize| row.get(i).copied().unwrap_or('\0');
        let (mut i, mut word_start) = (0, true);
        while i < row.len() {
            let c = row[i];
            let starts_word = word_start;
            word_start = quote.is_none() && (c.is_whitespace() || ";&|()<>".contains(c));
            match quote {
                Some(('"', _)) if c == '\\' => i += 1,
                Some((open, true)) if c == open && at(i + 1) == open && at(i + 2) == open => {
                    quote = None;
                    i += 2;
                }
                Some((open, false)) if c == open => quote = None,
                Some(_) => {}
                None if shell && c == '\\' => {
                    word_start = false;
                    i += 1;
                }
                None if c == '"' || c == '\'' => {
                    let triple = !shell && at(i + 1) == c && at(i + 2) == c;
                    quote = Some((c, triple));
                    if triple {
                        i += 2;
                    }
                }
                None if c == '#' => {
                    if !shell || starts_word {
                        lines.push(index + 1);
                        break;
                    }
                }
                None if shell && c == '<' && at(i + 1) == '<' => {
                    if at(i + 2) == '<' {
                        i += 2;
                    } else if let Some((word, strip_tabs, next)) = heredoc_delimiter(&row, i + 2) {
                        if rows[index + 1..].iter().any(|later| ends(later, &word, strip_tabs)) {
                            heredocs.push((word, strip_tabs));
                            i = next;
                            continue;
                        }
                    }
                }
                None => {}
            }
            i += 1;
        }
        if !shell && quote.is_some_and(|(_, triple)| !triple) {
            quote = None;
        }
    }
    lines
}

fn comment_lines(path: &str) -> Option<Vec<usize>> {
    let source = fs::read_to_string(path).ok()?;
    let extension = Path::new(path).extension().and_then(|e| e.to_str());
    match extension {
        Some(e) if SLASH_STYLE.contains(&e) => Some(slash_comment_lines(&source, e == "rs")),
        Some(e) if HASH_STYLE.contains(&e) => Some(hash_comment_lines(&source, e == "sh")),
        None if source.starts_with("#!") => Some(hash_comment_lines(&source, true)),
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
    assert_eq!(slash_comment_lines("auto s = R\"(a \" // no)\"; auto t = u8R\"x(\n)\" // no\n)x\";\n// three", false), [4]);
    assert_eq!(slash_comment_lines("printf(\"%\" PRIxPTR\"\\n\", p); // one", false), [1]);
}

#[test]
fn slash_scanner_follows_nested_rust_blocks() {
    assert_eq!(slash_comment_lines("/* outer /* inner */ \" */\n// two\n", true), [1, 2]);
    assert_eq!(slash_comment_lines("/* a /* b */ int x; // one\n", false), [1]);
}

#[test]
fn hash_scanner_ignores_shell_literals() {
    let shell = "#!/bin/bash\necho \"$#\" '# no' \"a # no\" ${#name} ${name#prefix}\ncd x # three\n# four\n";
    assert_eq!(hash_comment_lines(shell, true), [3, 4]);
    assert_eq!(hash_comment_lines("echo \\\" # one\necho \\ #no\necho ok;# three\n", true), [1, 3]);
    let heredoc = "cat <<'EOF'\n# no\nEOF\ncat <<-END <<< \"# no\"\n\t# no\n\tEND\necho $((1 << n)) # seven\n";
    assert_eq!(hash_comment_lines(heredoc, true), [7]);
}

#[test]
fn hash_scanner_ignores_toml_literals() {
    let toml = "a = 1# one\nb = \"\"\"x \" # no\ny\"\"\" # three\nc = '# no' # four\nd = '''\n# no\n'''\n";
    assert_eq!(hash_comment_lines(toml, false), [1, 3, 4]);
}
