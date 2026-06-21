import socket

req = (
    "GET /mqtt HTTP/1.1\r\n"
    "Host: 127.0.0.1:9001\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "Sec-WebSocket-Protocol: mqtt\r\n"
    "\r\n"
)

s = socket.socket()
s.connect(("127.0.0.1", 9001))
s.sendall(req.encode())
resp = s.recv(4096)
print("Response:", resp.decode())
s.close()
