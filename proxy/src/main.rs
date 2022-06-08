use anyhow::Result;
use fluent_uri::Uri;
use std::env;
use std::process;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpListener;
use tokio::net::TcpStream;

async fn handle_client(mut client: TcpStream) -> Option<()> {
    // read from tcpstream
    let mut data_recv = [0_u8; 1024];
    let size = client.read(&mut data_recv).await.ok()?;

    // parse the http request
    let request: Vec<u8> = data_recv[0..size].to_vec();
    let request = std::str::from_utf8(request.as_slice()).ok()?;

    let mut tokens = request.split_whitespace();
    let method = tokens.next()?;
    let uri = tokens.next()?;

    let uri = Uri::parse(uri).ok()?;
    let host = uri.authority()?.host().as_str();
    let port = uri.authority()?.port().unwrap_or("80");

    // connect to origin server
    let mut origin = TcpStream::connect(format!("{}:{}", host, port))
        .await
        .ok()?;
    let header = format!(
        "{} {} HTTP/1.0\r\n\
        Host: {}\r\n\
        User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n\
        Connection: close\r\n\
        Proxy-Connection: close\r\n\
        ",
        method, uri, host
    );
    let header = header.as_bytes();
    origin.write_all(header).await.ok()?;

    let mut origin_recv = [0_u8; 1024];
    while let Ok(size) = origin.read(&mut origin_recv).await {
        if size == 0 {
            break;
        }
        client.write_all(&origin_recv[0..size]).await.ok()?;
    }

    // stream exit
    println!("Terminating connection with {}", client.peer_addr().ok()?);
    Some(())
}

#[tokio::main]
async fn main() -> Result<()> {
    // bind to the port and start listening
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        println!("Too few arguments.");
        process::exit(1);
    }
    let port = &args[1];
    let listener = TcpListener::bind(format!("0.0.0.0:{}", port)).await?;
    println!("Server listening on port {}", port);

    // wait for clients
    loop {
        let (stream, _) = listener.accept().await?;
        println!("New connection: {}", stream.peer_addr()?);

        // handle new client
        tokio::task::spawn(async move { handle_client(stream).await });
    }
}
