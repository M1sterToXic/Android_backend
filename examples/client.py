import socket

def start_client():
    client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client.connect(("localhost", 12345))

    client.sendall(b"Hello world!")

    response = client.recv(1024)
    print(f"Server response: {response.decode()}")

    client.close()

if __name__ == "__main__":
    start_client()