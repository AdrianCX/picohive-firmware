import socket
import os
from datetime import datetime
UDP_IP = "0.0.0.0"
UDP_PORT = 21000

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

files = {}

if not os.path.isdir("./logs"):
    os.mkdir("./logs")
    

while True:
    data, addr = sock.recvfrom(4096)

    f = None

    if addr in files:
        f = files[addr]
    else:
        filename = "./logs/" + str(addr[0]) + ".txt"
        print("New connection from: " + str(addr[0]) + ", logs in: " + filename )

        f = open(filename, 'ab')
        files[addr] = f

    f.write(bytes(datetime.today().strftime('[%Y-%m-%d %H:%M:%S] '), "utf-8"))
    f.write(data)
    f.flush()
    
