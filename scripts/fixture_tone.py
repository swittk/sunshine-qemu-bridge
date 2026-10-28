import socket,time
s=socket.socket(socket.AF_UNIX);s.connect('bridge/run/av-fixture/qtest.sock');r=s.makefile('rb')
for t in ('outb 0x43 0xb6','outb 0x42 0x98','outb 0x42 0x0a','outb 0x61 0x33'): s.sendall((t+'\n').encode());print(r.readline().decode().strip(),flush=True)
time.sleep(60);s.sendall(b'outb 0x61 0x30\n');print(r.readline().decode().strip());s.close()
