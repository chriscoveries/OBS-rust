//! Exercise the CLI's output contract over a real obs-websocket connection.

use std::net::{TcpListener, TcpStream};
use std::process::{Command, Output};
use std::thread;
use std::time::Duration;

use obs_studio_rust::SUBPROTOCOL;
use serde_json::{Value, json};
use tungstenite::handshake::server::{Request, Response};
use tungstenite::{Message, WebSocket};

fn cli(args: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_obs-studio-rust"))
        .args(args)
        .env_remove("OBS_WEBSOCKET_PASSWORD")
        .output()
        .expect("run obs-studio-rust")
}

fn send(ws: &mut WebSocket<TcpStream>, op: u64, data: Value) -> Result<(), String> {
    ws.send(Message::text(json!({ "op": op, "d": data }).to_string()))
        .map_err(|e| e.to_string())
}

fn receive(ws: &mut WebSocket<TcpStream>) -> Result<Value, String> {
    match ws.read().map_err(|e| e.to_string())? {
        Message::Text(text) => serde_json::from_str(&text).map_err(|e| e.to_string()),
        other => Err(format!("expected JSON text, received {other:?}")),
    }
}

// The handshake callback's Result is defined by tungstenite.
#[allow(clippy::result_large_err)]
fn serve(
    listener: TcpListener,
    request_type: &str,
    response_data: Option<Value>,
    failure: bool,
) -> Result<(), String> {
    let (stream, _) = listener.accept().map_err(|e| e.to_string())?;
    let timeout = Some(Duration::from_secs(5));
    stream
        .set_read_timeout(timeout)
        .map_err(|e| e.to_string())?;
    stream
        .set_write_timeout(timeout)
        .map_err(|e| e.to_string())?;
    let mut ws = tungstenite::accept_hdr(stream, |request: &Request, mut response: Response| {
        assert_eq!(request.headers()["Sec-WebSocket-Protocol"], SUBPROTOCOL);
        response.headers_mut().insert(
            "Sec-WebSocket-Protocol",
            request.headers()["Sec-WebSocket-Protocol"].clone(),
        );
        Ok(response)
    })
    .map_err(|e| e.to_string())?;
    send(&mut ws, 0, json!({ "rpcVersion": 1 }))?;
    let identify = receive(&mut ws)?;
    assert_eq!(identify["op"], 1);
    assert!(identify["d"].get("authentication").is_none());
    send(&mut ws, 2, json!({ "negotiatedRpcVersion": 1 }))?;
    let request = receive(&mut ws)?;
    assert_eq!(request["op"], 6);
    assert_eq!(request["d"]["requestType"], request_type);
    let status = if failure {
        json!({ "result": false, "code": 501, "comment": "output not running" })
    } else {
        json!({ "result": true, "code": 100 })
    };
    let mut response = json!({
        "requestType": request_type,
        "requestId": request["d"]["requestId"],
        "requestStatus": status,
    });
    if let Some(data) = response_data {
        response["responseData"] = data;
    }
    send(&mut ws, 7, response)?;
    if failure {
        // A failed request returns before Client::close(), dropping the socket.
        return Ok(());
    }
    loop {
        match ws.read() {
            Ok(Message::Close(_)) => {}
            Err(tungstenite::Error::ConnectionClosed | tungstenite::Error::AlreadyClosed) => {
                return Ok(());
            }
            Err(e) => return Err(e.to_string()),
            Ok(other) => return Err(format!("expected close, received {other:?}")),
        }
    }
}

fn run_request(
    args: &[&str],
    request_type: &'static str,
    response_data: Option<Value>,
    failure: bool,
) -> (Output, Result<(), String>) {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = thread::spawn(move || serve(listener, request_type, response_data, failure));
    let output = Command::new(env!("CARGO_BIN_EXE_obs-studio-rust"))
        .args(["--host", "127.0.0.1", "--port", &port.to_string()])
        .args(args)
        .env_remove("OBS_WEBSOCKET_PASSWORD")
        .output();
    // The RED binary rejects --json before connecting. Wake accept() even on
    // that path; dropping this stream also releases the bounded handshake.
    drop(TcpStream::connect(("127.0.0.1", port)));
    let server_result = server.join().expect("server thread");
    (output.expect("run obs-studio-rust"), server_result)
}

