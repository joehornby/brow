//! Exercise a real Firefox browsing context from Rust without touching the user's profile.
use anyhow::{Context, Result, ensure};
use serde_json::{Value, json};
use std::{
    io::{BufRead, BufReader},
    net::{SocketAddr, TcpStream},
    process::{Child, Command, Stdio},
    time::Duration,
};
use tungstenite::{Message, WebSocket, client};

struct Firefox(Child);

impl Drop for Firefox {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

struct Session {
    socket: WebSocket<TcpStream>,
    id: u64,
}

impl Session {
    fn call(&mut self, method: &str, params: Value) -> Result<Value> {
        self.id += 1;
        self.socket.send(Message::text(
            json!({"id": self.id, "method": method, "params": params}).to_string(),
        ))?;
        loop {
            if let Message::Text(text) = self.socket.read()? {
                let reply: Value = serde_json::from_str(&text)?;
                if reply["id"] == self.id {
                    ensure!(reply["type"] == "success", "{method}: {reply}");
                    return Ok(reply["result"].clone());
                }
            }
        }
    }
}

fn main() -> Result<()> {
    let profile = tempfile::tempdir()?;
    let binary = std::env::var_os("FIREFOX_BIN")
        .unwrap_or_else(|| "/Applications/Firefox.app/Contents/MacOS/firefox".into());
    let mut firefox = Firefox(
        Command::new(binary)
            .args(["--headless", "--no-remote", "--profile"])
            .arg(profile.path())
            .args(["--remote-debugging-port", "0"])
            .stderr(Stdio::piped())
            .spawn()?,
    );
    let mut stderr = BufReader::new(firefox.0.stderr.take().unwrap());
    let mut line = String::new();
    let address: SocketAddr = loop {
        line.clear();
        ensure!(
            stderr.read_line(&mut line)? > 0,
            "Firefox exited before startup"
        );
        if let Some(endpoint) = line
            .trim()
            .strip_prefix("WebDriver BiDi listening on ws://")
        {
            break endpoint.parse()?;
        }
    };
    std::thread::spawn(move || std::io::copy(&mut stderr, &mut std::io::sink()));
    ensure!(
        address.ip().is_loopback(),
        "Expected a local Firefox endpoint"
    );
    let stream = TcpStream::connect_timeout(&address, Duration::from_secs(10))?;
    stream.set_read_timeout(Some(Duration::from_secs(20)))?;
    let (socket, _) = client(format!("ws://{address}/session"), stream)?;
    let mut session = Session { socket, id: 0 };
    let caps = session.call("session.new", json!({"capabilities": {}}))?;
    let tab = session.call("browsingContext.create", json!({"type":"tab"}))?;
    let context = tab["context"].as_str().context("missing context ID")?;
    session.call(
        "browsingContext.navigate",
        json!({"context":context,"wait":"complete",
        "url":"data:text/html,<title>Brow Gecko</title><input autofocus>"}),
    )?;
    session.call("input.performActions", json!({"context":context,"actions":[
        {"type":"key","id":"keyboard","actions":[{"type":"keyDown","value":"g"},{"type":"keyUp","value":"g"}]}]}))?;
    let input = session.call(
        "script.evaluate",
        json!({"target":{"context":context},
        "expression":"document.querySelector('input').value","awaitPromise":false}),
    )?;
    ensure!(input["result"]["value"] == "g", "keyboard input failed");
    let frame = session.call(
        "browsingContext.captureScreenshot",
        json!({"context":context}),
    )?;
    ensure!(
        frame["data"]
            .as_str()
            .is_some_and(|s| s.starts_with("iVBOR")),
        "expected a PNG frame"
    );
    println!(
        "Firefox {} rendered a Gecko frame and accepted keyboard input.",
        caps["capabilities"]["browserVersion"]
    );
    session.call("browser.close", json!({}))?;
    Ok(())
}
