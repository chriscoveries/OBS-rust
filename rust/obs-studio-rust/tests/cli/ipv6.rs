//! Connection tests against a loopback obs-websocket v5 server.

use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, TcpListener, TcpStream};
use std::thread;
use std::time::Duration;

use obs_studio_rust::{Client, Error, SUBPROTOCOL};
use serde_json::{Value, json};
use tungstenite::handshake::server::{Request, Response};
use tungstenite::{Message, WebSocket};

const IO_TIMEOUT: Duration = Duration::from_secs(5);

fn send(ws: &mut WebSocket<TcpStream>, op: u64, data: Value) {
    ws.send(Message::text(json!({ "op": op, "d": data }).to_string()))
        .unwrap();
}

fn receive(ws: &mut WebSocket<TcpStream>) -> Value {
    loop {
        if let Message::Text(text) = ws.read().unwrap() {
            return serde_json::from_str(&text).unwrap();
        }
    }
}

// The HTTP callback's error type is fixed by tungstenite.
#[allow(clippy::result_large_err)]
fn status_over(host: &str, listen_ip: IpAddr, http_host: &str) -> Result<(), Error> {
    let listener = TcpListener::bind((listen_ip, 0))
        .unwrap_or_else(|error| panic!("loopback {listen_ip} is unavailable: {error}"));
    let address = listener.local_addr().unwrap();
    let expected_host = format!("{http_host}:{}", address.port());
    let server = thread::spawn(move || -> Result<(), String> {
        let (stream, _) = listener.accept().unwrap();
        stream.set_read_timeout(Some(IO_TIMEOUT)).unwrap();
        stream.set_write_timeout(Some(IO_TIMEOUT)).unwrap();
        let mut ws =
            tungstenite::accept_hdr(stream, |request: &Request, mut response: Response| {
                assert_eq!(request.headers()["Host"], expected_host);
                assert_eq!(request.headers()["Sec-WebSocket-Protocol"], SUBPROTOCOL);
                response
                    .headers_mut()
                    .insert("Sec-WebSocket-Protocol", SUBPROTOCOL.parse().unwrap());
                Ok(response)
            })
            .map_err(|error| error.to_string())?;

        send(
            &mut ws,
            0,
            json!({ "obsWebSocketVersion": "5.6.3", "rpcVersion": 1 }),
        );
        let identify = receive(&mut ws);
        assert_eq!(identify["op"], 1);
        assert_eq!(identify["d"]["rpcVersion"], 1);
        assert_eq!(identify["d"]["eventSubscriptions"], 0);
        send(&mut ws, 2, json!({ "negotiatedRpcVersion": 1 }));

        let request = receive(&mut ws);
        assert_eq!(request["op"], 6);
        assert_eq!(request["d"]["requestType"], "GetStreamStatus");
        send(
            &mut ws,
            7,
            json!({
                "requestType": "GetStreamStatus",
                "requestId": request["d"]["requestId"],
                "requestStatus": { "result": true, "code": 100 },
                "responseData": { "outputActive": false },
            }),
        );
        assert!(matches!(ws.read().unwrap(), Message::Close(_)));
        assert!(matches!(
            ws.read(),
            Err(tungstenite::Error::ConnectionClosed)
        ));
        Ok(())
    });

    let result = (|| {
        let mut client = Client::connect(host, address.port(), None)?;
        let status = client.request("GetStreamStatus", None)?;
        client.close()?;
        assert_eq!(status, json!({ "outputActive": false }));
        Ok(())
    })();
    if result.is_err() {
        // If host parsing failed before TCP connect, wake accept with an EOF
        // so the failed case joins its server too. Reads have a deadline.
        let _ = TcpStream::connect_timeout(&address, IO_TIMEOUT);
    }
    let served = server.join().expect("loopback server panicked");
    if result.is_ok() {
        served.expect("loopback server handshake failed");
    }
    result
}

#[test]
fn connects_to_bare_ipv6_host() {
    status_over("::1", Ipv6Addr::LOCALHOST.into(), "[::1]").unwrap();
}

#[test]
fn connects_to_bracketed_ipv6_host() {
    status_over("[::1]", Ipv6Addr::LOCALHOST.into(), "[::1]").unwrap();
}

#[test]
fn preserves_ipv4_host_header() {
    status_over("127.0.0.1", Ipv4Addr::LOCALHOST.into(), "127.0.0.1").unwrap();
}

#[test]
fn preserves_dns_host_header() {
    status_over("localhost", Ipv4Addr::LOCALHOST.into(), "localhost").unwrap();
}

#[test]
fn rejects_malformed_ipv6_brackets() {
    for host in ["[::1", "::1]", "[[::1]]"] {
        assert!(
            status_over(host, Ipv6Addr::LOCALHOST.into(), "[::1]").is_err(),
            "accepted malformed IPv6 host: {host}"
        );
    }
}

#[test]
fn rejects_brackets_around_non_ipv6_hosts() {
    for host in ["[127.0.0.1]", "[localhost]"] {
        assert!(
            status_over(host, Ipv4Addr::LOCALHOST.into(), "localhost").is_err(),
            "accepted bracketed non-IPv6 host: {host}"
        );
    }
}
