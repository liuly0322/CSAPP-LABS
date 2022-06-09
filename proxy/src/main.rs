use anyhow::Result;
use fluent_uri::Uri;
use std::env;
use std::process;
use std::sync::Arc;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpListener;
use tokio::net::TcpStream;
use tokio::sync::Mutex;

const MAX_CACHE_SIZE: usize = 1048576;

async fn handle_client(
    mut client: TcpStream,
    cache: Arc<Mutex<lru::LruCache<String, Arc<Vec<u8>>>>>,
) -> Option<()> {
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
    let mut cache_guard = cache.lock().await;
    let buffer = cache_guard.get(uri.as_str()).cloned();
    drop(cache_guard);
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
    let mut buffer_valid = true;
    let mut origin_recv = [0_u8; 1024];
    while let Ok(size) = origin.read(&mut origin_recv).await {
        if size == 0 {
            break;
        }
        if buffer_valid {
            buffer.append(&mut origin_recv[0..size].to_vec());
            if buffer.len() > MAX_CACHE_SIZE {
                buffer_valid = false;
            }
        }
        client.write_all(&origin_recv[0..size]).await.ok()?;
    }

    // put value in cache
    if buffer_valid {
        let mut cache_guard = cache.lock().await;
        cache_guard.put(uri.to_string(), Arc::new(buffer));
        drop(cache_guard);
    }

    // stream exit
    println!("finish proxy from server: {}", client.peer_addr().ok()?);
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

    // cache
    let cache: lru::LruCache<String, Arc<Vec<u8>>> = lru::LruCache::new(32);
    let cache = Arc::new(Mutex::new(cache));

    // wait for clients
    loop {
        let (stream, _) = listener.accept().await?;
        let addr = stream.peer_addr()?;
        println!("New connection: {}", addr);

        // handle new client
        let cache = cache.clone();
        tokio::task::spawn(async move {
            handle_client(stream, cache).await;
            println!("Connection closed: {}", addr);
        });
    }
}
