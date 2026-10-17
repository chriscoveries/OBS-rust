//! Black-box tests of the client against a scripted obs-websocket v5 server.

use std::net::{TcpListener, TcpStream};
use std::thread;

use obs_studio_rust::{Client, Error, SUBPROTOCOL, authentication, describe_stream_status};
use serde_json::{Value, json};
use tungstenite::handshake::server::{Request, Response};
use tungstenite::protocol::CloseFrame;
use tungstenite::protocol::frame::coding::CloseCode;
use tungstenite::{Message, WebSocket};

const SALT: &str = "lM1GncleQOaCu9lT1yeUZhFYnqhsLLP1G5lAGo3ixaI=";
const CHALLENGE: &str = "+IxH4CnCiqpX1rM9scsNynZzbOe4KhDeYcTNS3PDaeY=";

/// The worked example from obs-websocket's protocol.md.
#[test]
fn authentication_matches_protocol_doc_example() {
    assert_eq!(
        authentication("supersecretpassword", SALT, CHALLENGE),
        "1Ct943GAT+6YQUUX47Ia/ncufilbe6+oD6lY+5kaCu4="
    );
}

fn send(ws: &mut WebSocket<TcpStream>, op: u64, d: Value) {
    ws.send(Message::text(json!({ "op": op, "d": d }).to_string()))
        .unwrap();
}

fn receive(ws: &mut WebSocket<TcpStream>) -> Value {
    loop {
        if let Message::Text(text) = ws.read().unwrap() {
            return serde_json::from_str(&text).unwrap();
        }
    }
}

/// Starts a one-connection server and returns its port. `script` runs after
/// the WebSocket handshake, which must request the JSON subprotocol.
// The handshake callback's Result type is fixed by tungstenite's `Callback` trait.
#[allow(clippy::result_large_err)]
fn serve(
    script: impl FnOnce(&mut WebSocket<TcpStream>) + Send + 'static,
) -> (u16, thread::JoinHandle<()>) {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    let handle = thread::spawn(move || {
        let (stream, _) = listener.accept().unwrap();
        let mut ws = tungstenite::accept_hdr(stream, |req: &Request, mut resp: Response| {
            let protocol = req.headers().get("Sec-WebSocket-Protocol").cloned();
            assert_eq!(
                protocol.as_ref().and_then(|p| p.to_str().ok()),
                Some(SUBPROTOCOL)
            );
            resp.headers_mut()
                .insert("Sec-WebSocket-Protocol", protocol.unwrap());
            Ok(resp)
        })
        .unwrap();
        script(&mut ws);
    });
    (port, handle)
}

fn hello(auth: bool) -> Value {
    let mut d = json!({ "obsWebSocketVersion": "5.6.3", "rpcVersion": 1 });
    if auth {
        d["authentication"] = json!({ "challenge": CHALLENGE, "salt": SALT });
    }
    d
}

/// Answers every request in order with `responses`, checking the type, then
/// completes the client's closing handshake.
fn answer(ws: &mut WebSocket<TcpStream>, responses: &[(&str, Value)]) {
    for (request_type, response_data) in responses {
        let request = receive(ws);
        assert_eq!(request["op"], 6);
        assert_eq!(request["d"]["requestType"], *request_type);
        let request_id = request["d"]["requestId"].clone();
        // An unrelated event first: the client must skip it.
        send(
            ws,
            5,
            json!({ "eventType": "StreamStateChanged", "eventIntent": 64 }),
        );
        send(
            ws,
            7,
            json!({
                "requestType": request_type,
                "requestId": request_id,
                "requestStatus": { "result": true, "code": 100 },
                "responseData": response_data,
            }),
        );
    }
    while ws.read().is_ok() {}
}

