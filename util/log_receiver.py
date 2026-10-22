import socket

UDP_IP = "0.0.0.0"
UDP_PORT = 21000

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

while True:
    data, addr = sock.recvfrom(4096)
    print("%s" % data)
