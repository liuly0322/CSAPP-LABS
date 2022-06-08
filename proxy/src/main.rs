use anyhow::Result;
use fluent_uri::Uri;
use std::env;
use std::process;
use std::sync::Arc;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpListener;
use tokio::net::TcpStream;
use tokio::sync::mpsc;
use tokio::sync::oneshot;

enum CacheOperation {
    Get,
    Put,
}

struct Message {
    operation: CacheOperation,
    uri: String,
    buffer: Option<Arc<Vec<u8>>>,
    tx: Option<oneshot::Sender<Option<Arc<Vec<u8>>>>>,
}

impl Message {
    pub fn new(
        operation: CacheOperation,
        uri: String,
        buffer: Option<Arc<Vec<u8>>>,
        tx: Option<oneshot::Sender<Option<Arc<Vec<u8>>>>>,
    ) -> Self {
        Message {
            operation,
            uri,
            buffer,
            tx,
        }
    }
}

async fn handle_client(mut client: TcpStream, tx: mpsc::Sender<Message>) -> Option<()> {
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
    let path = uri.path();

    // see if in cache
    let (buffer_tx, buffer_rx) = oneshot::channel();
    tx.send(Message::new(
        CacheOperation::Get,
        uri.to_string(),
        None,
        Some(buffer_tx),
    ))
    .await
    .ok()?;

    let buffer = buffer_rx.await.unwrap();
    // if in cache
    if buffer.is_some() {
        client.write_all(buffer.unwrap().as_slice()).await.ok()?;
        println!("finish proxy by cache: {}", client.peer_addr().ok()?);
        return Some(());
    }

    // connect to origin server
    let mut origin = TcpStream::connect(format!("{}:{}", host, port))
        .await
        .ok()?;
    let header = format!(
        "{} {} HTTP/1.0\r\n\
        Host: {}:{}\r\n\
        User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n\
        Connection: close\r\n\
        Proxy-Connection: close\r\n\
        \r\n",
        method, path, host, port
    );
    let header = header.as_bytes();
    origin.write_all(header).await.ok()?;

    let mut buffer: Vec<u8> = vec![];
    let mut origin_recv = [0_u8; 1024];
    while let Ok(size) = origin.read(&mut origin_recv).await {
        if size == 0 {
            break;
        }

        buffer.append(&mut origin_recv[0..size].to_vec());
        client.write_all(&origin_recv[0..size]).await.ok()?;
    }

    // put value in cache
    tx.send(Message::new(
        CacheOperation::Put,
        uri.to_string(),
        Some(Arc::new(buffer)),
        None,
    ))
    .await
    .ok()?;

    // stream exit
    println!("finish proxy to server: {}", client.peer_addr().ok()?);
    Some(())
}

async fn handle_cache(mut rx: mpsc::Receiver<Message>) -> Result<()> {
    let mut cache: lru::LruCache<String, Arc<Vec<u8>>> = lru::LruCache::new(32);

    loop {
        // loop for waiting cache operation request
        let received = rx.recv().await.unwrap();
        match received.operation {
            CacheOperation::Get => {
                let buffer = cache.get(&received.uri).cloned();
                received.tx.unwrap().send(buffer).unwrap();
            }
            CacheOperation::Put => {
                cache.put(received.uri, received.buffer.unwrap());
            }
        }
    }
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

    let (op_tx, op_rx) = mpsc::channel(128);
    tokio::task::spawn(async move { handle_cache(op_rx).await });

    // wait for clients
    loop {
        let (stream, _) = listener.accept().await?;
        println!("New connection: {}", stream.peer_addr()?);

        // handle new client
        let op_tx = op_tx.clone();
        tokio::task::spawn(async move { handle_client(stream, op_tx).await });
    }
}