fn assert_json(args: &[&str], request_type: &'static str, data: Option<Value>) {
    let expected = data.clone().unwrap_or(Value::Null);
    let (output, server) = run_request(args, request_type, data, false);
    assert!(
        output.status.success(),
        "{args:?}: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    server.expect("complete the request and closing handshake");
    assert!(output.stderr.is_empty());
    assert_eq!(output.stdout, format!("{expected}\n").as_bytes());
    assert_eq!(
        serde_json::from_slice::<Value>(&output.stdout).unwrap(),
        expected
    );
}

#[test]
fn json_status_preserves_every_response_field() {
    assert_json(
        &["--json", "status"],
        "GetStreamStatus",
        Some(json!({
            "outputActive": true,
            "outputReconnecting": false,
            "outputTimecode": "00:01:02.345",
            "outputBytes": 9007199254740993_u64,
            "outputSkippedFrames": 3,
            "outputTotalFrames": 900,
            "futureField": { "text": "line one\nline two — OBS", "value": null },
        })),
    );
}

#[test]
fn json_version_preserves_server_metadata() {
    assert_json(
        &["version", "--json"],
        "GetVersion",
        Some(json!({
            "obsVersion": "33.0.0",
            "obsWebSocketVersion": "5.6.3",
            "platformDescription": "Linux",
            "availableRequests": ["GetStreamStatus", "StartStream"],
            "rpcVersion": 1,
        })),
    );
}

#[test]
fn json_toggle_preserves_both_states() {
    for active in [false, true] {
        assert_json(
            &["--json", "toggle"],
            "ToggleStream",
            Some(json!({ "outputActive": active })),
        );
    }
}

#[test]
fn json_commands_preserve_absent_null_and_empty_data() {
    for (command, request) in [("start", "StartStream"), ("stop", "StopStream")] {
        for data in [None, Some(Value::Null), Some(json!({}))] {
            assert_json(&["--json", command], request, data);
        }
    }
}

#[test]
fn json_request_failure_keeps_stderr_and_exit_status() {
    let (output, server) = run_request(&["--json", "stop"], "StopStream", None, true);
    assert_eq!(output.status.code(), Some(1));
    server.expect("send failed request response");
    assert!(output.stdout.is_empty());
    assert_eq!(
        output.stderr,
        b"error: request failed (code 501): output not running\n"
    );
}

#[test]
fn default_text_output_is_unchanged() {
    for (command, request, data, text) in [
        (
            "status",
            "GetStreamStatus",
            json!({ "outputActive": false }),
            "stream: inactive\n",
        ),
        ("start", "StartStream", Value::Null, "stream: starting\n"),
        ("stop", "StopStream", Value::Null, "stream: stopping\n"),
        (
            "toggle",
            "ToggleStream",
            json!({ "outputActive": true }),
            "stream: starting\n",
        ),
        (
            "toggle",
            "ToggleStream",
            json!({ "outputActive": false }),
            "stream: stopping\n",
        ),
        (
            "version",
            "GetVersion",
            json!({ "obsVersion": "33.0.0", "obsWebSocketVersion": "5.6.3", "platformDescription": "Linux" }),
            "OBS 33.0.0 (obs-websocket 5.6.3, Linux)\n",
        ),
    ] {
        let (output, server) = run_request(&[command], request, Some(data), false);
        assert!(output.status.success(), "{command}: {output:?}");
        server.expect("complete default text command");
        assert!(output.stderr.is_empty());
        assert_eq!(output.stdout, text.as_bytes());
    }
}

#[test]
fn json_does_not_change_local_help_or_version() {
    for option in ["-h", "--help"] {
        let output = cli(&["--json", option]);
        assert!(output.status.success());
        assert!(output.stderr.is_empty());
        let text = String::from_utf8(output.stdout).unwrap();
        assert!(text.starts_with("Usage: obs-studio-rust"));
        assert!(text.contains("--json"));
    }
    for option in ["-V", "--version"] {
        let output = cli(&["--json", option]);
        assert!(output.status.success());
        assert!(output.stderr.is_empty());
        assert_eq!(
            output.stdout,
            format!("obs-studio-rust {}\n", env!("CARGO_PKG_VERSION")).as_bytes()
        );
    }
}

#[test]
fn json_does_not_change_argument_error_exit_codes() {
    for (args, code, message) in [
        (vec!["--json"], 2, "missing command"),
        (vec!["--json", "--invalid"], 2, "unknown option: --invalid"),
        (
            vec!["--json", "--port", "bad", "status"],
            2,
            "invalid port: bad",
        ),
        (vec!["--json", "invalid"], 1, "unknown command: invalid"),
    ] {
        let output = cli(&args);
        assert_eq!(output.status.code(), Some(code), "{args:?}");
        assert!(output.stdout.is_empty());
        assert!(String::from_utf8_lossy(&output.stderr).contains(message));
    }
}
