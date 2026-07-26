"""Test Modbus TCP connection pattern matching C++ plugin behavior"""
import socket, struct, time, sys

req = bytes.fromhex('000100000006010300000004')  # FC03 addr=0 count=4

print(f"Testing Modbus TCP simulator at 127.0.0.1:1502")
sys.stdout.flush()

for i in range(5):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(2)
    t0 = time.time()
    
    try:
        s.connect(('127.0.0.1', 1502))
        t1 = time.time()
        s.send(req)
        t2 = time.time()
        resp = s.recv(1024)
        t3 = time.time()
        
        if len(resp) >= 11:
            val = struct.unpack(">H", resp[9:11])[0]
            print(f"Test {i+1}: conn={(t1-t0)*1000:.0f}ms send={(t2-t1)*1000:.0f}ms recv={(t3-t2)*1000:.0f}ms val={val}")
        else:
            print(f"Test {i+1}: short resp {len(resp)}B")
    except Exception as e:
        print(f"Test {i+1}: ERROR {e}")
    finally:
        s.close()
        sys.stdout.flush()
