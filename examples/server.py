import socket

def start_server():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind(("localhost", 12345))
    server.listen(1)
    print("Server started on port 12345")

    conn, addr = server.accept()
    print(f"Connect by {addr}")

    data = conn.recv(1024)
    print(f"Received: {data.decode()}")

    conn.sendall(b"Hello Server")
    conn.close()
    server.close()

if __name__ == "__main__":
    start_server()