#[test]
fn identifies_without_password_and_reads_stream_status() {
    let (port, server) = serve(|ws| {
        send(ws, 0, hello(false));
        let identify = receive(ws);
        assert_eq!(identify["op"], 1);
        assert_eq!(identify["d"]["rpcVersion"], 1);
        assert!(identify["d"].get("authentication").is_none());
        send(ws, 2, json!({ "negotiatedRpcVersion": 1 }));
        answer(
            ws,
            &[(
                "GetStreamStatus",
                json!({
                    "outputActive": true,
                    "outputReconnecting": false,
                    "outputTimecode": "00:01:02.345",
                    "outputBytes": 1024,
                    "outputSkippedFrames": 3,
                    "outputTotalFrames": 900,
                }),
            )],
        );
    });
    let mut client = Client::connect("127.0.0.1", port, None).unwrap();
    let status = client.request("GetStreamStatus", None).unwrap();
    assert_eq!(
        describe_stream_status(&status),
        "stream: live 00:01:02.345, 1024 bytes sent, 3/900 frames skipped"
    );
    client.close().unwrap();
    server.join().unwrap();
}

#[test]
fn authenticates_and_runs_several_requests() {
    let (port, server) = serve(|ws| {
        send(ws, 0, hello(true));
        let identify = receive(ws);
        assert_eq!(
            identify["d"]["authentication"],
            "1Ct943GAT+6YQUUX47Ia/ncufilbe6+oD6lY+5kaCu4="
        );
        send(ws, 2, json!({ "negotiatedRpcVersion": 1 }));
        answer(
            ws,
            &[
                ("StartStream", Value::Null),
                ("ToggleStream", json!({ "outputActive": false })),
            ],
        );
    });
    let mut client = Client::connect("127.0.0.1", port, Some("supersecretpassword")).unwrap();
    assert_eq!(client.request("StartStream", None).unwrap(), Value::Null);
    assert_eq!(
        client.request("ToggleStream", None).unwrap(),
        json!({ "outputActive": false })
    );
    client.close().unwrap();
    server.join().unwrap();
}

#[test]
fn missing_password_is_reported_before_identify() {
    let (port, server) = serve(|ws| {
        send(ws, 0, hello(true));
        // The client gives up without sending Identify.
        assert!(matches!(ws.read(), Err(_) | Ok(Message::Close(_))));
    });
    let err = Client::connect("127.0.0.1", port, None).err().unwrap();
    assert!(matches!(err, Error::PasswordRequired), "{err}");
    server.join().unwrap();
}

#[test]
fn wrong_password_surfaces_close_code_4009() {
    let (port, server) = serve(|ws| {
        send(ws, 0, hello(true));
        receive(ws);
        ws.close(Some(CloseFrame {
            code: CloseCode::from(4009),
            reason: "Authentication failed.".into(),
        }))
        .unwrap();
        while ws.read().is_ok() {}
    });
    let err = Client::connect("127.0.0.1", port, Some("wrong"))
        .err()
        .unwrap();
    assert!(
        matches!(&err, Error::Closed { code: 4009, reason } if reason == "Authentication failed."),
        "{err}"
    );
    server.join().unwrap();
}

#[test]
fn failed_request_reports_status_code_and_comment() {
    let (port, server) = serve(|ws| {
        send(ws, 0, hello(false));
        receive(ws);
        send(ws, 2, json!({ "negotiatedRpcVersion": 1 }));
        let request = receive(ws);
        send(
            ws,
            7,
            json!({
                "requestType": "StopStream",
                "requestId": request["d"]["requestId"],
                "requestStatus": { "result": false, "code": 501, "comment": "output not running" },
            }),
        );
        while ws.read().is_ok() {}
    });
    let mut client = Client::connect("127.0.0.1", port, None).unwrap();
    let err = client.request("StopStream", None).unwrap_err();
    assert!(
        matches!(&err, Error::Request { code: 501, comment: Some(c) } if c == "output not running"),
        "{err}"
    );
    assert_eq!(
        err.to_string(),
        "request failed (code 501): output not running"
    );
    client.close().unwrap();
    server.join().unwrap();
}

#[test]
fn inactive_stream_status() {
    assert_eq!(
        describe_stream_status(&json!({ "outputActive": false })),
        "stream: inactive"
    );
    assert_eq!(
        describe_stream_status(&json!({ "outputActive": true, "outputReconnecting": true })),
        "stream: live (reconnecting)"
    );
}
